// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for squelch::Config::parse — pure JSON validation, no TR or
// libcurl deps.

#include "squelch_uploader/config.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

using nlohmann::json;
using squelch::Config;

namespace
{

    json valid_blob()
    {
        return json{
            {"server", "https://example.com"},
            {"apiKey", "abc"},
            {"shortName", "local"},
            {"systemId", 1234},
        };
    }

} // namespace

TEST(ConfigParse, ParsesAllFields)
{
    auto j = valid_blob();
    j["unitTagsFile"] = "/etc/trunk-recorder/units.csv";

    std::string err;
    auto cfg = Config::parse(j, &err);
    ASSERT_TRUE(cfg.has_value()) << "unexpected error: " << err;
    EXPECT_EQ(cfg->server, "https://example.com");
    EXPECT_EQ(cfg->api_key, "abc");
    EXPECT_EQ(cfg->short_name, "local");
    ASSERT_TRUE(cfg->system_id.has_value());
    EXPECT_EQ(*cfg->system_id, 1234L);
    EXPECT_EQ(cfg->unit_tags_file, "/etc/trunk-recorder/units.csv");
}

TEST(ConfigParse, OptionalFieldsDefaultEmpty)
{
    json j{
        {"server", "http://localhost:8080"},
        {"apiKey", "k"},
    };
    std::string err;
    auto cfg = Config::parse(j, &err);
    ASSERT_TRUE(cfg.has_value()) << err;
    EXPECT_EQ(cfg->short_name, "");
    EXPECT_FALSE(cfg->system_id.has_value());
    EXPECT_EQ(cfg->unit_tags_file, "");
}

TEST(ConfigParse, MissingServerIsError)
{
    auto j = valid_blob();
    j.erase("server");
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("server"), std::string::npos);
}

TEST(ConfigParse, EmptyServerIsError)
{
    auto j = valid_blob();
    j["server"] = "";
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("server"), std::string::npos);
}

TEST(ConfigParse, NonHttpServerIsError)
{
    auto j = valid_blob();
    j["server"] = "ftp://example.com";
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("https://"), std::string::npos);
}

TEST(ConfigParse, MissingApiKeyIsError)
{
    auto j = valid_blob();
    j.erase("apiKey");
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("apiKey"), std::string::npos);
}

TEST(ConfigParse, EmptyApiKeyIsError)
{
    auto j = valid_blob();
    j["apiKey"] = "";
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("apiKey"), std::string::npos);
}

TEST(ConfigParse, NonObjectIsError)
{
    json j = json::array();
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_FALSE(err.empty());
}

TEST(ConfigParse, NonIntegerSystemIdIsError)
{
    auto j = valid_blob();
    j["systemId"] = "not-a-number";
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("systemId"), std::string::npos);
}

// ---- Worker-pool tuning ---------------------------------------------------

TEST(ConfigParse, WorkerTuningDefaults)
{
    auto j = valid_blob();
    std::string err;
    auto cfg = Config::parse(j, &err);
    ASSERT_TRUE(cfg.has_value()) << err;
    EXPECT_EQ(cfg->worker_count, 2u);
    EXPECT_EQ(cfg->queue_capacity, 64u);
    EXPECT_EQ(cfg->max_retries, 3u);
    EXPECT_EQ(cfg->shutdown_drain_seconds, 10u);
}

TEST(ConfigParse, WorkerTuningOverrides)
{
    auto j = valid_blob();
    j["workers"] = 8;
    j["queueCapacity"] = 256;
    j["maxRetries"] = 5;
    j["shutdownDrainSeconds"] = 30;
    std::string err;
    auto cfg = Config::parse(j, &err);
    ASSERT_TRUE(cfg.has_value()) << err;
    EXPECT_EQ(cfg->worker_count, 8u);
    EXPECT_EQ(cfg->queue_capacity, 256u);
    EXPECT_EQ(cfg->max_retries, 5u);
    EXPECT_EQ(cfg->shutdown_drain_seconds, 30u);
}

TEST(ConfigParse, WorkersMustBeAtLeastOne)
{
    auto j = valid_blob();
    j["workers"] = 0;
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("workers"), std::string::npos);
}

TEST(ConfigParse, WorkersAboveBoundIsError)
{
    auto j = valid_blob();
    j["workers"] = 33;
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("workers"), std::string::npos);
}

TEST(ConfigParse, QueueCapacityAboveBoundIsError)
{
    auto j = valid_blob();
    j["queueCapacity"] = 4097;
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("queueCapacity"), std::string::npos);
}

TEST(ConfigParse, MaxRetriesAboveBoundIsError)
{
    auto j = valid_blob();
    j["maxRetries"] = 11;
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("maxRetries"), std::string::npos);
}

TEST(ConfigParse, ShutdownDrainAboveBoundIsError)
{
    auto j = valid_blob();
    j["shutdownDrainSeconds"] = 601;
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("shutdownDrainSeconds"), std::string::npos);
}

TEST(ConfigParse, MaxRetriesZeroIsValid)
{
    auto j = valid_blob();
    j["maxRetries"] = 0;
    std::string err;
    auto cfg = Config::parse(j, &err);
    ASSERT_TRUE(cfg.has_value()) << err;
    EXPECT_EQ(cfg->max_retries, 0u);
}

TEST(ConfigParse, NonIntegerWorkersIsError)
{
    auto j = valid_blob();
    j["workers"] = "two";
    std::string err;
    auto cfg = Config::parse(j, &err);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("workers"), std::string::npos);
}
