# Squelch C++ plugin for Trunk-Recorder

Status: **scaffolding**. Builds and links; does not yet perform real uploads.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSQUELCH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Required system packages on Debian/Ubuntu:

```bash
sudo apt-get install -y build-essential cmake \
    libcurl4-openssl-dev nlohmann-json3-dev \
    libgtest-dev libgmock-dev \
    libboost-all-dev gnuradio-dev libssl-dev
```

> `libboost-all-dev` and `gnuradio-dev` are pulled in transitively by
> Trunk-Recorder's `Plugin_Api` headers (boost::log, boost::dll,
> gnuradio types in `gr_blocks/decoder_wrapper.h`). They're heavy — a
> trimmed package set is feasible but not yet pinned. The devcontainer
> already installs them.

## Install

```bash
sudo cmake --install build --prefix /usr/local
# → /usr/local/lib/trunk-recorder/plugins/squelch_uploader.so
```

Or copy the `.so` directly into TR's plugin search path.

## Trunk-Recorder config snippet

```jsonc
{
  "plugins": [
    {
      "library": "squelch_uploader.so",
      "server": "https://squelch.example.com",
      "apiKey": "tr-recorder-1.<key-secret>",
      "systemId": 1,
      "shortName": "MyCity",
      "concurrency": 2
    }
  ]
}
```

The plugin discovers the TR Plugin_Api headers in this order:

1. `TR_PLUGIN_API_INCLUDE` environment variable (development).
2. Vendored copy under `third_party/trunk-recorder/` (release builds — TBD).

## Roadmap

- [ ] Vendor pinned TR `Plugin_Api.h` set
- [ ] Implement `parse_config`
- [ ] Implement `call_end` → multipart POST to `/api/v1/calls`
- [ ] Bounded worker queue with retry/backoff
- [ ] Optional `setup_recorder` / `unit_registration` auto-populate hooks
- [ ] Per-arch GitHub Releases (`linux-amd64`, `linux-arm64`, `darwin-arm64`)
