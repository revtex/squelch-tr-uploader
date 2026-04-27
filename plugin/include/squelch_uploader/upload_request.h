// SPDX-License-Identifier: GPL-3.0-or-later
//
// upload_request.h — TR `Call_Data_t` → Squelch wire fields.
//
// `UploadRequest` is the strongly-typed projection of TR's per-call metadata
// onto the Squelch native upload contract documented at
// /memories/repo/wire-contract.md. Pure conversion functions live here so the
// mapping is unit-testable without TR's headers.
//
// `CallData` is a TR-free POD that carries exactly the fields we project
// (`squelch_uploader.cc` adapts a real `Call_Data_t` into one before calling
// `from_call_data`). The split keeps the test binary off TR's include tree.

#pragma once

#include "squelch_uploader/http_client.h"

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace squelch
{

    // Mirrors TR's `Call_Source` (per-transmission unit-id record).
    struct CallSourceLite
    {
        long source = 0;
        long time = 0;
        double position = 0.0;
        double length = 0.0;
        bool emergency = false;
        std::string signal_system;
        std::string tag;
    };

    // Mirrors TR's `Call_Error` (per-transmission frequency / quality record).
    struct CallFreqLite
    {
        double freq = 0.0;
        long time = 0;
        double position = 0.0;
        double total_len = 0.0;
        long error_count = 0;
        long spike_count = 0;
    };

    // TR-free projection of the `Call_Data_t` fields Squelch consumes.
    struct CallData
    {
        long talkgroup = 0;
        long start_time = 0; // epoch seconds (from TR's `start_time`)
        double freq = 0.0;
        double length = 0.0; // seconds
        long error_count = 0;
        long spike_count = 0;

        std::string talkgroup_tag;
        std::string talkgroup_alpha_tag;
        std::string talkgroup_description;
        std::string talkgroup_group;

        // Audio file path on disk after `call_concluder` finishes.
        std::string audio_path;

        std::string short_name; // becomes `systemLabel`

        std::vector<CallSourceLite> transmission_source_list;
        std::vector<CallFreqLite> transmission_error_list;
        std::vector<unsigned long> patched_talkgroups;
    };

    // Projection of a call onto Squelch's native upload form. Optional integer
    // fields are stored as `std::optional<int64_t>` so we can omit them
    // entirely when unset (the wire contract says don't send "0").
    struct UploadRequest
    {
        // required
        std::string started_at;   // RFC 3339 UTC `Z`
        std::string system_id;    // integer-as-string
        std::string talkgroup_id; // integer-as-string
        std::string audio_path;
        std::string audio_content_type;

        // optional integers (omit when nullopt)
        std::optional<std::int64_t> frequency_hz;
        std::optional<std::int64_t> duration_ms;
        std::optional<std::int64_t> unit_id;
        std::optional<std::int64_t> error_count;
        std::optional<std::int64_t> spike_count;

        // optional JSON-string fields
        std::string sources;     // JSON array
        std::string frequencies; // JSON array
        std::string patches;     // JSON array

        // optional plain strings
        std::string talkgroup_label;
        std::string talkgroup_tag;
        std::string talkgroup_group;
        std::string talkgroup_name;
        std::string talker_alias;
        std::string system_label;
        std::string site;
        std::string channel;
        std::string decoder;

        // Build an UploadRequest from TR-projected call data plus the
        // configured Squelch system id.
        static UploadRequest from_call_data(const CallData &call,
                                            long system_id);

        // Populate `m` with this request's fields. Optional integers are
        // skipped when unset; optional strings are skipped when empty.
        void to_multipart(Multipart &m) const;
    };

    // Format `epoch_s` as an RFC 3339 timestamp in UTC: `YYYY-MM-DDTHH:MM:SSZ`.
    std::string to_rfc3339_utc(std::time_t epoch_s);

    // Pre-flight: returns true iff `bytes` is within Squelch's 50 MiB ceiling.
    constexpr bool is_audio_size_ok(std::uintmax_t bytes) noexcept
    {
        return bytes <= kMaxUploadBytes;
    }

    // Stat-based pre-flight. Returns:
    //   * true with `*size_out` populated if the file exists and is in range
    //   * false with `*error` set otherwise (missing, unreadable, too large)
    bool check_audio_file(const std::string &path,
                          std::uintmax_t *size_out,
                          std::string *error);

} // namespace squelch
