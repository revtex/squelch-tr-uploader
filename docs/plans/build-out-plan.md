# squelch-tr-uploader — Build-Out Plan

> Roadmap for taking the repo from scaffolding to v1.0.0. Updated as phases
> land. See `.github/copilot-instructions.md` for project conventions.

## Goal

Take the current scaffolding (CI-green, no real upload logic) to a v1.0.0
release: a working C++ Trunk-Recorder plugin that produces multipart
uploads to a Squelch `/api/v1/calls` endpoint.

## Phase 0 — Scaffolding (DONE)

- ✅ Repo created, GPL-3.0, dev branch, CI green
- ✅ C++ skeleton (CMake + plugin source stub + 2 gtest scaffolds)
- ✅ Devcontainer, copilot-instructions, expert agents, this plan

## Phase C-1 — Vendor TR's Plugin_Api

- Pin a Trunk-Recorder release tag (latest stable at the time)
- Vendor `Plugin_Api.h` and any required headers under
  `plugin/include/third_party/trunk-recorder/`
- Add a `THIRD_PARTY_NOTICES.md` listing the TR license (GPL-3.0 — compatible)
- Update `CMakeLists.txt` to include the vendored headers
- **Validation:** `cmake --build plugin/build` still passes; the include path
  is wired

## Phase C-2 — Plugin lifecycle hooks

- Implement `extern "C" Plugin_Api * create_plugin()` returning a real instance
- Implement: `parse_config`, `start`, `stop`, `setup_recorder`,
  `setup_system`, `setup_systems`, `setup_sources`, `unit_registration`,
  `unit_deregistration`, `unit_acknowledge_response`, `unit_data_grant`,
  `unit_answer_request`, `unit_location`, `call_start`, `call_end`
- Empty bodies for the ones we don't care about
- Read upload server URL + API key from TR config JSON
- gtest: parse_config produces a `Config` struct from a sample TR JSON

## Phase C-3 — Multipart upload (call_end)

- On `call_end(Call_Data_t)`, build a multipart body matching the
  [native-API plan §5](https://github.com/revtex/OpenScanner/blob/dev/docs/plans/native-api-design-plan.md#5-multipart-call-upload-field-map)
- libcurl `curl_mime_*` for the body; never hand-roll
- Convert `start_time` (epoch seconds) → RFC 3339 `startedAt`
- Reject files >50 MiB before opening the connection
- gtest with a fake `HttpClient` asserts every field name + value

## Phase C-4 — Worker pool & retry

- `std::thread`-based bounded worker pool (count from config, default 2)
- Per-worker libcurl handle pool
- Retry: 3 attempts, exponential backoff (1s, 2s, 4s, jitter ±20%)
- On final failure, log to TR's logger at `error` and a short reason; drop the
  call (don't block TR's record loop)
- Graceful shutdown: drain queue with hard timeout, then forcibly join

## Phase C-5 — Unit registration auto-populate

- On `unit_registration`, cache `unit_id → talker_alias` in a small
  thread-safe map (LRU or bounded by entry count, e.g. 4096).
- In `call_end`, look up the cached alias for the call's `source` /
  `unit_id` and emit it as `talkerAlias` on the upload.
- Keep it inline in `squelch_uploader.cc` — no new module, no interface.
- Skip dedicated tests; this is exercised by the live smoke test in C-6.

## Phase C-6 — Release plumbing

- GitHub Actions release workflow on tag `v*`:
  - Build `squelch_uploader-linux-amd64.so` (Ubuntu 24.04 runner)
  - Build `squelch_uploader-linux-arm64.so` (cross-compile or arm64 runner)
  - Upload to GitHub Releases with checksums
- Release notes drawn from CHANGELOG.md `[Unreleased]` section
- Compatibility matrix in README.md updated by the release PR

## Phase C-7 — Slim-down before v1.0.0 (NEW)

> Before tagging v1.0.0, trim the plugin to match the size and shape of
> rdioscanner_uploader. Tests stay through development, then are removed
> at this phase along with the seams that exist only to support them.

- **Remove the test suite.** Delete `plugin/test/`, drop
  `enable_testing()` / `add_subdirectory(test)` from CMake, drop GoogleTest
  from `FetchContent`, drop the `plugin-test` CI job. Keep `plugin-build`.
- **Collapse the `IHttpClient` interface + factory injection.** It only
  exists so gtest can swap in a fake. Delete the interface, the factory
  parameter on `UploadWorkerPool`, and the `http_client.h` indirection.
  One concrete `HttpClient` class, owned directly by the worker.
- **Fold `retry_policy` into the worker.** Move the ~30 lines of backoff
  math and `is_retriable()` into `upload_worker.cc`. Delete
  `retry_policy.{h,cc}` and its test file.
- **Replace the worker pool with a single background thread + unbounded
  `std::deque`.** TR produces calls at human speed (a few per second
  worst case across all systems). A single thread with rdio's "retry on
  the next call" model is plenty. Drop:
  - `workers` config key (always 1)
  - `queueCapacity` config key (queue is unbounded)
  - drop counter / `dropped()` accessor
  - drain-timeout / detach-stragglers logic — let `stop()` join the
    thread; if the server is wedged we'll wait, same as rdio
- **Fold `http_client_util` into the worker.** Tiny helper, single caller.
- **Keep:** `config.{h,cc}` (validation has real ops value),
  `upload_request.{h,cc}` (wire-field map is non-trivial and worth its
  own module), and `squelch_uploader.cc` (the plugin entry point).
- **Result target:** plugin/src/ down from ~7 files to ~3
  (`squelch_uploader.cc`, `config.cc`, `upload_request.cc` — uploader
  thread lives inside `squelch_uploader.cc` or a small `uploader.cc`).
- **Validation:** plugin loads in TR, performs a real upload to a Squelch
  dev server, drops a call cleanly when the server is down, recovers when
  it comes back. No automated tests after this phase.

## Phase X — Wire-contract cross-check (one-shot)

- Before the slim-down phase (C-7), while tests still exist: capture a
  multipart body from the plugin's gtest dump and diff it byte-by-byte
  against the documented wire contract. Manual diff is fine — this is a
  one-shot pre-release gate, not a CI job.

## v1.0.0 acceptance

- C++ plugin successfully uploads a real recording from a TR install to a
  Squelch dev server
- Plugin multipart body has been diffed once (Phase X) against the
  documented wire contract and matches
- Test suite has been removed (Phase C-7)
- Compatibility matrix in README pinned to a tested TR + Squelch combo
- `plugin/README.md` covers install, config, and troubleshooting

## Out of scope for v1.x

- A separate Python uploader. The repo is plugin-only by design — TR's
  `uploadScript` mechanism is not a supported delivery path here.
- Pre-recording metadata (we get everything from `call_end`)
- Live transcription (Squelch handles that server-side)
- Multi-server fan-out (one TR plugin → one Squelch server; users wanting
  multiple can run multiple plugin instances or use Squelch's own downstream
  feature)
