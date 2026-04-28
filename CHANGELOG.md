# Changelog

All notable user-visible changes to `squelch-tr-uploader` are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.3] — 2026-04-28

### Fixed

- **Plugin load:** the v0.2.2 build linked Boost.Log via the
  `Boost::log` imported target, but the modern Linux default
  `-Wl,--as-needed` linker flag dropped libboost_log (and the rest of
  the Boost components) from `DT_NEEDED` because every reference into
  Boost.Log came from inlined template expansions of
  `BOOST_LOG_TRIVIAL`, which the linker treats as weak. ld.so therefore
  never loaded `libboost_log.so.1.83.0` into the trunk-recorder process
  and the first log call aborted with:

  ```
  trunk-recorder: symbol lookup error:
      /usr/local/lib/trunk-recorder/squelch_uploader.so:
      undefined symbol: _ZN5boost3log12v2s_mt_posix7trivial6logger3getEv
  ```

  Pass `-Wl,--no-as-needed` so every Boost component we explicitly
  link against ends up in `DT_NEEDED` and is loaded at runtime.

## [0.2.2] — 2026-04-28

### Fixed

- **Plugin load:** the v0.2.1 build referenced TR's global
  `frequency_format` symbol (defined in `trunk_recorder_library`'s
  `formatter.cc`). Because we build out-of-tree (TR headers via
  FetchContent, no link against `trunk_recorder_library`), that
  reference was an undefined symbol that `dlopen()` could not resolve
  against the visibility-hidden `trunk-recorder` executable, producing:

  ```
  boost::dll::shared_library::load() failed: undefined symbol: frequency_format
  ```

  The assignment was leftover boilerplate copied from TR's bundled
  plugins; the squelch uploader emits `frequencyHz` as a plain integer
  via its own stringstreams and never calls TR's `format_freq()`
  helper, so it never needed the global. Removed.

## [0.2.1] — 2026-04-27

### Fixed

- **Install:** `cmake --install` now installs `squelch_uploader.so` to
  `${CMAKE_INSTALL_PREFIX}/lib/trunk-recorder/` — the same directory
  Trunk-Recorder's bundled uploaders use, and the path baked into the
  `trunk-recorder` binary's `DT_RUNPATH`. The previous default
  (`lib/trunk-recorder/plugins/`) had an extra `plugins/` subdirectory
  that is not on TR's runpath, so following the README produced a
  plugin TR could not load (`boost::dll::shared_library::load() failed:
cannot open shared object file`). If you were relying on the old
  layout, pass `-DCMAKE_INSTALL_PREFIX=...` to redirect, or copy the
  `.so` yourself.

### Changed

- **Build:** the plugin is now a CMake `MODULE` library (dlopen-only,
  no SONAME, no version symlinks), matching Trunk-Recorder's bundled
  uploaders.

### Documentation

- README “Install” section updated with the corrected destination and
  an explanation of how TR resolves plugin libraries by basename via
  `boost::dll::shared_library::load()` → `dlopen()`.

## [0.2.0] — 2026-04-27

### Changed

- **Breaking:** plugin configuration now requires a `systems[]` array of
  `{ systemId, shortName, unitTagsFile? }` entries. A single
  `squelch_uploader` instance routes each completed call to the matching
  entry by TR's `shortName`; calls whose `shortName` is not listed are
  dropped at debug level. Top-level `systemId` / `shortName` /
  `unitTagsFile` are no longer accepted — move them into a `systems[]`
  entry (one entry is fine for a single-system setup). `shortName`
  values must be unique within `systems[]`.

## [0.1.0] — initial release

First public release of `squelch_uploader.so`, the Trunk-Recorder plugin
for Squelch.

### Plugin

- Loads inside Trunk-Recorder via the `Plugin_Api`. Drop
  `squelch_uploader.so` into TR's plugin search path and add a `plugins`
  entry to `config.json`.
- Built against Trunk-Recorder `v5.2.1` headers, pulled at configure
  time via CMake `FetchContent` (override with `-DSQUELCH_TR_TAG=...`).
- Single background uploader thread; the TR recording loop never blocks
  on network I/O.

### Uploads

- Posts each completed call to `<server>/api/v1/calls` as
  `multipart/form-data` with `Authorization: Bearer <api-key>`.
- TR call metadata is mapped onto Squelch's wire contract:
  `systemId`, `talkgroupId`, `startedAt` (RFC 3339 UTC), `frequencyHz`,
  `durationMs`, `unitId`, `talkerAlias`,
  `talkgroupLabel`/`talkgroupTag`/`talkgroupGroup`/`talkgroupName`,
  `systemLabel`, `errorCount`, `spikeCount`, and the `sources`,
  `frequencies`, and `patches` JSON arrays. Optional fields are omitted
  rather than sent empty.
- `talkerAlias` is auto-populated whenever Trunk-Recorder has resolved
  a unit alias (via `unitTagsFile` or over-the-air alias decode) for
  the call's primary unit.
- Audio is uploaded as the WAV or M4A produced by Trunk-Recorder; the
  plugin picks the compressed file when `compress_wav` is set,
  otherwise the raw recording.
- Recordings larger than 50 MiB are rejected locally before any
  connection is opened, with a clear log message.
- Transient failures (HTTP 408, 429, 5xx, and network errors) are
  retried with exponential backoff plus jitter, capped at 30 s per
  delay. Validation errors (4xx other than 408 / 429) are not retried.
  When the retry budget is exhausted, the call is logged and dropped —
  uploads never block the recorder indefinitely.
- On `stop()`, the plugin blocks until the in-process upload queue has
  drained, so a clean Trunk-Recorder shutdown also flushes pending
  uploads.

### Configuration

Plugin configuration block in `config.json`:

| Key            | Required | Default | Notes                                                                             |
| -------------- | -------- | ------- | --------------------------------------------------------------------------------- |
| `server`       | yes      | —       | Squelch base URL. Must be `http://` or `https://`.                                |
| `apiKey`       | yes      | —       | Bearer token issued by Squelch. Never logged.                                     |
| `systemId`     | yes      | —       | Squelch system ID this Trunk-Recorder instance feeds. Must be a positive integer. |
| `shortName`    | no       | —       | Trunk-Recorder system short name (informational, used in `systemLabel`).          |
| `unitTagsFile` | no       | —       | Path to a TR `unitTagsFile`; used by TR to resolve `talkerAlias`.                 |
| `maxRetries`   | no       | `3`     | Per-call retry budget. Range `0..10`.                                             |

Invalid configuration fails fast at TR startup with a clear log message
instead of silently continuing.

### Security

- TLS verification is on by default. There is no insecure-mode flag.
- The API key is treated as a secret — never logged, never echoed in
  error messages, never written into request URLs or query parameters.
- HTTP redirects are not followed.

### Build & distribution

- C++17, CMake ≥3.20, Ninja, libcurl, Boost (log, dll, system,
  filesystem, regex, thread). nlohmann-json comes via TR.
- Single source file (`plugin/squelch_uploader.cc`) with a small
  `CMakeLists.txt`, in line with Trunk-Recorder's bundled uploaders.
- GPL-3.0-or-later, matching Trunk-Recorder. See
  `THIRD_PARTY_NOTICES.md` for upstream attributions.

### Compatibility

| `squelch-tr-uploader` | Squelch / OpenScanner            | Trunk-Recorder |
| --------------------- | -------------------------------- | -------------- |
| `0.2.0`               | ≥ 1.3.0 (native `/api/v1/calls`) | `v5.2.1`       |
| `0.1.0`               | ≥ 1.3.0 (native `/api/v1/calls`) | `v5.2.1`       |
