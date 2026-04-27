# squelch-tr-uploader — Copilot Instructions

## Project Overview

`squelch-tr-uploader` is the first-party Trunk-Recorder uploader for **Squelch** (currently developed at [revtex/OpenScanner](https://github.com/revtex/OpenScanner) — pending rename). It ships **two** delivery formats targeting Squelch's native `/api/v1/calls` endpoint:

| Format | Path | When to use |
|---|---|---|
| C++ TR plugin | [plugin/](../plugin/) | Recommended. Loads inside Trunk-Recorder via its `Plugin_Api`, same model as the built-in `rdioscanner_uploader`. |
| Python upload script | [script/](../script/) | Fallback for platforms where loading a `.so` is awkward. Runs as TR's `uploadScript`. |

Both must produce **byte-identical multipart bodies** for the same input — the field-mapping rules live in [Squelch's native-API plan §5](https://github.com/revtex/OpenScanner/blob/dev/docs/plans/native-api-design-plan.md#5-multipart-call-upload-field-map).

## Tech Stack

- **C++ plugin:** C++17, CMake ≥3.20, Ninja, libcurl, nlohmann-json, GoogleTest.
- **Python script:** Python 3.10+, requests, ruff, mypy `--strict`, pytest, responses (HTTP mocks).
- **Dev container:** Debian 12, ships all toolchains pre-installed (see [.devcontainer/Dockerfile](../.devcontainer/Dockerfile)).
- **CI:** GitHub Actions, two parallel jobs (`plugin-build`, `script-lint`).

## Project Structure

```
plugin/                    ← C++ TR plugin (CMake)
  CMakeLists.txt
  include/                 ← public headers
  src/                     ← .cc sources
  test/                    ← gtest unit tests
script/                    ← Python upload script
  pyproject.toml
  upload.py
  tests/
docs/
  plans/                   ← committed planning docs (build-out plan, etc.)
.devcontainer/             ← dev container setup (cmake/ninja/python toolchains)
.github/
  agents/                  ← expert agents per domain
  workflows/ci.yml         ← parallel build + lint jobs
```

## Subagent Usage — Default Behavior

**Always delegate domain work to the matching expert agent** via `runSubagent`. The top-level conversation stays focused on planning and reporting; agents do the work.

| Task | Agent |
|---|---|
| C++ plugin code (CMake, src, headers, gtest) | **C++ Expert** |
| Python script (mapping, CLI, tests, retries) | **Python Expert** |
| Field-mapping correctness vs. Squelch's API | **Wire Contract Expert** |
| Read-only investigation across the codebase | **Explore** |

When a request touches both plugin and script (e.g. "add a new field"), run **C++ Expert** and **Python Expert** in parallel — they share no code but must produce the same wire format. Then run **Wire Contract Expert** to verify both outputs against Squelch's native upload contract.

## Coding Conventions

### C++ (plugin/)

- C++17, no exceptions across the plugin ABI boundary (TR's plugin loader is C-linkage).
- All allocations RAII; no raw `new`/`delete`.
- libcurl wrapped in a small RAII handle; never call `curl_easy_cleanup` manually outside the dtor.
- Errors propagate via `tl::expected` or sentinel return codes — TR's plugin SPI does not use exceptions.
- All public symbols live in `namespace squelch`. Only `extern "C"` factory functions cross the ABI boundary.
- New compilation units register with `CMakeLists.txt`; gtest binaries via `gtest_discover_tests`.
- Format with `clang-format` (config TBD); `clang-tidy` recommendations are not blocking but should be addressed.

### Python (script/)

- Python 3.10+, type-hint everything, **`mypy --strict` must pass**.
- Standard library + `requests` only at runtime — no heavy frameworks.
- One module per concern. CLI lives in `upload.py`'s `main()`, business logic in pure functions for testability.
- `ruff check .` clean; selected rules: `E, F, W, I, B, UP, SIM, TCH`.
- All HTTP calls go through a single `Session` with timeouts and retries — never bare `requests.post`.
- Tests use `responses` to mock HTTP; no live network calls in CI.

### Wire contract (BOTH)

- Field names: `systemId`, `talkgroupId`, `startedAt` (RFC 3339, UTC `Z`), `frequencyHz`, `durationMs`, `unitId`, `audio` (file part), and the optional set documented in the [native-API plan §5](https://github.com/revtex/OpenScanner/blob/dev/docs/plans/native-api-design-plan.md#5-multipart-call-upload-field-map).
- **Never accept unix-timestamp `startedAt`** — Squelch v1 rejects it with `validation_failed`. Convert from TR's epoch-seconds field to RFC 3339 in the plugin/script.
- Auth: `Authorization: Bearer <api-key>`. Never `X-API-Key`, never `?key=`, never form `key=`. Those are legacy-only on Squelch and the plan deletes them.
- Multipart, not JSON.
- Size limit: 50 MiB on Squelch's v1 upload route. Caller must validate before sending.

## Security Rules

1. API key is a secret. Never log it. Never echo it. Never put it in error messages or in process arguments visible to other users.
2. Read the API key from environment variables (`SQUELCH_API_KEY`) by default — TR config files should reference the env var, not contain the key directly.
3. TLS verification on by default. The script may expose `--insecure` for self-signed dev servers, with a startup warning. The C++ plugin must default to `CURLOPT_SSL_VERIFYPEER=1`.
4. Bound the upload size; reject local files >50 MiB before opening a connection.
5. Bound retry budget — exponential backoff with a per-call ceiling; never retry indefinitely.
6. The plugin runs in TR's process. A panic/crash takes TR down with it. Wrap all external state crossings with try-catch (where exceptions are used inside the plugin) or check return codes; never let an error bubble to TR.

## Tooling Conventions

- Search: VS Code `grep_search` tool. In the terminal, `rg` (ripgrep). Never plain `grep`.
- File listing: `list_dir` or `file_search`. Avoid `find` in the terminal.
- Validation after change: `cmake --build plugin/build && ctest --test-dir plugin/build` for C++; `cd script && ruff check . && mypy --strict upload.py && pytest -q` for Python.
- Do not commit or push unless the user asks.
- Version-pinning: `pyproject.toml` for Python, `CMakeLists.txt` `find_package(... REQUIRED)` for C++.

## Planning docs (`docs/plans/`)

- Planning docs live under `docs/plans/` and are **committed** to the repo so contributors can follow the build-out roadmap.
- `CHANGELOG.md` bullets still describe **what changed in the product**, not which plan phase shipped — link to the plan only when it adds operator-relevant context.

## Changelog & Releases

- User-visible changes (new features, fixes, security) **must** add a bullet under `[Unreleased]` in `CHANGELOG.md`. Pure refactor / CI tweaks may skip with a `skip-changelog` PR label.
- Bullets describe **what changed in the product**, never **what plan was followed**.
- Releases are tagged `vX.Y.Z` and ship per-arch binaries via GitHub Releases:
  - `squelch_uploader-linux-amd64.so`, `squelch_uploader-linux-arm64.so`, `squelch_uploader-darwin-arm64.dylib`
  - `squelch-tr-upload-X.Y.Z.tar.gz` (Python script source dist)
- Compatibility table in `README.md` must be updated when the matching Squelch / Trunk-Recorder version changes.
