// SPDX-License-Identifier: GPL-3.0-or-later
//
// retry_policy.h — exponential-backoff parameters and helpers for the
// upload worker pool. Pure functions, no I/O, no global state — easy to
// unit-test.

#pragma once

#include <chrono>
#include <functional>

namespace squelch
{

    struct RetryPolicy
    {
        // Total attempts including the first one. `max_attempts == 1`
        // disables retry; `max_attempts == 0` is treated as 1.
        unsigned max_attempts = 3;

        // Base delay used as `base * 2^attempt_index`.
        std::chrono::milliseconds base = std::chrono::seconds(1);

        // Hard ceiling for any single delay, before jitter is applied.
        std::chrono::milliseconds cap = std::chrono::seconds(30);

        // Symmetric jitter ratio in [0.0, 1.0]. The final delay is
        // `delay * (1 + jitter_ratio * (2*r - 1))` with r in [0, 1).
        double jitter_ratio = 0.20;
    };

    // Returns the delay to wait *before* attempt `attempt_index + 1`. So
    // attempt_index=0 covers the wait between attempts 1 and 2, etc.
    // `rng_uniform_0_1` is called exactly once per invocation; injectable
    // for deterministic tests. Pass `nullptr` to disable jitter.
    std::chrono::milliseconds backoff_for_attempt(
        const RetryPolicy &policy,
        unsigned attempt_index,
        const std::function<double()> &rng_uniform_0_1);

    // Classifies an HTTP/transport outcome as retriable. The Squelch wire
    // contract treats 4xx as deterministic except for 408/429.
    //   * `network_error == true`  — retriable regardless of status_code
    //   * status 5xx, 408, 429     — retriable
    //   * any other 4xx, 2xx, 3xx — not retriable
    bool is_retriable(int status_code, bool network_error) noexcept;

} // namespace squelch
