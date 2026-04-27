// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke tests for the scaffolded plugin. Real upload-pipeline tests will land
// alongside the Phase N-4 implementation.

#include <gtest/gtest.h>

#include "squelch_uploader.h"

TEST(SquelchUploaderScaffold, PluginIdentity) {
    EXPECT_STREQ(squelch::kPluginName, "squelch_uploader");
    EXPECT_STREQ(squelch::kPluginVersion, "0.1.0");
}

extern "C" void* create_plugin();

TEST(SquelchUploaderScaffold, CreatePluginReturnsNullForNow) {
    // Scaffold returns nullptr until the real Plugin_Api wiring lands.
    EXPECT_EQ(create_plugin(), nullptr);
}
