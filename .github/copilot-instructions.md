# squelch-tr-uploader — Copilot Instructions

## Project Overview

`squelch-tr-uploader` is the first-party Trunk-Recorder uploader for **Squelch** (currently developed at [revtex/OpenScanner](https://github.com/revtex/OpenScanner) — pending rename). It is delivered as a single C++ TR plugin (`squelch_uploader.so`) under [plugin/](../plugin/), loaded by Trunk-Recorder via its `Plugin_Api` (the same mechanism as the built-in `rdioscanner_uploader`). It POSTs completed calls to Squelch's native `/api/v1/calls` endpoint as multipart/form-data with `Authorization: Bearer <api-key>`.

Field-mapping rules: see [Squelch's native-API plan §5](https://github.com/revtex/OpenScanner/blob/dev/docs/plans/native-api-design-plan.md#5-multipart-call-upload-field-map) and the verified contract at [docs/plans/squelch-wire-contract.md](../docs/plans/squelch-wire-contract.md).

## Tech Stack

- C++17, CMake ≥3.20, Ninja, libcurl, nlohmann-json (vendored via TR), Boost (log, dll), GoogleTest (test-only, removed before v1.0.0).
- Dev container: Debian 12, ships the toolchain pre-installed (see [.devcontainer/Dockerfile](../.devcontainer/Dockerfile)).
- CI: GitHub Actions, single `plugin-build` job (cmake + gtest).

## Project Structure

```
plugin/                    ← C++ TR plugin (CMake)
  CMakeLists.txt
  include/                 ← public headers
  src/                     ← .cc sources
  test/                    ← gtest unit tests (removed before v1.0.0)
docs/
  plans/                   ← committed planning docs (build-out plan, wire contract)
.devcontainer/             ← dev container setup (cmake/ninja toolchain)
.github/
  agents/                  ← expert agents per domain
  workflows/ci.yml         ← cmake + gtest job
```

## Subagent Usage

Delegate domain work to the matching expert agent via `runSubagent`:

| Task | Agent |
|---|---|
| C++ plugin code (CMake, src, headers, gtest) | **C++ Expert** |
| Read-only investigation across the codebase | **Explore** |

## Coding Conventions

- C++17, no exceptions across the plugin ABI boundary (TR's plugin loader is C-linkage).
- RAII for all owned resources. libcurl wrapped in a small RAII handle; never `curl_easy_cleanup` manually outside the dtor.
- Public symbols live in `namespace squelch`. Only `extern "C"` factory functions cross the ABI boundary.
- New compilation units register in `plugin/CMakeLists.txt`; gtest binaries via `gtest_discover_tests`.
- Keep the layout small. Match `rdioscanner_uploader`'s shape — a handful of source files, not a service-grade architecture. Don't add interfaces / factories / multi-thread pools that exist only to make tests easier; tests will be removed before v1.0.0.

## Wire Contract

- Field names: `systemId`, `talkgroupId`, `startedAt` (RFC 3339, UTC `Z`), `frequencyHz`, `durationMs`, `unitId`, `audio` (file part), and the optional set in [docs/plans/squelch-wire-contract.md](../docs/plans/squelch-wire-contract.md).
- **Never send unix-timestamp `startedAt`** — Squelch v1 rejects it. Convert from TR's epoch-seconds in the plugin.
- Auth: `Authorization: Bearer <api-key>`. Never `X-API-Key`, never `?key=`, never form `key=`.
- Multipart, not JSON.
- Size limit: 50 MiB on Squelch's v1 upload route. Validate locally before opening the connection.

## Security Rules

1. API key is a secret. Never log it, echo it, or put it in error messages.
2. TR config files contain the API key directly (TR plugin config is JSON). Don't dump the parsed config.
3. TLS verification on by default. `CURLOPT_SSL_VERIFYPEER=1`. No insecure-mode flag.
4. Reject local files >50 MiB before opening a connection.
5. Bound the retry budget — exponential backoff with a per-call ceiling; never retry indefinitely.
6. The plugin runs in TR's process. A crash takes TR down. Check return codes / catch internally; never let an error bubble to TR.

## Tooling Conventions

- Search: VS Code `grep_search`. In the terminal, `rg`. Never plain `grep`.
- File listing: `list_dir` or `file_search`. Avoid `find` in the terminal.
- Validation after change: `cmake --build plugin/build && ctest --test-dir plugin/build`.
- Do not commit or push unless the user asks.

## Planning docs (`docs/plans/`)

- Planning docs are committed so contributors can follow the roadmap.
- `CHANGELOG.md` bullets describe **what changed in the product**, not which plan phase shipped.

## Changelog & Releases

- User-visible changes (features, fixes, security) **must** add a bullet under `[Unreleased]` in `CHANGELOG.md`. Pure refactor / CI tweaks may skip with a `skip-changelog` PR label.
- Bullets describe **what changed in the product**, never **what plan was followed**.
- Releases are tagged `vX.Y.Z` and ship per-arch binaries via GitHub Releases: `squelch_uploader-linux-amd64.so`, `squelch_uploader-linux-arm64.so`, `squelch_uploader-darwin-arm64.dylib`.
- Compatibility table in `README.md` must be updated when the matching Squelch / Trunk-Recorder version changes.
