// SPDX-License-Identifier: GPL-3.0-or-later
//
// squelch_uploader.h — version constants for the Squelch uploader plugin.
//
// Trunk-Recorder discovers the plugin via the BOOST_DLL_ALIAS-exported
// factory `create_plugin` defined in `squelch_uploader.cc`. The plugin's
// TR `Plugin_Api` subclass and its lifecycle wiring live entirely in that
// translation unit so this header has no dependency on TR's headers.

#pragma once

namespace squelch
{

    constexpr const char *kPluginName = "squelch_uploader";
    constexpr const char *kPluginVersion = "0.1.0";

} // namespace squelch
