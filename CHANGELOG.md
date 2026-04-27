# Changelog

All notable user-visible changes to `squelch-tr-uploader` are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Plugin now uploads completed calls to Squelch via `POST /api/v1/calls`
  (multipart/form-data, `Authorization: Bearer`). Recordings larger than
  50 MiB are rejected locally with a clear log message.
- Mapped Trunk-Recorder call metadata onto the Squelch field map
  (`systemId`, `talkgroupId`, `startedAt` as RFC 3339 UTC, `frequencyHz`,
  `durationMs`, `unitId`, `talkerAlias`, `talkgroupLabel`/`Tag`/`Group`/`Name`,
  `errorCount`/`spikeCount`, plus `sources`/`frequencies`/`patches` as JSON).
- TLS verification is on by default; there is no insecure-mode flag.
- Plugin now builds against Trunk-Recorder's real `Plugin_Api`. Headers are
  pulled at CMake configure time via `FetchContent`, pinned to `v5.2.1` (override
  with `-DSQUELCH_TR_TAG=...`).
- `squelch_uploader.so` exports a `create_plugin` factory via `BOOST_DLL_ALIAS`,
  matching Trunk-Recorder's loader contract (drop the `.so` into TR's plugin
  directory the same way you would `rdioscanner_uploader.so`).
- Plugin configuration block: `server`, `apiKey`, `shortName`, optional
  `systemId`, optional `unitTagsFile`. The server URL is required and must use
  `http://` or `https://`; `apiKey` is required. Invalid configuration now fails
  fast at TR startup with a clear log message instead of silently continuing.
- Lifecycle hooks (`init`, `start`, `stop`, `setup_*`, `unit_*`, `call_start`,
  `call_end`) are wired through. `call_end` currently logs the call it would
  upload — actual multipart `POST /api/v1/calls` arrives in the next release.
- Devcontainer ships the build dependencies needed for the plugin
  (`libboost-all-dev`, `gnuradio-dev`, `libssl-dev`) so `cmake --build
  plugin/build` works out of the box.
- `THIRD_PARTY_NOTICES.md` documents the Trunk-Recorder GPL-3.0-or-later
  upstream, and this changelog file is now part of the repo.
