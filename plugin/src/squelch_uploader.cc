// SPDX-License-Identifier: GPL-3.0-or-later
//
// squelch_uploader.cc — Trunk-Recorder plugin entry points.
//
// Status: scaffolding. Wires up no real upload yet — just provides the
// shared-library symbols TR's plugin loader expects so the build is exercised
// in CI. The Phase N-4 implementation will add:
//   * parse_config(): read server URL, API key, system mapping
//   * call_end():     POST multipart to /api/v1/calls
//   * worker queue with bounded concurrency + retry/backoff
//   * setup_recorder() / unit_registration() hooks (optional auto-populate)

#include "squelch_uploader.h"

#include <cstdio>

extern "C" {

// TR's plugin loader looks up `create_plugin` (or the version-specific symbol)
// when it dlopen()s the library. We expose a stable C-linkage entry point so
// future ABI work is local to this file.
//
// The signature returned is intentionally `void*` here so the scaffold builds
// without TR's headers on the include path. Real implementation will return
// `Plugin_Api*` once the headers are vendored.
void* create_plugin() {
    std::fprintf(stderr,
                 "[%s %s] scaffold loaded — no-op until Phase N-4 lands\n",
                 squelch::kPluginName, squelch::kPluginVersion);
    return nullptr;
}

}  // extern "C"
