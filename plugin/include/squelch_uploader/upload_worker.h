// SPDX-License-Identifier: GPL-3.0-or-later
//
// upload_worker.h — bounded MPMC job queue + worker thread pool that
// performs Squelch uploads off TR's call_end thread.
//
// Each worker owns its own `UploaderSlot`, produced by an injected factory.
// Production wires the factory to libcurl-backed `HttpClient`s; tests inject
// a fake slot that returns canned `HttpResult`s. libcurl's easy handles are
// not thread-safe, so one slot per worker is the contract.

#pragma once

#include "squelch_uploader/http_client.h"
#include "squelch_uploader/retry_policy.h"
#include "squelch_uploader/upload_request.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace squelch
{

    struct UploadJob
    {
        UploadRequest request;
        // Human-friendly identifier used in log lines (e.g. "tg=1234
        // started=2024-04-27T06:40:00Z"). Never contains the API key.
        std::string debug_tag;
    };

    // Per-worker uploader. The factory hands a fresh instance to each worker
    // thread; instances must NOT be shared across threads.
    struct UploaderSlot
    {
        virtual ~UploaderSlot() = default;
        virtual HttpResult upload(const UploadRequest &request) = 0;
    };
    using UploaderFactory = std::function<std::unique_ptr<UploaderSlot>()>;

    // Builds the production factory: each worker gets a libcurl-backed
    // `HttpClient` plus the `url`/`bearer_token` captured by value. Caller
    // owns the resulting factory; it is safe to copy and call from any thread.
    UploaderFactory make_http_uploader_factory(std::string url,
                                               std::string bearer_token);

    class UploadWorkerPool
    {
    public:
        UploadWorkerPool(std::size_t worker_count,
                         std::size_t queue_capacity,
                         RetryPolicy policy,
                         UploaderFactory factory);
        ~UploadWorkerPool();

        UploadWorkerPool(const UploadWorkerPool &) = delete;
        UploadWorkerPool &operator=(const UploadWorkerPool &) = delete;

        // Spawn workers. Idempotent — second call is a no-op. Each worker
        // calls `factory_()` exactly once on entry; if any factory call
        // throws or returns nullptr, that worker exits immediately and the
        // pool runs with fewer workers (logged once).
        void start();

        // Non-blocking: enqueues the job if there is room, otherwise
        // increments `dropped()` and returns false. Never blocks the caller
        // (TR's call_end thread must keep moving).
        bool enqueue(UploadJob &&job);

        // Signal shutdown, wake all workers, and wait up to `drain_timeout`
        // for the queue to drain. Workers stop consuming new jobs after the
        // current one returns. If they don't all exit within the timeout, the
        // remaining threads are detached (last-resort path that lets TR exit
        // even if a worker is wedged inside libcurl).
        void stop(std::chrono::milliseconds drain_timeout);

        std::size_t pending() const;
        std::size_t dropped() const noexcept { return dropped_.load(); }
        std::size_t in_flight() const noexcept { return in_flight_.load(); }

    private:
        void worker_loop(std::unique_ptr<UploaderSlot> slot);

        // Runs an upload with retry. Returns when the job either succeeds,
        // fails non-retriably, exhausts attempts, or shutdown was signalled
        // mid-backoff. Logs at the appropriate severity.
        void process_job(UploaderSlot &slot, const UploadJob &job);

        // Sleep for `delay`, returning true if shutdown was signalled while
        // we slept (so the worker should bail without further retries).
        bool sleep_or_stop(std::chrono::milliseconds delay);

        const std::size_t worker_count_;
        const std::size_t queue_capacity_;
        const RetryPolicy policy_;
        UploaderFactory factory_;

        mutable std::mutex mu_;
        std::condition_variable not_empty_;
        std::condition_variable workers_done_;
        std::queue<UploadJob> queue_;

        bool started_ = false;
        bool stopping_ = false;
        std::size_t active_workers_ = 0;
        std::atomic<std::size_t> dropped_{0};
        std::atomic<std::size_t> in_flight_{0};

        std::vector<std::thread> workers_;
    };

} // namespace squelch
