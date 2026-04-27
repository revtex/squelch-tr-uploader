// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke tests for plugin identity. The BOOST_DLL_ALIAS-exported factory
// requires dlopen() to exercise; that runs in the integration harness
// (Phase C-3+), not in this unit-test binary.

#include <gtest/gtest.h>

#include "squelch_uploader.h"

TEST(SquelchUploaderScaffold, PluginIdentity)
{
    EXPECT_STREQ(squelch::kPluginName, "squelch_uploader");
    EXPECT_STREQ(squelch::kPluginVersion, "0.1.0");
}
