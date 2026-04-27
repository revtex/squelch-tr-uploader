// SPDX-License-Identifier: GPL-3.0-or-later

#include "squelch_uploader/retry_policy.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace squelch
{

    std::chrono::milliseconds backoff_for_attempt(
        const RetryPolicy &policy,
        unsigned attempt_index,
        const std::function<double()> &rng_uniform_0_1)
    {
        using std::chrono::milliseconds;

        const auto base_ms = policy.base.count();
        const auto cap_ms = policy.cap.count();
        if (base_ms <= 0 || cap_ms <= 0)
        {
            return milliseconds{0};
        }

        // Compute base * 2^attempt_index without overflow. Once the shift
        // would exceed the cap, just clamp.
        std::int64_t delay = base_ms;
        const unsigned safe_shift = std::min<unsigned>(attempt_index, 30u);
        for (unsigned i = 0; i < safe_shift; ++i)
        {
            if (delay >= cap_ms)
            {
                delay = cap_ms;
                break;
            }
            delay *= 2;
        }
        if (delay > cap_ms)
            delay = cap_ms;

        // Apply symmetric jitter.
        double jitter = std::clamp(policy.jitter_ratio, 0.0, 1.0);
        if (jitter > 0.0 && rng_uniform_0_1)
        {
            const double r = std::clamp(rng_uniform_0_1(), 0.0, 1.0);
            const double scale = 1.0 + jitter * (2.0 * r - 1.0);
            const double scaled = static_cast<double>(delay) * scale;
            if (scaled <= 0.0)
                delay = 0;
            else
                delay = static_cast<std::int64_t>(scaled);
        }

        if (delay < 0)
            delay = 0;
        return milliseconds{delay};
    }

    bool is_retriable(int status_code, bool network_error) noexcept
    {
        if (network_error)
            return true;
        if (status_code >= 500 && status_code < 600)
            return true;
        if (status_code == 408 || status_code == 429)
            return true;
        return false;
    }

} // namespace squelch
