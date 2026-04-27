# Squelch upload script for Trunk-Recorder

Status: **scaffolding**. Argument shape and field mapping are pinned; the actual POST is not yet wired.

Use this fallback when the C++ plugin (in [../plugin/](../plugin/)) is awkward to deploy — e.g. NAS deployments without a working build toolchain.

## Install

```bash
cd script
pip install -e ".[dev]"
```

## Trunk-Recorder config snippet

```jsonc
{
  "shortName": "MyCity",
  "uploadScript": "/usr/local/bin/squelch-tr-upload",
  "uploadServer": "https://squelch.example.com",
  "uploadApiKey": "tr-recorder-1.<key-secret>"
}
```

The script is invoked by TR with two positional arguments — the audio file path and the metadata JSON path — plus credentials passed via `--server` and `--api-key`. Future work will accept the credentials via env vars (`SQUELCH_URL`, `SQUELCH_API_KEY`) so they don't appear in TR's process list.

## Roadmap

- [ ] Implement the actual POST (multipart) with retry/backoff
- [ ] Read credentials from env vars
- [ ] Honour TR's per-system shortName mapping
- [ ] Optional transcript / unit metadata forwarding
