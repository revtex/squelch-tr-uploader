# squelch-tr-uploader

First-party Trunk-Recorder uploader for [Squelch](https://github.com/revtex/squelch) (currently developed as [revtex/OpenScanner](https://github.com/revtex/OpenScanner)). Uploads completed calls to Squelch's native `/api/v1/calls` endpoint with `Authorization: Bearer <api-key>`.

> **Status:** Scaffolding. The plugin and script are skeletons; not yet feature-complete.

This repository ships **two** delivery formats:

| Format | Location | When to use |
|---|---|---|
| **C++ TR plugin** (`squelch_uploader.so`) | [plugin/](plugin/) | Recommended. Loads inside Trunk-Recorder, integrates via TR's `Plugin_Api` (same model as the built-in `rdioscanner_uploader`). |
| **Python upload script** (`upload.py`) | [script/](script/) | Fallback for platforms where loading a `.so` is awkward (e.g. NAS deployments). Runs as TR's `uploadScript`. |

Both target the same wire contract: the multipart shape documented in [Squelch's native-API plan §5](https://github.com/revtex/OpenScanner/blob/dev/docs/plans/native-api-design-plan.md#5-multipart-call-upload-field-map).

## Quick install

### C++ plugin (recommended)

```bash
# Build
cd plugin
cmake -S . -B build
cmake --build build

# Install (drop the .so into TR's plugin search path)
cp build/squelch_uploader.so /etc/trunk-recorder/plugins/
```

Add to your TR `config.json`:

```jsonc
{
  "plugins": [{
    "library": "squelch_uploader.so",
    "server": "https://squelch.example.com",
    "apiKey": "tr-recorder-1.<key>",
    "systemId": 1
  }]
}
```

### Python script (fallback)

See [script/README.md](script/README.md).

## Compatibility

| squelch-tr-uploader | Squelch / OpenScanner | Trunk-Recorder |
|---|---|---|
| `0.x` | ≥ 1.3.0 (native `/api/v1/calls`) | ≥ 4.7 (Plugin_Api v1) |

## License

GPL-3.0 — matches Trunk-Recorder and Squelch.
