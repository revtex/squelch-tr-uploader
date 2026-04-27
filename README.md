# squelch-tr-uploader

First-party Trunk-Recorder uploader for [Squelch](https://github.com/revtex/squelch) (currently developed as [revtex/OpenScanner](https://github.com/revtex/OpenScanner)). Uploads completed calls to Squelch's native `/api/v1/calls` endpoint with `Authorization: Bearer <api-key>`.

> **Status:** Pre-release. Working uploader with multipart `/api/v1/calls` POSTs and a background uploader thread; not yet tagged 1.0.

Delivered as a Trunk-Recorder C++ plugin (`squelch_uploader.so`) under [plugin/](plugin/). It loads inside TR via the `Plugin_Api`, the same mechanism TR uses for its bundled uploaders. The wire contract is the multipart shape documented in [Squelch's native-API plan §5](https://github.com/revtex/OpenScanner/blob/dev/docs/plans/native-api-design-plan.md#5-multipart-call-upload-field-map).

## Quick install

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

## Compatibility

| squelch-tr-uploader | Squelch / OpenScanner | Trunk-Recorder |
|---|---|---|
| `0.x` | ≥ 1.3.0 (native `/api/v1/calls`) | ≥ 4.7 (Plugin_Api v1) |

## License

GPL-3.0 — matches Trunk-Recorder and Squelch.
