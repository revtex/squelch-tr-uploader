// SPDX-License-Identifier: GPL-3.0-or-later
//
// http_client_util.cc — pure helpers for the upload HTTP path. No libcurl
// dependency lives in this TU so the test binary can link it directly.

#include "squelch_uploader/http_client.h"

#include <algorithm>
#include <cctype>

namespace squelch
{

    namespace
    {

        std::string to_lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });
            return s;
        }

    } // namespace

    void Multipart::add_string(std::string name, std::string value)
    {
        strings_.push_back({std::move(name), std::move(value)});
    }

    void Multipart::add_file(std::string name,
                             std::string path,
                             std::string content_type)
    {
        files_.push_back({std::move(name), std::move(path), std::move(content_type)});
    }

    std::vector<std::string> build_headers(const std::string &bearer_token)
    {
        // Order matters only for human readability; libcurl preserves it.
        // We MUST emit exactly one Authorization header and never X-API-Key.
        return {
            "Authorization: Bearer " + bearer_token,
            // Disable libcurl's automatic Expect: 100-continue. Some proxies
            // mishandle it and we never need the round-trip on a 50 MiB body.
            "Expect:",
        };
    }

    std::string build_upload_url(const std::string &server)
    {
        std::string base = server;
        while (!base.empty() && base.back() == '/')
        {
            base.pop_back();
        }
        return base + "/api/v1/calls";
    }

    std::string audio_content_type_for(const std::string &path)
    {
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= path.size())
        {
            return "application/octet-stream";
        }
        const std::string ext = to_lower(path.substr(dot + 1));
        if (ext == "wav")
            return "audio/wav";
        if (ext == "mp3")
            return "audio/mpeg";
        if (ext == "aac")
            return "audio/aac";
        if (ext == "m4a")
            return "audio/m4a";
        if (ext == "ogg")
            return "audio/ogg";
        if (ext == "opus")
            return "audio/opus";
        return "application/octet-stream";
    }

} // namespace squelch
