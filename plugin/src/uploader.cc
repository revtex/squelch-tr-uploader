// SPDX-License-Identifier: GPL-3.0-or-later
//
// uploader.cc — single-thread libcurl-backed uploader for Squelch.

#include "squelch_uploader/uploader.h"
#include "squelch_uploader.h"

#include <curl/curl.h>

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <utility>

namespace squelch
{

    namespace
    {

        constexpr std::chrono::milliseconds kBackoffBase{1000};
        constexpr std::chrono::milliseconds kBackoffCap{30000};
        constexpr double kJitterRatio = 0.20;

        std::string to_lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });
            return s;
        }

        void ensure_curl_global_init()
        {
            static std::once_flag once;
            std::call_once(once, []()
                           { curl_global_init(CURL_GLOBAL_DEFAULT); });
        }

        std::size_t write_cb(void *contents, std::size_t size, std::size_t nmemb,
                             void *userp)
        {
            const std::size_t bytes = size * nmemb;
            auto *buf = static_cast<std::string *>(userp);
            buf->append(static_cast<char *>(contents), bytes);
            return bytes;
        }

        bool is_retriable(long status_code, bool network_error) noexcept
        {
            if (network_error)
                return true;
            if (status_code >= 500 && status_code < 600)
                return true;
            if (status_code == 408 || status_code == 429)
                return true;
            return false;
        }

        std::chrono::milliseconds backoff_for_attempt(unsigned attempt_index)
        {
            std::int64_t delay = kBackoffBase.count();
            const unsigned safe_shift = std::min<unsigned>(attempt_index, 30u);
            for (unsigned i = 0; i < safe_shift; ++i)
            {
                if (delay >= kBackoffCap.count())
                {
                    delay = kBackoffCap.count();
                    break;
                }
                delay *= 2;
            }
            if (delay > kBackoffCap.count())
                delay = kBackoffCap.count();

            // Per-thread RNG so jitter is non-deterministic without
            // re-seeding from random_device on every call.
            static thread_local std::mt19937_64 rng(std::random_device{}());
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            const double r = dist(rng);
            const double scale = 1.0 + kJitterRatio * (2.0 * r - 1.0);
            const double scaled = static_cast<double>(delay) * scale;
            if (scaled <= 0.0)
                return std::chrono::milliseconds{0};
            return std::chrono::milliseconds{static_cast<std::int64_t>(scaled)};
        }

    } // namespace

    void Multipart::add_string(std::string name, std::string value)
    {
        strings_.push_back({std::move(name), std::move(value)});
    }

    void Multipart::add_file(std::string name, std::string path,
                             std::string content_type)
    {
        files_.push_back({std::move(name), std::move(path),
                          std::move(content_type)});
    }

    std::string build_upload_url(const std::string &server)
    {
        std::string base = server;
        while (!base.empty() && base.back() == '/')
            base.pop_back();
        return base + "/api/v1/calls";
    }

    std::string audio_content_type_for(const std::string &path)
    {
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= path.size())
            return "application/octet-stream";
        const std::string ext = to_lower(path.substr(dot + 1));
        if (ext == "wav")
            return "audio/wav";
        if (ext == "mp3")
            return "audio/mpeg";
        if (ext == "aac")
            return "audio/aac";
        if (ext == "m4a")
            return "audio/m4a";
        if (ext == "ogg")
            return "audio/ogg";
        if (ext == "opus")
            return "audio/opus";
        return "application/octet-stream";
    }

    Uploader::Uploader(std::string url, std::string bearer_token,
                       unsigned max_retries)
        : url_(std::move(url)),
          bearer_token_(std::move(bearer_token)),
          max_retries_(max_retries)
    {
        ensure_curl_global_init();
        curl_ = curl_easy_init();
    }

    Uploader::~Uploader()
    {
        try
        {
            stop();
        }
        catch (...)
        {
            // Never let an exception escape; TR is in our process.
        }
        if (curl_ != nullptr)
        {
            curl_easy_cleanup(curl_);
            curl_ = nullptr;
        }
    }

    void Uploader::start()
    {
        std::scoped_lock lk(mu_);
        if (started_)
            return;
        started_ = true;
        stopping_ = false;
        worker_ = std::thread([this]()
                              { worker_loop(); });
    }

    void Uploader::enqueue(UploadJob job)
    {
        {
            std::scoped_lock lk(mu_);
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

    void Uploader::stop()
    {
        {
            std::scoped_lock lk(mu_);
            if (!started_)
                return;
            stopping_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable())
            worker_.join();
        std::scoped_lock lk(mu_);
        started_ = false;
    }

    bool Uploader::perform_upload(const UploadJob &job, std::string *error_out,
                                  long *status_out)
    {
        *error_out = {};
        *status_out = 0;

        if (curl_ == nullptr)
        {
            *error_out = "curl handle not initialised";
            return false;
        }

        // Reset prior options so a repeat call doesn't reuse stale state.
        curl_easy_reset(curl_);

        Multipart body;
        job.request.to_multipart(body);

        curl_mime *mime = curl_mime_init(curl_);
        for (const auto &f : body.strings())
        {
            curl_mimepart *part = curl_mime_addpart(mime);
            curl_mime_name(part, f.name.c_str());
            curl_mime_data(part, f.value.c_str(), f.value.size());
        }
        for (const auto &f : body.files())
        {
            curl_mimepart *part = curl_mime_addpart(mime);
            curl_mime_name(part, f.name.c_str());
            curl_mime_filedata(part, f.path.c_str());
            if (!f.content_type.empty())
                curl_mime_type(part, f.content_type.c_str());
        }

        curl_slist *headerlist = nullptr;
        const std::string auth = "Authorization: Bearer " + bearer_token_;
        headerlist = curl_slist_append(headerlist, auth.c_str());
        // Disable libcurl's automatic Expect: 100-continue.
        headerlist = curl_slist_append(headerlist, "Expect:");

        const std::string user_agent =
            std::string(kPluginName) + "/" + kPluginVersion;

        char errbuf[CURL_ERROR_SIZE];
        errbuf[0] = '\0';
        std::string response_body;

        curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_USERAGENT, user_agent.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headerlist);
        curl_easy_setopt(curl_, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, errbuf);

        const CURLcode rc = curl_easy_perform(curl_);
        bool network_error = false;
        if (rc != CURLE_OK)
        {
            *error_out = errbuf[0] != '\0'
                             ? std::string(errbuf)
                             : std::string(curl_easy_strerror(rc));
            network_error = true;
        }
        else
        {
            long status = 0;
            curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status);
            *status_out = status;
        }

        curl_slist_free_all(headerlist);
        curl_mime_free(mime);

        if (network_error)
            return false;
        return *status_out >= 200 && *status_out < 300;
    }

    void Uploader::worker_loop()
    {
        for (;;)
        {
            UploadJob job;
            {
                std::unique_lock lk(mu_);
                cv_.wait(lk, [this]
                         { return stopping_ || !queue_.empty(); });
                if (queue_.empty())
                    return; // stopping with nothing left to do
                job = std::move(queue_.front());
                queue_.pop_front();
            }

            const unsigned attempts =
                max_retries_ == 0 ? 1u : max_retries_ + 1u;

            for (unsigned attempt = 0; attempt < attempts; ++attempt)
            {
                std::string err;
                long status = 0;
                bool ok = false;
                try
                {
                    ok = perform_upload(job, &err, &status);
                }
                catch (const std::exception &ex)
                {
                    err = std::string("uploader exception: ") + ex.what();
                    status = 0;
                }
                catch (...)
                {
                    err = "uploader unknown exception";
                    status = 0;
                }

                if (ok)
                {
                    if (attempt > 0)
                    {
                        BOOST_LOG_TRIVIAL(info)
                            << "[" << kPluginName << "] upload succeeded after "
                            << (attempt + 1) << " attempts (" << job.debug_tag
                            << ")";
                    }
                    break;
                }

                const bool network_error = !err.empty();
                const bool retriable = is_retriable(status, network_error);
                const bool last_attempt = (attempt + 1 >= attempts);

                if (!retriable)
                {
                    BOOST_LOG_TRIVIAL(error)
                        << "[" << kPluginName << "] upload rejected (HTTP "
                        << status << ") for " << job.debug_tag
                        << "; not retrying";
                    break;
                }

                if (last_attempt)
                {
                    if (network_error)
                    {
                        BOOST_LOG_TRIVIAL(error)
                            << "[" << kPluginName << "] upload failed after "
                            << attempts << " attempts (network error: "
                            << err << ") for " << job.debug_tag;
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(error)
                            << "[" << kPluginName << "] upload failed after "
                            << attempts << " attempts (HTTP " << status
                            << ") for " << job.debug_tag;
                    }
                    break;
                }

                const auto delay = backoff_for_attempt(attempt);
                BOOST_LOG_TRIVIAL(warning)
                    << "[" << kPluginName << "] upload attempt "
                    << (attempt + 1) << "/" << attempts << " failed ("
                    << (network_error ? err : "HTTP " + std::to_string(status))
                    << "); retrying in " << delay.count() << " ms ("
                    << job.debug_tag << ")";

                // Sleep through the backoff unconditionally; stop() waits
                // for the current retry chain plus the remaining queue to
                // drain fully.
                std::this_thread::sleep_for(delay);
            }
        }
    }

} // namespace squelch
