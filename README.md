# squelch-tr-uploader

First-party [Trunk-Recorder](https://github.com/robotastic/trunk-recorder)
uploader plugin for [Squelch](https://github.com/revtex/squelch) (currently
developed at [revtex/OpenScanner](https://github.com/revtex/OpenScanner)).
Posts completed calls to Squelch's native `/api/v1/calls` endpoint as
`multipart/form-data` with `Authorization: Bearer <api-key>`.

Delivered as a single C++ shared library (`squelch_uploader.so`) loaded by
Trunk-Recorder through its `Plugin_Api` — the same mechanism TR uses for its
own bundled uploaders.

## Features

- Direct `/api/v1/calls` upload — no shell-script middleman.
- Audio + full TR call metadata (talkgroup, frequency, duration, unit, sources,
  frequencies, patches, talker alias, …) mapped onto Squelch's wire contract.
- Background uploader thread; the recorder loop never blocks on the network.
- Exponential backoff with jitter for transient failures (408, 429, 5xx,
  network errors). 4xx is final.
- 50 MiB local pre-flight check — oversized recordings are rejected before
  the connection is opened.
- TLS verification on by default; no insecure-mode flag.
- Drains the in-process queue on shutdown so a clean TR exit also flushes
  pending uploads.

## Compatibility

| `squelch-tr-uploader` | Squelch / OpenScanner            | Trunk-Recorder |
|-----------------------|----------------------------------|----------------|
| `0.1.0`               | ≥ 1.3.0 (native `/api/v1/calls`) | `v5.2.1`       |

The plugin is compiled against the exact TR tag listed above. To build
against a different revision, pass `-DSQUELCH_TR_TAG=<tag>` (or set
`SQUELCH_TR_TAG=…` for the `make` wrapper).

## Build

Required system packages on Debian/Ubuntu:

```bash
sudo apt-get install -y build-essential cmake ninja-build \
    libcurl4-openssl-dev libboost-all-dev gnuradio-dev libssl-dev
```

> Boost and gnuradio-dev are pulled in transitively by Trunk-Recorder's
> `Plugin_Api` headers. The included devcontainer already installs them.

Build via the convenience `Makefile`:

```bash
make            # configure + build (Debug)
make build BUILD_TYPE=Release
make rebuild    # distclean + build
make install    # cmake --install (default prefix /usr/local)
```

Or call CMake directly:

```bash
cmake -S plugin -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build plugin/build
```

The output is `plugin/build/squelch_uploader.so`.

## Install

Drop the `.so` into Trunk-Recorder's plugin search path:

```bash
sudo install -m 0644 plugin/build/squelch_uploader.so \
    /etc/trunk-recorder/plugins/
```

…or use `make install`, which installs to
`<prefix>/lib/trunk-recorder/plugins/squelch_uploader.so`.

## Configure

Add a plugin entry to your Trunk-Recorder `config.json`:

```jsonc
{
  "plugins": [
    {
      "library": "squelch_uploader.so",
      "server": "https://squelch.example.com",
      "apiKey": "tr-recorder-1.<key>",
      "systemId": 1,
      "shortName": "MyCity",
      "unitTagsFile": "/etc/trunk-recorder/units.csv",
      "maxRetries": 3
    }
  ]
}
```

| Key            | Required | Default | Notes                                                                                                       |
|----------------|----------|---------|-------------------------------------------------------------------------------------------------------------|
| `server`       | yes      | —       | Squelch base URL. Must be `http://` or `https://`.                                                          |
| `apiKey`       | yes      | —       | Bearer token issued by Squelch. Never logged.                                                               |
| `systemId`     | yes      | —       | Squelch system ID this Trunk-Recorder instance feeds. Must be a positive integer.                          |
| `shortName`    | no       | —       | TR system short name; populates Squelch's `systemLabel`.                                                    |
| `unitTagsFile` | no       | —       | Path to TR's unit-tag CSV; lets TR resolve `talkerAlias` for uploads.                                       |
| `maxRetries`   | no       | `3`     | Per-call retry budget after the initial attempt. Range `0..10`. Backoff is exponential with jitter, ≤ 30 s. |

Invalid configuration fails fast at TR startup with a clear log message
rather than silently continuing.

## Wire contract

Each completed call is uploaded as a single multipart POST:

```
POST /api/v1/calls
Authorization: Bearer <api-key>
Content-Type: multipart/form-data
```

with the parts listed in [docs/plans/squelch-wire-contract.md](docs/plans/squelch-wire-contract.md).
The `audio` part is the WAV (or M4A when TR's `compress_wav` is set) produced
by Trunk-Recorder; everything else is text fields. `startedAt` is RFC 3339 UTC
(`Z`-suffixed). Optional fields are omitted rather than sent empty.

## Security

- The API key is a secret. Never logged, never echoed in error messages,
  never placed in URLs or query parameters.
- TR `config.json` files contain the key directly — protect those files.
- TLS verification is always on (`CURLOPT_SSL_VERIFYPEER=1`).
- HTTP redirects are not followed.
- Recordings >50 MiB are rejected locally before any connection is opened.
- The retry budget is bounded; failed calls are logged and dropped rather
  than retried indefinitely.

## Development

| Target               | What it does                                          |
|----------------------|-------------------------------------------------------|
| `make` / `make build`| Configure (if needed) and build                       |
| `make configure`     | Run cmake configure only                              |
| `make rebuild`       | `distclean` + `build`                                 |
| `make clean`         | `cmake --build … --target clean`                      |
| `make distclean`     | Remove `plugin/build/`                                |
| `make install`       | `cmake --install`                                     |
| `make format`        | `clang-format -i` plugin sources                      |
| `make format-check`  | Verify formatting without writing                     |
| `make tidy`          | `clang-tidy -p plugin/build`                          |
| `make cppcheck`      | cppcheck static analysis                              |
| `make lint`          | `format-check` + `tidy` + `cppcheck`                  |

Variables: `BUILD_TYPE` (default `Debug`), `GENERATOR` (default `Ninja`),
`JOBS=<N>`, `SQUELCH_TR_TAG=<tag>`.

A `.devcontainer/` is included with the full toolchain pre-installed; open
the repo in VS Code and reopen in the container for a one-shot dev setup.

See [plugin/README.md](plugin/README.md) for plugin internals and the
[docs/plans/](docs/plans/) directory for design notes.

## Changelog

User-visible changes are tracked in [CHANGELOG.md](CHANGELOG.md).

## License

GPL-3.0-or-later, matching Trunk-Recorder and Squelch. See [LICENSE](LICENSE)
and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream
attributions.
