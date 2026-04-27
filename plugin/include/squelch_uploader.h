// SPDX-License-Identifier: GPL-3.0-or-later
//
// squelch_uploader.h — public-facing entry points for the Squelch
// uploader plugin. Trunk-Recorder discovers the plugin via the standard
// Plugin_Api factory function `create_plugin()`.
//
// This header is intentionally minimal during scaffolding; future revisions
// will surface configuration and lifecycle hooks once the upload pipeline
// lands.

#pragma once

namespace squelch {

constexpr const char* kPluginName = "squelch_uploader";
constexpr const char* kPluginVersion = "0.1.0";

}  // namespace squelch
