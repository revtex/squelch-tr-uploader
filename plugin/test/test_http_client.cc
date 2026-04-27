// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_http_client.cc — exercises the curl-free helpers (build_headers,
// build_upload_url). The real `HttpClient::upload` is excluded from this
// test binary (no libcurl link); a real-network smoke test is gated behind
// the `SQUELCH_TEST_HTTP_TARGET` env var and lives in the integration suite.

#include "squelch_uploader/http_client.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using squelch::build_headers;
using squelch::build_upload_url;

namespace
{

    bool any_starts_with(const std::vector<std::string> &v,
                         const std::string &prefix)
    {
        return std::any_of(v.begin(), v.end(), [&](const std::string &s)
                           { return s.size() >= prefix.size() &&
                                    s.compare(0, prefix.size(), prefix) == 0; });
    }

    std::size_t count_starts_with(const std::vector<std::string> &v,
                                  const std::string &prefix)
    {
        return static_cast<std::size_t>(std::count_if(
            v.begin(), v.end(), [&](const std::string &s)
            { return s.size() >= prefix.size() &&
                     s.compare(0, prefix.size(), prefix) == 0; }));
    }

} // namespace

TEST(BuildHeaders, IncludesBearerOnce)
{
    const auto hdrs = build_headers("sk_live_abcdef");
    EXPECT_EQ(count_starts_with(hdrs, "Authorization:"), 1u);
    EXPECT_TRUE(any_starts_with(hdrs, "Authorization: Bearer sk_live_abcdef"));
}

TEST(BuildHeaders, NeverEmitsLegacyApiKeyHeader)
{
    const auto hdrs = build_headers("any-key-value");
    EXPECT_FALSE(any_starts_with(hdrs, "X-API-Key"));
    EXPECT_FALSE(any_starts_with(hdrs, "x-api-key"));
}

TEST(BuildHeaders, ResetsExpectHeader)
{
    // We need libcurl to NOT auto-emit Expect: 100-continue for a 50 MiB
    // body, so the header-list must contain a literal "Expect:" reset.
    const auto hdrs = build_headers("k");
    EXPECT_TRUE(any_starts_with(hdrs, "Expect:"));
}

TEST(BuildUploadUrl, AppendsApiPath)
{
    EXPECT_EQ(build_upload_url("https://squelch.example.com"),
              "https://squelch.example.com/api/v1/calls");
}

TEST(BuildUploadUrl, StripsTrailingSlash)
{
    EXPECT_EQ(build_upload_url("https://squelch.example.com/"),
              "https://squelch.example.com/api/v1/calls");
}

TEST(BuildUploadUrl, StripsMultipleTrailingSlashes)
{
    EXPECT_EQ(build_upload_url("https://squelch.example.com///"),
              "https://squelch.example.com/api/v1/calls");
}

TEST(BuildUploadUrl, PreservesSubpath)
{
    EXPECT_EQ(build_upload_url("https://squelch.example.com/proxy"),
              "https://squelch.example.com/proxy/api/v1/calls");
}

TEST(MultipartBuilder, RecordsStringsAndFiles)
{
    squelch::Multipart m;
    m.add_string("a", "1");
    m.add_string("b", "two");
    m.add_file("audio", "/tmp/x.wav", "audio/wav");

    ASSERT_EQ(m.strings().size(), 2u);
    EXPECT_EQ(m.strings()[0].name, "a");
    EXPECT_EQ(m.strings()[0].value, "1");
    EXPECT_EQ(m.strings()[1].name, "b");
    EXPECT_EQ(m.strings()[1].value, "two");
    ASSERT_EQ(m.files().size(), 1u);
    EXPECT_EQ(m.files()[0].name, "audio");
    EXPECT_EQ(m.files()[0].path, "/tmp/x.wav");
    EXPECT_EQ(m.files()[0].content_type, "audio/wav");
}
