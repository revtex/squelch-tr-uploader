// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_upload_request.cc — covers the TR→Squelch field mapping, RFC 3339
// formatting, the 50 MiB pre-flight, and the multipart projection.

#include "squelch_uploader/upload_request.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using squelch::CallData;
using squelch::CallFreqLite;
using squelch::CallSourceLite;
using squelch::Multipart;
using squelch::UploadRequest;

namespace
{

    // Build a CallData with sane realistic values for happy-path tests.
    CallData make_sample_call(const std::string &audio_path)
    {
        CallData c;
        c.talkgroup = 1234;
        c.start_time = 1714200000; // 2024-04-27T06:40:00Z
        c.freq = 851012500.0;
        c.length = 8.5;
        c.error_count = 2;
        c.spike_count = 1;
        c.talkgroup_tag = "Dispatch";
        c.talkgroup_alpha_tag = "PD Disp 1";
        c.talkgroup_description = "Police Dispatch 1";
        c.talkgroup_group = "Public Safety";
        c.audio_path = audio_path;
        c.short_name = "Metro";
        c.transmission_source_list = {
            CallSourceLite{0, 1714200001, 0.00, 1.0, false, "P25", ""},
            CallSourceLite{
                1234567, 1714200002, 1.10, 2.5, false, "P25", "Unit 7"},
            CallSourceLite{7654321, 1714200005, 3.60, 4.9, true, "P25", ""}};
        c.transmission_error_list = {
            CallFreqLite{851012500.0, 1714200001, 0.0, 8.5, 2, 1}};
        c.patched_talkgroups = {1234, 5678};
        return c;
    }

    // Find a string field in a Multipart by name. Returns nullptr when absent.
    const Multipart::StringField *find_string(const Multipart &m,
                                              const std::string &name)
    {
        for (const auto &f : m.strings())
        {
            if (f.name == name)
                return &f;
        }
        return nullptr;
    }

    bool has_string(const Multipart &m, const std::string &name)
    {
        return find_string(m, name) != nullptr;
    }

} // namespace

// ---- to_rfc3339_utc -------------------------------------------------------

TEST(Rfc3339, EpochZero)
{
    EXPECT_EQ(squelch::to_rfc3339_utc(0), "1970-01-01T00:00:00Z");
}

TEST(Rfc3339, KnownTimestamp)
{
    // 1714200000 = 2024-04-27T06:40:00Z (date -u -d @1714200000).
    EXPECT_EQ(squelch::to_rfc3339_utc(1714200000), "2024-04-27T06:40:00Z");
}

// ---- from_call_data -------------------------------------------------------

TEST(UploadRequestMapping, RequiredFields)
{
    const auto call = make_sample_call("/tmp/squelch-test-fixture.wav");
    const auto req = UploadRequest::from_call_data(call, 42);

    EXPECT_EQ(req.system_id, "42");
    EXPECT_EQ(req.talkgroup_id, "1234");
    EXPECT_EQ(req.started_at, "2024-04-27T06:40:00Z");
    EXPECT_EQ(req.audio_path, "/tmp/squelch-test-fixture.wav");
    EXPECT_EQ(req.audio_content_type, "audio/wav");
}

TEST(UploadRequestMapping, OptionalIntegers)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 1);

    ASSERT_TRUE(req.frequency_hz.has_value());
    EXPECT_EQ(*req.frequency_hz, 851012500);

    ASSERT_TRUE(req.duration_ms.has_value());
    EXPECT_EQ(*req.duration_ms, 8500);

    ASSERT_TRUE(req.unit_id.has_value());
    EXPECT_EQ(*req.unit_id, 1234567); // first non-zero source

    ASSERT_TRUE(req.error_count.has_value());
    EXPECT_EQ(*req.error_count, 2);
    ASSERT_TRUE(req.spike_count.has_value());
    EXPECT_EQ(*req.spike_count, 1);
}

TEST(UploadRequestMapping, OptionalIntegersOmittedWhenZero)
{
    CallData c;
    c.talkgroup = 1;
    c.start_time = 1714200000;
    c.freq = 0.0;   // omit frequencyHz
    c.length = 0.0; // omit durationMs
    c.audio_path = "/tmp/x.mp3";

    const auto req = UploadRequest::from_call_data(c, 1);
    EXPECT_FALSE(req.frequency_hz.has_value());
    EXPECT_FALSE(req.duration_ms.has_value());
    EXPECT_FALSE(req.unit_id.has_value());
    EXPECT_FALSE(req.error_count.has_value());
    EXPECT_FALSE(req.spike_count.has_value());
    EXPECT_EQ(req.audio_content_type, "audio/mpeg");
}

TEST(UploadRequestMapping, UnitIdSkipsZeroEntries)
{
    CallData c;
    c.start_time = 0;
    c.audio_path = "/tmp/x.wav";
    c.transmission_source_list = {
        CallSourceLite{0, 0, 0, 0, false, "", ""}, // no valid src
        CallSourceLite{0, 0, 0, 0, false, "", ""},
        CallSourceLite{99, 0, 0, 0, false, "", ""},
    };
    const auto req = UploadRequest::from_call_data(c, 1);
    ASSERT_TRUE(req.unit_id.has_value());
    EXPECT_EQ(*req.unit_id, 99);
}

TEST(UploadRequestMapping, UnitIdAbsentWhenAllZero)
{
    CallData c;
    c.audio_path = "/tmp/x.wav";
    c.transmission_source_list = {
        CallSourceLite{0, 0, 0, 0, false, "", ""}};
    const auto req = UploadRequest::from_call_data(c, 1);
    EXPECT_FALSE(req.unit_id.has_value());
}

TEST(UploadRequestMapping, TalkgroupStrings)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 1);

    EXPECT_EQ(req.talkgroup_label, "PD Disp 1");
    EXPECT_EQ(req.talkgroup_tag, "Dispatch");
    EXPECT_EQ(req.talkgroup_group, "Public Safety");
    EXPECT_EQ(req.talkgroup_name, "Police Dispatch 1");
    EXPECT_EQ(req.system_label, "Metro");
}

// ---- JSON-string fields ---------------------------------------------------

TEST(UploadRequestJson, SourcesShape)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 1);

    // 3 entries, in order. Spot-check the shape rather than full string match.
    EXPECT_NE(req.sources.find("\"src\":1234567"), std::string::npos);
    EXPECT_NE(req.sources.find("\"src\":7654321"), std::string::npos);
    EXPECT_NE(req.sources.find("\"emergency\":true"), std::string::npos);
    EXPECT_NE(req.sources.find("\"signal_system\":\"P25\""),
              std::string::npos);
    EXPECT_NE(req.sources.find("\"tag\":\"Unit 7\""), std::string::npos);
    EXPECT_EQ(req.sources.front(), '[');
    EXPECT_EQ(req.sources.back(), ']');
}

TEST(UploadRequestJson, SourcesEmptyIsEmptyArray)
{
    CallData c;
    c.audio_path = "/tmp/x.wav";
    const auto req = UploadRequest::from_call_data(c, 1);
    EXPECT_EQ(req.sources, "[]");
    EXPECT_EQ(req.frequencies, "[]");
}

TEST(UploadRequestJson, FrequenciesRoundsHzToInt)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 1);
    EXPECT_NE(req.frequencies.find("\"freq\":851012500"), std::string::npos);
    EXPECT_NE(req.frequencies.find("\"errorCount\":2"), std::string::npos);
}

TEST(UploadRequestJson, PatchesEmittedWhenMultiple)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 1);
    EXPECT_EQ(req.patches, "[1234,5678]");
}

TEST(UploadRequestJson, PatchesOmittedForSingleEntry)
{
    CallData c;
    c.audio_path = "/tmp/x.wav";
    c.patched_talkgroups = {1234};
    const auto req = UploadRequest::from_call_data(c, 1);
    EXPECT_TRUE(req.patches.empty());
}

TEST(UploadRequestJson, JsonStringEscaping)
{
    CallData c;
    c.audio_path = "/tmp/x.wav";
    c.transmission_source_list = {
        CallSourceLite{1, 0, 0, 0, false, "P25", "He said \"hi\"\n"}};
    const auto req = UploadRequest::from_call_data(c, 1);
    // Backslash escapes for quote and newline.
    EXPECT_NE(req.sources.find("He said \\\"hi\\\"\\n"), std::string::npos);
}

// ---- to_multipart ---------------------------------------------------------

TEST(UploadRequestMultipart, RequiredAndOptionalFields)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 42);

    Multipart m;
    req.to_multipart(m);

    EXPECT_EQ(find_string(m, "startedAt")->value, "2024-04-27T06:40:00Z");
    EXPECT_EQ(find_string(m, "systemId")->value, "42");
    EXPECT_EQ(find_string(m, "talkgroupId")->value, "1234");
    EXPECT_EQ(find_string(m, "frequencyHz")->value, "851012500");
    EXPECT_EQ(find_string(m, "durationMs")->value, "8500");
    EXPECT_EQ(find_string(m, "unitId")->value, "1234567");
    EXPECT_EQ(find_string(m, "errorCount")->value, "2");
    EXPECT_EQ(find_string(m, "spikeCount")->value, "1");
    EXPECT_EQ(find_string(m, "talkgroupLabel")->value, "PD Disp 1");
    EXPECT_EQ(find_string(m, "talkgroupTag")->value, "Dispatch");
    EXPECT_EQ(find_string(m, "talkgroupGroup")->value, "Public Safety");
    EXPECT_EQ(find_string(m, "talkgroupName")->value, "Police Dispatch 1");
    EXPECT_EQ(find_string(m, "systemLabel")->value, "Metro");
    EXPECT_TRUE(has_string(m, "sources"));
    EXPECT_TRUE(has_string(m, "frequencies"));
    EXPECT_TRUE(has_string(m, "patches"));

    // Optional unset strings must NOT be sent (no empty values on the wire).
    EXPECT_FALSE(has_string(m, "talkerAlias"));
    EXPECT_FALSE(has_string(m, "site"));
    EXPECT_FALSE(has_string(m, "channel"));
    EXPECT_FALSE(has_string(m, "decoder"));

    // The audio file part.
    ASSERT_EQ(m.files().size(), 1u);
    EXPECT_EQ(m.files()[0].name, "audio");
    EXPECT_EQ(m.files()[0].path, "/tmp/x.wav");
    EXPECT_EQ(m.files()[0].content_type, "audio/wav");
}

TEST(UploadRequestMultipart, NeverEmitsLegacyAuthFields)
{
    const auto call = make_sample_call("/tmp/x.wav");
    const auto req = UploadRequest::from_call_data(call, 1);
    Multipart m;
    req.to_multipart(m);
    // Squelch v1 forbids these — guard against accidental regressions.
    EXPECT_FALSE(has_string(m, "key"));
    EXPECT_FALSE(has_string(m, "apiKey"));
    EXPECT_FALSE(has_string(m, "X-API-Key"));
}

TEST(UploadRequestMultipart, OmitsOptionalIntegersWhenUnset)
{
    CallData c;
    c.talkgroup = 7;
    c.start_time = 0;
    c.audio_path = "/tmp/x.wav";
    const auto req = UploadRequest::from_call_data(c, 1);

    Multipart m;
    req.to_multipart(m);
    EXPECT_FALSE(has_string(m, "frequencyHz"));
    EXPECT_FALSE(has_string(m, "durationMs"));
    EXPECT_FALSE(has_string(m, "unitId"));
    EXPECT_FALSE(has_string(m, "errorCount"));
    EXPECT_FALSE(has_string(m, "spikeCount"));
}

// ---- size / file pre-flight -----------------------------------------------

TEST(AudioPreflight, SizeBoundary)
{
    EXPECT_TRUE(squelch::is_audio_size_ok(0));
    EXPECT_TRUE(squelch::is_audio_size_ok(50ULL * 1024ULL * 1024ULL));
    EXPECT_FALSE(squelch::is_audio_size_ok(50ULL * 1024ULL * 1024ULL + 1));
}

TEST(AudioPreflight, MissingFile)
{
    std::string err;
    std::uintmax_t size = 0;
    const bool ok = squelch::check_audio_file(
        "/tmp/squelch-this-path-must-not-exist-xyz.wav", &size, &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}

TEST(AudioPreflight, EmptyPath)
{
    std::string err;
    EXPECT_FALSE(squelch::check_audio_file("", nullptr, &err));
    EXPECT_FALSE(err.empty());
}

TEST(AudioPreflight, RealFileWithinLimit)
{
    char tmpl[] = "/tmp/squelch-preflight-XXXXXX";
    int fd = ::mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    const std::string path(tmpl);
    {
        std::ofstream out(path, std::ios::binary);
        out << "hello";
    }
    ::close(fd);

    std::uintmax_t size = 0;
    std::string err;
    EXPECT_TRUE(squelch::check_audio_file(path, &size, &err));
    EXPECT_EQ(size, 5u);
    EXPECT_TRUE(err.empty());

    ::unlink(path.c_str());
}

// ---- audio_content_type_for ----------------------------------------------

TEST(AudioContentType, KnownExtensions)
{
    EXPECT_EQ(squelch::audio_content_type_for("/x/y.wav"), "audio/wav");
    EXPECT_EQ(squelch::audio_content_type_for("/x/y.WAV"), "audio/wav");
    EXPECT_EQ(squelch::audio_content_type_for("y.mp3"), "audio/mpeg");
    EXPECT_EQ(squelch::audio_content_type_for("y.aac"), "audio/aac");
    EXPECT_EQ(squelch::audio_content_type_for("y.m4a"), "audio/m4a");
    EXPECT_EQ(squelch::audio_content_type_for("y.ogg"), "audio/ogg");
    EXPECT_EQ(squelch::audio_content_type_for("y.opus"), "audio/opus");
    EXPECT_EQ(squelch::audio_content_type_for("y.bin"),
              "application/octet-stream");
    EXPECT_EQ(squelch::audio_content_type_for("noext"),
              "application/octet-stream");
}
