# Squelch Trunk-Recorder plugin

This directory builds `squelch_uploader.so`, the Trunk-Recorder plugin that
posts completed calls to Squelch's `/api/v1/calls` endpoint.

For installation, configuration, and the wire contract, see the
[top-level README](../README.md). This file covers plugin internals and
direct CMake usage.

## Layout

```
plugin/
├── CMakeLists.txt        ← single CMake target, fetches TR headers
├── README.md             ← (this file)
└── squelch_uploader.cc   ← entire plugin (single source file)
```

The plugin matches the shape of Trunk-Recorder's bundled uploaders under
`plugins/` upstream — one `.cc` per plugin, no public headers.

## Direct CMake build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/squelch_uploader.so`.

The repo root has a [`Makefile`](../Makefile) that wraps this with
`make build`, `make rebuild`, `make lint`, etc.

### CMake variables

| Variable             | Default       | Purpose                                                       |
|----------------------|---------------|---------------------------------------------------------------|
| `SQUELCH_TR_REPO`    | `robotastic/trunk-recorder` | Trunk-Recorder upstream URL.                    |
| `SQUELCH_TR_TAG`     | `v5.2.1`      | TR tag/branch/commit whose `Plugin_Api` headers we compile against. |
| `CMAKE_BUILD_TYPE`   | (unset)       | Standard CMake build type.                                    |

TR headers are fetched via `FetchContent_Populate` at configure time; we
never invoke TR's own `CMakeLists.txt`. The headers go on the include
path as `${trunk_recorder_SOURCE_DIR}` and `…/lib` (the latter for TR's
bundled `<json.hpp>`).

## Internals

Everything lives in `squelch_uploader.cc` in three logical sections:

1. **`Config` + parser** — pulls plugin keys out of TR's
   `boost::property_tree` config, validates `server`/`apiKey`, and bounds
   `maxRetries`.
2. **`Uploader`** — owns the libcurl handle (RAII), runs a single
   background thread draining an unbounded `std::deque`, builds the
   multipart body via `curl_mime`, and applies retry/backoff for
   transient failures (HTTP 408/429/5xx + network errors).
3. **`SquelchUploader : Plugin_Api`** — the TR-facing class. Exports
   `create()` through `BOOST_DLL_ALIAS(... , create_plugin)` so TR's
   plugin loader can instantiate it.

The plugin runs inside TR's process — a crash takes TR down. Errors are
caught and logged internally; nothing is allowed to escape across the
ABI boundary.

## Conventions

- C++17, no exceptions across the plugin boundary.
- RAII for all owned resources; never `curl_easy_cleanup` outside a dtor.
- Public symbols in `namespace squelch`; only `extern "C"` factory
  functions cross the ABI.
- Match the shape of TR's bundled uploaders — keep this single-file. New
  helpers belong in the anonymous namespace, not in new translation units.
