// SPDX-License-Identifier: GPL-3.0-or-later
//
// upload_worker.cc — bounded job queue + worker pool implementation.
//
// Logging here goes through std::clog (not Boost.Log) so the unit-test
// binary doesn't need to link Boost. The real plugin still routes Boost.Log
// to the same destination TR uses; std::clog is unbuffered enough for
// operator-visible warnings/errors.

#include "squelch_uploader/upload_worker.h"

#include "squelch_uploader.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <random>
#include <utility>

namespace squelch
{

    namespace
    {

        // Per-worker RNG for jitter. seeded from std::random_device so
        // separate workers don't synchronize their backoffs.
        class JitterRng
        {
        public:
            JitterRng() : engine_(std::random_device{}()) {}
            double operator()()
            {
                std::uniform_real_distribution<double> dist(0.0, 1.0);
                return dist(engine_);
            }

        private:
            std::mt19937_64 engine_;
        };

        void log_warn(const std::string &msg)
        {
            std::clog << "[" << kPluginName << "] WARN " << msg << '\n';
        }
        void log_error(const std::string &msg)
        {
            std::clog << "[" << kPluginName << "] ERROR " << msg << '\n';
        }
        void log_info(const std::string &msg)
        {
            std::clog << "[" << kPluginName << "] " << msg << '\n';
        }

    } // namespace

    // Note: `make_http_uploader_factory` is defined in
    // `squelch_uploader.cc` (production-only TU) so this file stays free of
    // libcurl dependencies and links cleanly into the test binary.

    UploadWorkerPool::UploadWorkerPool(std::size_t worker_count,
                                       std::size_t queue_capacity,
                                       RetryPolicy policy,
                                       UploaderFactory factory)
        : worker_count_(worker_count == 0 ? 1 : worker_count),
          queue_capacity_(queue_capacity == 0 ? 1 : queue_capacity),
          policy_(policy),
          factory_(std::move(factory))
    {
    }

    UploadWorkerPool::~UploadWorkerPool()
    {
        // RAII: if the user forgot to stop(), drain with a generous timeout
        // so we never leave joinable threads dangling. We never throw out of
        // the destructor.
        try
        {
            stop(std::chrono::seconds(30));
        }
        catch (...)
        {
            // Last resort: detach anything still joinable.
            for (auto &t : workers_)
            {
                if (t.joinable())
                    t.detach();
            }
        }
    }

    void UploadWorkerPool::start()
    {
        std::scoped_lock lk(mu_);
        if (started_)
            return;
        started_ = true;
        stopping_ = false;
        active_workers_ = 0;
        workers_.reserve(worker_count_);
        for (std::size_t i = 0; i < worker_count_; ++i)
        {
            std::unique_ptr<UploaderSlot> slot;
            try
            {
                slot = factory_();
            }
            catch (const std::exception &ex)
            {
                log_error(std::string("uploader factory threw: ") + ex.what());
                continue;
            }
            catch (...)
            {
                log_error("uploader factory threw unknown exception");
                continue;
            }
            if (!slot)
            {
                log_error("uploader factory returned nullptr; worker skipped");
                continue;
            }
            ++active_workers_;
            workers_.emplace_back(
                [this, s = std::move(slot)]() mutable
                { worker_loop(std::move(s)); });
        }
    }

    bool UploadWorkerPool::enqueue(UploadJob &&job)
    {
        {
            std::scoped_lock lk(mu_);
            if (stopping_ || !started_)
            {
                ++dropped_;
                return false;
            }
            if (queue_.size() >= queue_capacity_)
            {
                ++dropped_;
                log_warn("upload queue full (capacity=" +
                         std::to_string(queue_capacity_) + "); dropping " +
                         job.debug_tag);
                return false;
            }
            queue_.push(std::move(job));
        }
        not_empty_.notify_one();
        return true;
    }

    void UploadWorkerPool::stop(std::chrono::milliseconds drain_timeout)
    {
        {
            std::scoped_lock lk(mu_);
            if (!started_)
                return;
            stopping_ = true;
        }
        not_empty_.notify_all();

        const auto deadline = std::chrono::steady_clock::now() + drain_timeout;
        bool clean_exit = false;
        {
            std::unique_lock lk(mu_);
            clean_exit = workers_done_.wait_until(
                lk, deadline, [this]
                { return active_workers_ == 0; });
        }

        if (clean_exit)
        {
            for (auto &t : workers_)
            {
                if (t.joinable())
                    t.join();
            }
        }
        else
        {
            log_warn("shutdown drain timed out after " +
                     std::to_string(drain_timeout.count()) +
                     " ms; detaching " + std::to_string(workers_.size()) +
                     " worker(s)");
            for (auto &t : workers_)
            {
                if (t.joinable())
                    t.detach();
            }
        }
        workers_.clear();
        std::scoped_lock lk(mu_);
        started_ = false;
    }

    std::size_t UploadWorkerPool::pending() const
    {
        std::scoped_lock lk(mu_);
        return queue_.size();
    }

    bool UploadWorkerPool::sleep_or_stop(std::chrono::milliseconds delay)
    {
        if (delay <= std::chrono::milliseconds{0})
        {
            std::scoped_lock lk(mu_);
            return stopping_;
        }
        std::unique_lock lk(mu_);
        // wait_for returns true if the predicate is satisfied (i.e. we were
        // woken by stop()); false on timeout (full delay elapsed).
        return not_empty_.wait_for(lk, delay, [this]
                                   { return stopping_; });
    }

    void UploadWorkerPool::process_job(UploaderSlot &slot, const UploadJob &job)
    {
        const unsigned attempts =
            policy_.max_attempts == 0 ? 1u : policy_.max_attempts;
        JitterRng rng;
        std::function<double()> rng_fn = [&rng]
        { return rng(); };

        for (unsigned attempt = 0; attempt < attempts; ++attempt)
        {
            HttpResult result;
            try
            {
                result = slot.upload(job.request);
            }
            catch (const std::exception &ex)
            {
                // Treat an exception from the slot as a transport error so
                // retry can kick in. Never let it cross back into TR.
                result.status_code = 0;
                result.error_message =
                    std::string("uploader exception: ") + ex.what();
            }
            catch (...)
            {
                result.status_code = 0;
                result.error_message = "uploader unknown exception";
            }

            const bool network_error = !result.error_message.empty();
            const long status = result.status_code;

            if (!network_error && status >= 200 && status < 300)
            {
                if (attempt > 0)
                {
                    log_info("upload succeeded after " +
                             std::to_string(attempt + 1) + " attempts (" +
                             job.debug_tag + ")");
                }
                return;
            }

            const bool retriable =
                is_retriable(static_cast<int>(status), network_error);
            const bool last_attempt = (attempt + 1 >= attempts);

            if (!retriable)
            {
                log_error("upload rejected (" + std::to_string(status) +
                          ") for " + job.debug_tag + "; not retrying");
                return;
            }

            if (last_attempt)
            {
                if (network_error)
                {
                    log_error("upload failed after " +
                              std::to_string(attempts) +
                              " attempts (network error: " +
                              result.error_message + ") for " + job.debug_tag);
                }
                else
                {
                    log_error("upload failed after " +
                              std::to_string(attempts) + " attempts (HTTP " +
                              std::to_string(status) + ") for " +
                              job.debug_tag);
                }
                return;
            }

            const auto delay = backoff_for_attempt(policy_, attempt, rng_fn);
            log_warn("upload attempt " + std::to_string(attempt + 1) + "/" +
                     std::to_string(attempts) + " failed (" +
                     (network_error ? result.error_message
                                    : "HTTP " + std::to_string(status)) +
                     "); retrying in " +
                     std::to_string(delay.count()) + " ms (" +
                     job.debug_tag + ")");
            if (sleep_or_stop(delay))
            {
                log_warn("shutdown during retry backoff; dropping " +
                         job.debug_tag);
                return;
            }
        }
    }

    void UploadWorkerPool::worker_loop(std::unique_ptr<UploaderSlot> slot)
    {
        // Decrement-on-exit guard, regardless of how the loop terminates.
        struct ExitGuard
        {
            UploadWorkerPool *self;
            ~ExitGuard()
            {
                {
                    std::scoped_lock lk(self->mu_);
                    if (self->active_workers_ > 0)
                        --self->active_workers_;
                }
                self->workers_done_.notify_all();
            }
        } guard{this};

        for (;;)
        {
            UploadJob job;
            {
                std::unique_lock lk(mu_);
                not_empty_.wait(lk, [this]
                                { return stopping_ || !queue_.empty(); });
                if (queue_.empty())
                {
                    // Stopping with nothing left to do.
                    return;
                }
                job = std::move(queue_.front());
                queue_.pop();
            }

            ++in_flight_;
            try
            {
                process_job(*slot, job);
            }
            catch (...)
            {
                // Defence-in-depth: process_job catches its own slot
                // exceptions, but if anything else (e.g. std::bad_alloc in
                // logging) escapes, we must not propagate into TR.
                log_error("worker swallowed unexpected exception");
            }
            --in_flight_;
        }
    }

} // namespace squelch
