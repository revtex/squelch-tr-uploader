// SPDX-License-Identifier: GPL-3.0-or-later
//
// config.h — Parsed plugin configuration block.
//
// The Squelch uploader's TR config sub-object looks roughly like:
//
//   {
//     "name":        "squelch_uploader",
//     "library":     "/usr/lib/trunk-recorder/plugins/squelch_uploader.so",
//     "server":      "https://squelch.example.com",
//     "apiKey":      "sk_live_…",
//     "shortName":   "local",
//     "systemId":    1234,
//     "unitTagsFile": "/etc/trunk-recorder/units.csv"
//   }
//
// `Config::parse` validates the required fields and returns either a populated
// `Config` or an error message via the out-parameter. Exceptions never cross
// the parse boundary: TR's plugin SPI uses int return codes.

#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace squelch
{

    struct Config
    {
        std::string server;            // required, must be http(s) URL
        std::string api_key;           // required, non-empty (from JSON "apiKey")
        std::string short_name;        // optional (from JSON "shortName")
        std::optional<long> system_id; // optional (from JSON "systemId")
        std::string unit_tags_file;    // optional (from JSON "unitTagsFile")

        // Maximum retry attempts on transient failure (HTTP 408, 429, 5xx,
        // network errors). 0 disables retry; bounds 0..10. From JSON
        // `maxRetries`.
        unsigned max_retries = 3;

        // Parses `data` into a Config. Returns std::nullopt and writes a
        // human-readable reason into *error on validation failure. *error is
        // untouched on success.
        static std::optional<Config> parse(const nlohmann::json &data,
                                           std::string *error);
    };

} // namespace squelch
