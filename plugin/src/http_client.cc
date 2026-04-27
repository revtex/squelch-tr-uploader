// SPDX-License-Identifier: GPL-3.0-or-later
//
// http_client.cc — libcurl-backed HTTP client. The pure helpers
// (build_headers, build_upload_url, audio_content_type_for) live in
// http_client_util.cc so the test binary doesn't need libcurl.

#include "squelch_uploader/http_client.h"
#include "squelch_uploader.h"

#include <curl/curl.h>

#include <cstddef>
#include <mutex>
#include <string>

namespace squelch
{

    namespace
    {

        // libcurl requires a one-time global init. Threadsafe via std::once.
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

    } // namespace

    HttpClient::HttpClient()
    {
        ensure_curl_global_init();
    }

    HttpClient::~HttpClient() = default;

    HttpResult HttpClient::upload(const std::string &url,
                                  const std::string &bearer_token,
                                  const Multipart &body)
    {
        HttpResult result;

        CURL *curl = curl_easy_init();
        if (curl == nullptr)
        {
            result.error_message = "curl_easy_init failed";
            return result;
        }

        curl_mime *mime = curl_mime_init(curl);
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
            {
                curl_mime_type(part, f.content_type.c_str());
            }
        }

        curl_slist *headerlist = nullptr;
        for (const auto &h : build_headers(bearer_token))
        {
            headerlist = curl_slist_append(headerlist, h.c_str());
        }

        const std::string user_agent =
            std::string(kPluginName) + "/" + kPluginVersion;

        char errbuf[CURL_ERROR_SIZE];
        errbuf[0] = '\0';

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

        const CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK)
        {
            result.error_message = errbuf[0] != '\0'
                                       ? std::string(errbuf)
                                       : std::string(curl_easy_strerror(rc));
        }
        else
        {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            result.status_code = status;
        }

        curl_slist_free_all(headerlist);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);

        return result;
    }

} // namespace squelch
