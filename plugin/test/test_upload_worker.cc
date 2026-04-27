// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for UploadWorkerPool. We inject a fake UploaderSlot via the
// pool's factory so no libcurl traffic happens.

#include "squelch_uploader/retry_policy.h"
#include "squelch_uploader/upload_worker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using squelch::HttpResult;
using squelch::RetryPolicy;
using squelch::UploaderFactory;
using squelch::UploaderSlot;
using squelch::UploadJob;
using squelch::UploadRequest;
using squelch::UploadWorkerPool;
using std::chrono::milliseconds;

namespace
{

    UploadJob make_job(const std::string &tag = "tg=1")
    {
        UploadJob j;
        j.request.system_id = "1";
        j.request.talkgroup_id = "1";
        j.request.started_at = "2024-04-27T06:40:00Z";
        j.request.audio_path = "/dev/null";
        j.request.audio_content_type = "audio/wav";
        j.debug_tag = tag;
        return j;
    }

    RetryPolicy fast_policy(unsigned attempts = 3)
    {
        RetryPolicy p;
        p.max_attempts = attempts;
        p.base = milliseconds(1);
        p.cap = milliseconds(5);
        p.jitter_ratio = 0.0;
        return p;
    }

    // Test harness that records every upload call. The behaviour is
    // controlled by a callback shared across all slots produced by a
    // factory, so the test can drive scenario logic from one place.
    struct FakeBackend
    {
        std::mutex mu;
        std::condition_variable cv;
        std::atomic<int> total_calls{0};
        std::atomic<int> success{0};
        std::atomic<int> failure{0};

        // Per-tag attempt counter, lets a callback decide what to return on
        // the Nth call for a given debug_tag.
        std::map<std::string, int> attempts_per_tag;

        // Optional sleep applied to every upload (used by the slow-shutdown
        // test).
        milliseconds upload_delay{0};

        // Returns the HttpResult for the current attempt of `tag`. The test
        // sets this; default returns 200.
        std::function<HttpResult(const std::string &, int)> respond =
            [](const std::string &, int)
        {
            HttpResult r;
            r.status_code = 200;
            r.body = "ok";
            return r;
        };
    };

    class FakeSlot final : public UploaderSlot
    {
    public:
        explicit FakeSlot(FakeBackend *b) : backend_(b) {}
        HttpResult upload(const UploadRequest &req) override
        {
            (void)req;
            // We need a tag to drive scenarios; the request itself doesn't
            // carry one, so fall back to talkgroup_id.
            const std::string tag = req.talkgroup_id;
            int attempt = 0;
            {
                std::scoped_lock lk(backend_->mu);
                attempt = ++backend_->attempts_per_tag[tag];
            }
            backend_->total_calls.fetch_add(1);
            if (backend_->upload_delay.count() > 0)
                std::this_thread::sleep_for(backend_->upload_delay);
            HttpResult r = backend_->respond(tag, attempt);
            const bool ok = r.error_message.empty() &&
                            r.status_code >= 200 && r.status_code < 300;
            if (ok)
                backend_->success.fetch_add(1);
            else
                backend_->failure.fetch_add(1);
            backend_->cv.notify_all();
            return r;
        }

    private:
        FakeBackend *backend_;
    };

    UploaderFactory fake_factory(FakeBackend *backend)
    {
        return [backend]() -> std::unique_ptr<UploaderSlot>
        { return std::make_unique<FakeSlot>(backend); };
    }

    // Wait until predicate is true or `timeout` elapses. Returns true on
    // success.
    template <typename Pred>
    bool wait_for(FakeBackend &b, Pred pred, milliseconds timeout)
    {
        std::unique_lock lk(b.mu);
        return b.cv.wait_for(lk, timeout, pred);
    }

} // namespace

// ---- Successful uploads ---------------------------------------------------

TEST(UploadWorkerPool, EnqueuesAndProcessesAll)
{
    FakeBackend backend;
    UploadWorkerPool pool(2, 16, fast_policy(3), fake_factory(&backend));
    pool.start();

    constexpr int kJobs = 5;
    for (int i = 0; i < kJobs; ++i)
    {
        UploadJob j = make_job();
        j.request.talkgroup_id = std::to_string(i);
        EXPECT_TRUE(pool.enqueue(std::move(j)));
    }

    pool.stop(milliseconds(2000));
    EXPECT_EQ(backend.success.load(), kJobs);
    EXPECT_EQ(backend.failure.load(), 0);
    EXPECT_EQ(pool.dropped(), 0u);
}

// ---- Retry behaviour ------------------------------------------------------

TEST(UploadWorkerPool, RetriesOn5xxThenSucceeds)
{
    FakeBackend backend;
    backend.respond = [](const std::string &, int attempt)
    {
        HttpResult r;
        if (attempt < 3)
        {
            r.status_code = 500;
            r.body = "boom";
        }
        else
        {
            r.status_code = 200;
            r.body = "ok";
        }
        return r;
    };
    UploadWorkerPool pool(1, 8, fast_policy(5), fake_factory(&backend));
    pool.start();
    pool.enqueue(make_job("retry-5xx"));

    ASSERT_TRUE(wait_for(backend,
                         [&] { return backend.success.load() == 1; },
                         milliseconds(2000)));
    pool.stop(milliseconds(1000));
    EXPECT_EQ(backend.total_calls.load(), 3);
    EXPECT_EQ(backend.success.load(), 1);
    EXPECT_EQ(backend.failure.load(), 2);
}

TEST(UploadWorkerPool, DoesNotRetryOn4xx)
{
    FakeBackend backend;
    backend.respond = [](const std::string &, int)
    {
        HttpResult r;
        r.status_code = 401;
        r.body = "nope";
        return r;
    };
    UploadWorkerPool pool(1, 8, fast_policy(5), fake_factory(&backend));
    pool.start();
    pool.enqueue(make_job("forbidden"));

    pool.stop(milliseconds(1000));
    EXPECT_EQ(backend.total_calls.load(), 1);
    EXPECT_EQ(backend.success.load(), 0);
}

TEST(UploadWorkerPool, NetworkErrorRetriedThenExhausted)
{
    FakeBackend backend;
    backend.respond = [](const std::string &, int)
    {
        HttpResult r;
        r.status_code = 0;
        r.error_message = "connection reset";
        return r;
    };
    UploadWorkerPool pool(1, 8, fast_policy(3), fake_factory(&backend));
    pool.start();
    pool.enqueue(make_job("flaky"));

    ASSERT_TRUE(wait_for(backend,
                         [&] { return backend.total_calls.load() == 3; },
                         milliseconds(2000)));
    pool.stop(milliseconds(1000));
    EXPECT_EQ(backend.total_calls.load(), 3);
    EXPECT_EQ(backend.success.load(), 0);
}

// ---- Queue overflow -------------------------------------------------------

TEST(UploadWorkerPool, DropsWhenQueueFull)
{
    FakeBackend backend;
    backend.upload_delay = milliseconds(50);
    // 1 worker, capacity 2 → with a slow upload, fast enqueueing overflows.
    UploadWorkerPool pool(1, 2, fast_policy(1), fake_factory(&backend));
    pool.start();

    int accepted = 0;
    int rejected = 0;
    for (int i = 0; i < 50; ++i)
    {
        if (pool.enqueue(make_job(std::to_string(i))))
            ++accepted;
        else
            ++rejected;
    }
    EXPECT_GT(rejected, 0);
    EXPECT_EQ(static_cast<std::size_t>(rejected), pool.dropped());

    pool.stop(milliseconds(5000));
    EXPECT_EQ(backend.total_calls.load(), accepted);
}

// ---- Graceful shutdown ----------------------------------------------------

TEST(UploadWorkerPool, GracefulShutdownDrainsQueue)
{
    FakeBackend backend;
    UploadWorkerPool pool(4, 256, fast_policy(1), fake_factory(&backend));
    pool.start();

    constexpr int kJobs = 100;
    for (int i = 0; i < kJobs; ++i)
    {
        UploadJob j = make_job();
        j.request.talkgroup_id = std::to_string(i);
        ASSERT_TRUE(pool.enqueue(std::move(j)));
    }

    pool.stop(milliseconds(5000));
    EXPECT_EQ(backend.total_calls.load(), kJobs);
    EXPECT_EQ(backend.success.load(), kJobs);
    EXPECT_EQ(pool.dropped(), 0u);
    EXPECT_EQ(pool.pending(), 0u);
}

TEST(UploadWorkerPool, ShutdownTimeoutDetachesSlowWorker)
{
    FakeBackend backend;
    backend.upload_delay = milliseconds(2000);
    UploadWorkerPool pool(1, 8, fast_policy(1), fake_factory(&backend));
    pool.start();
    pool.enqueue(make_job("slow"));

    // Make sure the worker has actually started the upload before we ask
    // it to stop, so we exercise the timeout path rather than the
    // queue-empty fast-exit path.
    ASSERT_TRUE(wait_for(
        backend, [&]
        { return backend.total_calls.load() >= 1; },
        milliseconds(1000)));

    const auto t0 = std::chrono::steady_clock::now();
    pool.stop(milliseconds(100));
    const auto elapsed =
        std::chrono::duration_cast<milliseconds>(
            std::chrono::steady_clock::now() - t0);
    EXPECT_LT(elapsed, milliseconds(800))
        << "stop() blocked for " << elapsed.count() << "ms";
}
