// SPDX-License-Identifier: GPL-3.0-or-later
//
// http_client.h — libcurl wrapper for multipart uploads to Squelch.
//
// `HttpClient` is the production implementation, RAII over libcurl. It is
// thread-unsafe; instantiate one per worker thread.
//
// `Multipart` is a curl-free builder that records form fields. The plugin
// builds a `Multipart` from an `UploadRequest` and hands it to
// `HttpClient::upload`. Tests can examine the `Multipart` directly without
// linking libcurl.
//
// The pure helper functions (`build_headers`, `build_upload_url`,
// `audio_content_type_for`) live in `http_client_util.cc` so the test binary
// can link them without dragging libcurl in.

#pragma once

#include <string>
#include <vector>

namespace squelch
{

    struct HttpResult
    {
        long status_code = 0;      // 0 if the request never reached the server
        std::string body;          // response body (small; we don't stream)
        std::string error_message; // non-empty iff transport-level failure
    };

    class Multipart
    {
    public:
        struct StringField
        {
            std::string name;
            std::string value;
        };
        struct FileField
        {
            std::string name;
            std::string path;
            std::string content_type;
        };

        void add_string(std::string name, std::string value);
        void add_file(std::string name, std::string path, std::string content_type);

        const std::vector<StringField> &strings() const noexcept { return strings_; }
        const std::vector<FileField> &files() const noexcept { return files_; }

    private:
        std::vector<StringField> strings_;
        std::vector<FileField> files_;
    };

    // Construct the headers list for a Squelch upload. Always exactly one
    // `Authorization: Bearer <token>` plus an `Expect:` reset to disable
    // libcurl's 100-continue behaviour. NEVER returns the legacy `X-API-Key`
    // header — the project's wire contract forbids it.
    std::vector<std::string> build_headers(const std::string &bearer_token);

    // Build the upload URL by appending `/api/v1/calls` to `server`,
    // trimming exactly one trailing slash so we never produce a double-slash.
    std::string build_upload_url(const std::string &server);

    // Map an audio-file extension (case-insensitive) to its MIME type per the
    // Squelch wire contract. Unknown extensions return "application/octet-stream".
    std::string audio_content_type_for(const std::string &path);

    // Shared by HttpClient and tests: total upload-size ceiling enforced
    // before opening a connection. Squelch v1 rejects anything larger.
    constexpr std::size_t kMaxUploadBytes = 50ULL * 1024ULL * 1024ULL;

    class HttpClient
    {
    public:
        HttpClient();
        virtual ~HttpClient();

        HttpClient(const HttpClient &) = delete;
        HttpClient &operator=(const HttpClient &) = delete;

        // Upload `body` as multipart/form-data via POST to `url` with a Bearer
        // token. Synchronous. Returns transport / HTTP error in `HttpResult`.
        // Virtual so tests can substitute a mock without linking libcurl.
        virtual HttpResult upload(const std::string &url,
                                  const std::string &bearer_token,
                                  const Multipart &body);
    };

} // namespace squelch
