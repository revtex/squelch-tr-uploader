// SPDX-License-Identifier: GPL-3.0-or-later
//
// uploader.h — single-thread Squelch upload worker. One background
// thread, an unbounded in-process FIFO, libcurl multipart POST,
// exponential-backoff retries on transient failures.

#pragma once

#include "squelch_uploader/upload_request.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward-declare libcurl's CURL type so this header doesn't pull in
// <curl/curl.h>; the Uploader implementation owns the handle.
typedef void CURL;

namespace squelch
{

    // Plain-data multipart form builder. The wire-mapping code in
    // upload_request.cc populates one of these; uploader.cc serializes it
    // into a libcurl mime structure.
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
        void add_file(std::string name, std::string path,
                      std::string content_type);

        const std::vector<StringField> &strings() const noexcept { return strings_; }
        const std::vector<FileField> &files() const noexcept { return files_; }

    private:
        std::vector<StringField> strings_;
        std::vector<FileField> files_;
    };

    // Build the Squelch upload URL by appending `/api/v1/calls` to `server`,
    // trimming a single trailing slash so we never produce a double-slash.
    std::string build_upload_url(const std::string &server);

    // Map an audio-file extension (case-insensitive) to its MIME type per the
    // Squelch wire contract. Unknown extensions return "application/octet-stream".
    std::string audio_content_type_for(const std::string &path);

    struct UploadJob
    {
        UploadRequest request;
        // Human-friendly identifier used in log lines (e.g. "tg=1234
        // started=2024-04-27T06:40:00Z"). Never contains the API key.
        std::string debug_tag;
    };

    // Single-threaded uploader. start() spawns one background thread that
    // drains an unbounded queue. enqueue() never blocks and never drops.
    // stop() blocks until the queue is fully drained and the thread joined.
    class Uploader
    {
    public:
        Uploader(std::string url, std::string bearer_token, unsigned max_retries);
        ~Uploader();

        Uploader(const Uploader &) = delete;
        Uploader &operator=(const Uploader &) = delete;

        // Spawn the background thread. Idempotent.
        void start();

        // Wake the worker, finish the current upload (with retries) and
        // drain everything still queued, then join the thread.
        void stop();

        // Push a job onto the queue. Always succeeds.
        void enqueue(UploadJob job);

    private:
        void worker_loop();
        bool perform_upload(const UploadJob &job, std::string *error_out,
                            long *status_out);

        const std::string url_;
        const std::string bearer_token_;
        const unsigned max_retries_;

        CURL *curl_ = nullptr;

        mutable std::mutex mu_;
        std::condition_variable cv_;
        std::deque<UploadJob> queue_;
        bool started_ = false;
        bool stopping_ = false;

        std::thread worker_;
    };

} // namespace squelch
