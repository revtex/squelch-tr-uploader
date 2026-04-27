# squelch-tr-uploader — Build-Out Plan

> Roadmap for taking the repo from scaffolding to v1.0.0. Updated as phases
> land. See `.github/copilot-instructions.md` for project conventions.

## Goal

Take the current scaffolding (CI-green, no real upload logic) to a v1.0.0
release: a working C++ Trunk-Recorder plugin and a working Python fallback
script, both producing wire-identical multipart uploads to a Squelch
`/api/v1/calls` endpoint.

## Phase 0 — Scaffolding (DONE)

- ✅ Repo created, GPL-3.0, dev branch, CI green
- ✅ C++ skeleton (CMake + plugin source stub + 2 gtest scaffolds)
- ✅ Python skeleton (pyproject + upload.py stub + 7 pytest tests)
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

- On `unit_registration`, if `talker_alias` is set, surface it in the next
  upload's `talkerAlias` field
- This is a UX nicety — Squelch's auto-populate handles the system/talkgroup
  side; this fills the operator name in
- gtest: a registration followed by a call_end produces a multipart body with
  the right `talkerAlias`

## Phase C-6 — Release plumbing

- GitHub Actions release workflow on tag `v*`:
  - Build `squelch_uploader-linux-amd64.so` (Ubuntu 24.04 runner)
  - Build `squelch_uploader-linux-arm64.so` (cross-compile or arm64 runner)
  - Upload to GitHub Releases with checksums
- Release notes drawn from CHANGELOG.md `[Unreleased]` section
- Compatibility matrix in README.md updated by the release PR

## Phase PY-1 — Real upload in upload.py

- Currently `build_request()` returns a dataclass; the script doesn't actually
  POST yet
- Add a `send_request(req: UploadRequest) -> UploadResult` that POSTs via
  `requests.Session` with timeouts and retries
- pytest with `responses` covers happy path + 4xx + 5xx + timeout

## Phase PY-2 — Env-var credentials & CLI polish

- `--server` / `--api-key` accept env-var defaults (`SQUELCH_URL`,
  `SQUELCH_API_KEY`)
- `--dry-run` prints prepared request, exits 0
- `--debug` enables DEBUG logging
- Documented exit codes (0/1/2)

## Phase PY-3 — Retry & backoff

- Same retry budget as the C++ plugin: 3 attempts, exp backoff with jitter
- `requests.adapters.HTTPAdapter` with `Retry()` for 5xx; manual handling for
  network errors
- `--max-retries` flag overrides default

## Phase X — Wire-contract cross-check (CI)

- Once both formats render real multipart bodies:
  - Add `tools/diff_wire.py` that runs the plugin's gtest dump output and the
    script's pytest dump output through a normalizer
  - CI job `wire-contract-diff` fails on mismatch
- Use the **Wire Contract Expert** agent to audit before tagging v1.0.0

## v1.0.0 acceptance

- C++ plugin successfully uploads a real recording from a TR install to a
  Squelch dev server
- Python script does the same, dropped into the same TR config as
  `uploadScript`
- Both produce identical multipart bodies for the same fixture
- Compatibility matrix in README pinned to a tested TR + Squelch combo
- Documentation in `plugin/README.md` and `script/README.md` covers install,
  config, troubleshooting

## Out of scope for v1.x

- Pre-recording metadata (we get everything from `call_end`)
- Live transcription (Squelch handles that server-side)
- Multi-server fan-out (one TR plugin → one Squelch server; users wanting
  multiple can run multiple plugin instances or use Squelch's own downstream
  feature)
