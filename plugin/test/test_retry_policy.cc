// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for retry_policy: backoff math, jitter, and is_retriable.

#include "squelch_uploader/retry_policy.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>

using squelch::backoff_for_attempt;
using squelch::is_retriable;
using squelch::RetryPolicy;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace
{

    RetryPolicy default_policy()
    {
        RetryPolicy p;
        p.max_attempts = 5;
        p.base = seconds(1);
        p.cap = seconds(30);
        p.jitter_ratio = 0.0;
        return p;
    }

} // namespace

TEST(BackoffForAttempt, ExponentialDoublingNoJitter)
{
    auto p = default_policy();
    EXPECT_EQ(backoff_for_attempt(p, 0, nullptr), milliseconds(1000));
    EXPECT_EQ(backoff_for_attempt(p, 1, nullptr), milliseconds(2000));
    EXPECT_EQ(backoff_for_attempt(p, 2, nullptr), milliseconds(4000));
    EXPECT_EQ(backoff_for_attempt(p, 3, nullptr), milliseconds(8000));
    EXPECT_EQ(backoff_for_attempt(p, 4, nullptr), milliseconds(16000));
}

TEST(BackoffForAttempt, CappedAtCeiling)
{
    auto p = default_policy();
    // 32s would exceed 30s cap.
    EXPECT_EQ(backoff_for_attempt(p, 5, nullptr), milliseconds(30000));
    EXPECT_EQ(backoff_for_attempt(p, 10, nullptr), milliseconds(30000));
    // Very large attempt indices must not overflow the math.
    EXPECT_EQ(backoff_for_attempt(p, 100, nullptr), milliseconds(30000));
}

TEST(BackoffForAttempt, JitterBandIsSymmetric)
{
    RetryPolicy p = default_policy();
    p.jitter_ratio = 0.20;

    // r = 0.5  -> scale = 1.0 -> exact base value.
    std::function<double()> mid = []
    { return 0.5; };
    EXPECT_EQ(backoff_for_attempt(p, 0, mid), milliseconds(1000));

    // r = 0.0  -> scale = 0.80 -> 800ms.
    std::function<double()> lo = []
    { return 0.0; };
    EXPECT_EQ(backoff_for_attempt(p, 0, lo), milliseconds(800));

    // r = 1.0  -> scale = 1.20 -> 1200ms (boundary clamps to 1.0).
    std::function<double()> hi = []
    { return 1.0; };
    EXPECT_EQ(backoff_for_attempt(p, 0, hi), milliseconds(1200));
}

TEST(BackoffForAttempt, JitterStaysWithinBand)
{
    RetryPolicy p = default_policy();
    p.jitter_ratio = 0.20;
    // For attempt 2 (4000ms), jitter should keep result in [3200, 4800].
    for (double r : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0})
    {
        std::function<double()> rng = [r]
        { return r; };
        const auto d = backoff_for_attempt(p, 2, rng);
        EXPECT_GE(d, milliseconds(3200));
        EXPECT_LE(d, milliseconds(4800));
    }
}

TEST(BackoffForAttempt, ZeroJitterIgnoresRng)
{
    RetryPolicy p = default_policy();
    p.jitter_ratio = 0.0;
    bool called = false;
    std::function<double()> rng = [&called]
    { called = true; return 0.0; };
    EXPECT_EQ(backoff_for_attempt(p, 1, rng), milliseconds(2000));
    EXPECT_FALSE(called);
}

TEST(IsRetriable, SuccessNotRetriable)
{
    EXPECT_FALSE(is_retriable(200, false));
    EXPECT_FALSE(is_retriable(201, false));
    EXPECT_FALSE(is_retriable(204, false));
    EXPECT_FALSE(is_retriable(301, false));
}

TEST(IsRetriable, ClientErrorsNotRetriable)
{
    EXPECT_FALSE(is_retriable(400, false));
    EXPECT_FALSE(is_retriable(401, false));
    EXPECT_FALSE(is_retriable(403, false));
    EXPECT_FALSE(is_retriable(404, false));
    EXPECT_FALSE(is_retriable(422, false));
}

TEST(IsRetriable, TimeoutAndRateLimitRetriable)
{
    EXPECT_TRUE(is_retriable(408, false));
    EXPECT_TRUE(is_retriable(429, false));
}

TEST(IsRetriable, ServerErrorsRetriable)
{
    EXPECT_TRUE(is_retriable(500, false));
    EXPECT_TRUE(is_retriable(502, false));
    EXPECT_TRUE(is_retriable(503, false));
    EXPECT_TRUE(is_retriable(504, false));
}

TEST(IsRetriable, NetworkErrorAlwaysRetriable)
{
    EXPECT_TRUE(is_retriable(0, true));
    EXPECT_TRUE(is_retriable(200, true));
    EXPECT_TRUE(is_retriable(400, true));
    EXPECT_TRUE(is_retriable(500, true));
}
