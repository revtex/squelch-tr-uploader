// SPDX-License-Identifier: GPL-3.0-or-later
//
// upload_request.cc — projects TR's `Call_Data_t` onto the Squelch native
// upload contract. JSON for the array fields (`sources`, `frequencies`,
// `patches`) is hand-rolled; the shapes are fixed and small, and not pulling
// in nlohmann here keeps this TU compilable for the test binary without
// touching TR's vendored include tree.

#include "squelch_uploader/upload_request.h"

#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>

namespace squelch
{

    namespace
    {

        // RFC 8259 §7 string escaping. `/` is left unescaped (legal either way).
        void json_escape_into(const std::string &s, std::ostringstream &out)
        {
            out.put('"');
            for (char ch : s)
            {
                const auto c = static_cast<unsigned char>(ch);
                switch (c)
                {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                case '\b':
                    out << "\\b";
                    break;
                case '\f':
                    out << "\\f";
                    break;
                case '\n':
                    out << "\\n";
                    break;
                case '\r':
                    out << "\\r";
                    break;
                case '\t':
                    out << "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out << buf;
                    }
                    else
                    {
                        out.put(ch);
                    }
                    break;
                }
            }
            out.put('"');
        }

        // Format a double with 2 decimals, matching the precision rdioscanner
        // emits for `pos` / `len` fields. The receiver round-trips it as
        // float-or-string so this is just for human readability.
        std::string fmt_fixed(double v, int precision)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
            return std::string(buf);
        }

        // Build the `sources` JSON array.
        std::string build_sources_json(const std::vector<CallSourceLite> &items)
        {
            if (items.empty())
                return "[]";
            std::ostringstream out;
            out << '[';
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto &s = items[i];
                if (i != 0)
                    out << ',';
                out << "{\"src\":" << s.source
                    << ",\"time\":" << s.time
                    << ",\"pos\":" << fmt_fixed(s.position, 2)
                    << ",\"len\":" << fmt_fixed(s.length, 2)
                    << ",\"emergency\":" << (s.emergency ? "true" : "false")
                    << ",\"signal_system\":";
                json_escape_into(s.signal_system, out);
                out << ",\"tag\":";
                json_escape_into(s.tag, out);
                out << '}';
            }
            out << ']';
            return out.str();
        }

        // Build the `frequencies` JSON array from per-transmission error records.
        std::string build_frequencies_json(const std::vector<CallFreqLite> &items)
        {
            if (items.empty())
                return "[]";
            std::ostringstream out;
            out << '[';
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto &f = items[i];
                if (i != 0)
                    out << ',';
                // freq is rounded to integer Hz to match the wire contract;
                // pos/len keep two decimals.
                const std::int64_t freq_hz =
                    static_cast<std::int64_t>(std::llround(f.freq));
                out << "{\"freq\":" << freq_hz
                    << ",\"time\":" << f.time
                    << ",\"pos\":" << fmt_fixed(f.position, 2)
                    << ",\"len\":" << fmt_fixed(f.total_len, 2)
                    << ",\"errorCount\":" << f.error_count
                    << ",\"spikeCount\":" << f.spike_count
                    << '}';
            }
            out << ']';
            return out.str();
        }

        // Build the `patches` JSON array. The TR convention is that a single-
        // entry list (the talkgroup itself) means "not patched"; only emit the
        // array when there's more than one.
        std::string build_patches_json(const std::vector<unsigned long> &items)
        {
            if (items.size() <= 1)
                return std::string{};
            std::ostringstream out;
            out << '[';
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (i != 0)
                    out << ',';
                out << items[i];
            }
            out << ']';
            return out.str();
        }

        // Walk transmission_source_list for the first non-zero source id.
        std::optional<std::int64_t>
        first_unit_id(const std::vector<CallSourceLite> &sources)
        {
            for (const auto &s : sources)
            {
                if (s.source > 0)
                    return static_cast<std::int64_t>(s.source);
            }
            return std::nullopt;
        }

        // Look up the talker-alias tag attached to the source matching
        // `unit_id`. TR resolves unit_tags / OTA aliases into Call_Source.tag
        // before call_end fires, so we don't keep our own cache.
        std::string
        alias_for_unit(const std::vector<CallSourceLite> &sources,
                       std::optional<std::int64_t> unit_id)
        {
            if (!unit_id)
                return {};
            for (const auto &s : sources)
            {
                if (static_cast<std::int64_t>(s.source) == *unit_id &&
                    !s.tag.empty())
                    return s.tag;
            }
            return {};
        }

    } // namespace

    std::string to_rfc3339_utc(std::time_t epoch_s)
    {
        std::tm tm_utc{};
        // gmtime_r is the POSIX threadsafe form. We only target Linux/macOS.
        if (gmtime_r(&epoch_s, &tm_utc) == nullptr)
        {
            return std::string{};
        }
        char buf[32];
        // %F = %Y-%m-%d, %T = %H:%M:%S, then literal Z.
        const std::size_t n =
            std::strftime(buf, sizeof(buf), "%FT%TZ", &tm_utc);
        if (n == 0)
            return std::string{};
        return std::string(buf, n);
    }

    bool check_audio_file(const std::string &path,
                          std::uintmax_t *size_out,
                          std::string *error)
    {
        if (path.empty())
        {
            if (error)
                *error = "audio path is empty";
            return false;
        }
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0)
        {
            if (error)
                *error = "audio file not found: " + path;
            return false;
        }
        if (!S_ISREG(st.st_mode))
        {
            if (error)
                *error = "audio path is not a regular file: " + path;
            return false;
        }
        const auto size = static_cast<std::uintmax_t>(st.st_size);
        if (!is_audio_size_ok(size))
        {
            if (error)
            {
                *error = "audio file exceeds 50 MiB limit (" +
                         std::to_string(size) + " bytes): " + path;
            }
            return false;
        }
        if (size_out)
            *size_out = size;
        return true;
    }

    UploadRequest UploadRequest::from_call_data(const CallData &call,
                                                long system_id)
    {
        UploadRequest req;

        req.started_at = to_rfc3339_utc(call.start_time);
        req.system_id = std::to_string(system_id);
        req.talkgroup_id = std::to_string(call.talkgroup);
        req.audio_path = call.audio_path;
        req.audio_content_type = audio_content_type_for(call.audio_path);

        if (call.freq > 0.0)
        {
            req.frequency_hz =
                static_cast<std::int64_t>(std::llround(call.freq));
        }

        if (call.length > 0.0)
        {
            req.duration_ms =
                static_cast<std::int64_t>(std::llround(call.length * 1000.0));
        }

        req.unit_id = first_unit_id(call.transmission_source_list);
        req.talker_alias =
            alias_for_unit(call.transmission_source_list, req.unit_id);

        if (call.error_count > 0)
            req.error_count = static_cast<std::int64_t>(call.error_count);
        if (call.spike_count > 0)
            req.spike_count = static_cast<std::int64_t>(call.spike_count);

        req.sources = build_sources_json(call.transmission_source_list);
        req.frequencies = build_frequencies_json(call.transmission_error_list);
        req.patches = build_patches_json(call.patched_talkgroups);

        req.talkgroup_label = call.talkgroup_alpha_tag;
        req.talkgroup_tag = call.talkgroup_tag;
        req.talkgroup_group = call.talkgroup_group;
        req.talkgroup_name = call.talkgroup_description;
        req.system_label = call.short_name;

        return req;
    }

    void UploadRequest::to_multipart(Multipart &m) const
    {
        // Required fields. `audio` is the file part; everything else is text.
        m.add_string("startedAt", started_at);
        m.add_string("systemId", system_id);
        m.add_string("talkgroupId", talkgroup_id);

        // Optional integers — emit only when set.
        if (frequency_hz)
            m.add_string("frequencyHz", std::to_string(*frequency_hz));
        if (duration_ms)
            m.add_string("durationMs", std::to_string(*duration_ms));
        if (unit_id)
            m.add_string("unitId", std::to_string(*unit_id));
        if (error_count)
            m.add_string("errorCount", std::to_string(*error_count));
        if (spike_count)
            m.add_string("spikeCount", std::to_string(*spike_count));

        // Optional JSON-string fields. We always emit `sources`/`frequencies`
        // (even if "[]") so operators can audit empty-array cases; `patches`
        // is omitted entirely when there are no patched talkgroups (the
        // build_patches_json helper returns "" in that case).
        if (!sources.empty())
            m.add_string("sources", sources);
        if (!frequencies.empty())
            m.add_string("frequencies", frequencies);
        if (!patches.empty())
            m.add_string("patches", patches);

        // Optional plain strings — skip when empty.
        if (!talkgroup_label.empty())
            m.add_string("talkgroupLabel", talkgroup_label);
        if (!talkgroup_tag.empty())
            m.add_string("talkgroupTag", talkgroup_tag);
        if (!talkgroup_group.empty())
            m.add_string("talkgroupGroup", talkgroup_group);
        if (!talkgroup_name.empty())
            m.add_string("talkgroupName", talkgroup_name);
        if (!talker_alias.empty())
            m.add_string("talkerAlias", talker_alias);
        if (!system_label.empty())
            m.add_string("systemLabel", system_label);
        if (!site.empty())
            m.add_string("site", site);
        if (!channel.empty())
            m.add_string("channel", channel);
        if (!decoder.empty())
            m.add_string("decoder", decoder);

        // File part.
        m.add_file("audio", audio_path, audio_content_type);
    }

} // namespace squelch
