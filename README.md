# squelch-tr-uploader

A [Trunk Recorder](https://github.com/robotastic/trunk-recorder) plugin that uploads completed calls (audio + metadata) to an [OpenScanner](https://github.com/revtex/OpenScanner) server.

## Status

Drop-in `user_plugins/` build, distributed as source — Trunk Recorder builds and installs the plugin shared library (`squelch_uploader.so`) alongside its own binaries.

Requires Trunk Recorder **5.0 or later**.

## Install

Clone this repository into the `user_plugins/` directory of your Trunk Recorder source tree, then build Trunk Recorder normally:

```bash
cd /path/to/trunk-recorder
mkdir -p user_plugins
git clone https://github.com/revtex/squelch-tr-uploader.git user_plugins/squelch_uploader
mkdir -p build && cd build
cmake ..
make -j"$(nproc)"
sudo make install
```

The plugin installs to `/usr/local/lib/trunk-recorder/squelch_uploader.so`.

To update later: `cd user_plugins/squelch_uploader && git pull`, then re-run `make install` from the Trunk Recorder build directory.

## Configure

Reference the plugin from your Trunk Recorder `config.json`:

```jsonc
{
  // …your usual systems / sources / etc.
  "plugins": [
    {
      "name": "OpenScanner Uploader",
      "library": "squelch_uploader.so",

      // OpenScanner server base URL. Must start with http:// or https://.
      "server": "https://openscanner.example.com:3022",

      // API key issued by OpenScanner (Admin → API Keys). The same key
      // is used for every system uploaded by this trunk-recorder.
      "apiKey": "REPLACE_ME",

      // Per-system routing — match by `shortName` from the corresponding
      // `systems[]` entry elsewhere in this config.json. Each entry maps
      // a TR shortName to its OpenScanner system_id.
      "systems": [
        { "systemId": 1, "shortName": "metro" },
        { "systemId": 2, "shortName": "county" }
      ]
    }
  ]
}
```

If the plugin can't be found by name, supply a full path:

```jsonc
"library": "/usr/local/lib/trunk-recorder/squelch_uploader.so"
```

## Build dependencies

The plugin links against the same Boost / GNURadio components Trunk Recorder already depends on, plus libcurl for HTTPS uploads. There are no extra dev packages to install beyond the standard Trunk Recorder build environment.

## Docker

The reference build is in [revtex/OpenScanner — `systems-config/TrunkRecorder/Dockerfile`](https://github.com/revtex/OpenScanner/blob/main/systems-config/TrunkRecorder/Dockerfile). It clones this repo into `user_plugins/` during the builder stage so the resulting image ships with `squelch_uploader.so` pre-built.

## License

GPL-3.0 — see [LICENSE](LICENSE).
