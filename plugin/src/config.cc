// SPDX-License-Identifier: GPL-3.0-or-later

#include "squelch_uploader/config.h"

#include <cstdint>
#include <string>

namespace squelch
{

    namespace
    {

        bool starts_with(const std::string &s, const char *prefix)
        {
            const std::size_t n = std::char_traits<char>::length(prefix);
            return s.size() >= n && s.compare(0, n, prefix) == 0;
        }

        template <typename T>
        bool get_optional(const nlohmann::json &data, const char *key, T &out)
        {
            auto it = data.find(key);
            if (it == data.end() || it->is_null())
            {
                return false;
            }
            out = it->get<T>();
            return true;
        }

    } // namespace

    std::optional<Config> Config::parse(const nlohmann::json &data,
                                        std::string *error)
    {
        if (!data.is_object())
        {
            if (error)
                *error = "config must be a JSON object";
            return std::nullopt;
        }

        Config cfg;

        // server — required, must be a non-empty http(s) URL.
        {
            auto it = data.find("server");
            if (it == data.end() || !it->is_string())
            {
                if (error)
                    *error = "missing required string field: server";
                return std::nullopt;
            }
            cfg.server = it->get<std::string>();
            if (cfg.server.empty())
            {
                if (error)
                    *error = "server must not be empty";
                return std::nullopt;
            }
            if (!starts_with(cfg.server, "https://") &&
                !starts_with(cfg.server, "http://"))
            {
                if (error)
                {
                    *error = "server must start with https:// or http:// (got '" +
                             cfg.server + "')";
                }
                return std::nullopt;
            }
        }

        // apiKey — required, non-empty.
        {
            auto it = data.find("apiKey");
            if (it == data.end() || !it->is_string())
            {
                if (error)
                    *error = "missing required string field: apiKey";
                return std::nullopt;
            }
            cfg.api_key = it->get<std::string>();
            if (cfg.api_key.empty())
            {
                if (error)
                    *error = "apiKey must not be empty";
                return std::nullopt;
            }
        }

        // shortName — optional.
        get_optional<std::string>(data, "shortName", cfg.short_name);

        // systemId — optional.
        {
            auto it = data.find("systemId");
            if (it != data.end() && !it->is_null())
            {
                if (!it->is_number_integer())
                {
                    if (error)
                        *error = "systemId must be an integer";
                    return std::nullopt;
                }
                cfg.system_id = it->get<long>();
            }
        }

        // unitTagsFile — optional.
        get_optional<std::string>(data, "unitTagsFile", cfg.unit_tags_file);

        // Retry tuning. The only knob still exposed in v1; defaults plus a
        // hard upper bound to keep stuck calls from hammering Squelch.
        auto parse_bounded_uint = [&](const char *key, std::uint64_t lo,
                                      std::uint64_t hi, std::uint64_t &out_u64,
                                      bool &present) -> bool
        {
            present = false;
            auto it = data.find(key);
            if (it == data.end() || it->is_null())
                return true;
            if (!it->is_number_integer() && !it->is_number_unsigned())
            {
                if (error)
                    *error = std::string(key) + " must be an integer";
                return false;
            }
            const auto raw = it->get<std::int64_t>();
            if (raw < 0 || static_cast<std::uint64_t>(raw) < lo ||
                static_cast<std::uint64_t>(raw) > hi)
            {
                if (error)
                {
                    *error = std::string(key) + " must be between " +
                             std::to_string(lo) + " and " +
                             std::to_string(hi) + " (got " +
                             std::to_string(raw) + ")";
                }
                return false;
            }
            out_u64 = static_cast<std::uint64_t>(raw);
            present = true;
            return true;
        };

        {
            std::uint64_t v = 0;
            bool present = false;
            if (!parse_bounded_uint("maxRetries", 0, 10, v, present))
                return std::nullopt;
            if (present)
                cfg.max_retries = static_cast<unsigned>(v);
        }

        return cfg;
    }

} // namespace squelch
