# Upstream (Trunk-Recorder PR) layout

This directory holds the **upstream-shaped** version of the plugin — the
files you would drop under `plugins/squelch_uploader/` inside a
[Trunk-Recorder](https://github.com/robotastic/trunk-recorder) source
tree to submit it as a built-in plugin.

It is a tracking copy, not the source of truth. The source of truth is
[`plugin/squelch_uploader.cc`](../plugin/squelch_uploader.cc) at the
repo root, which compiles standalone via `cmake -S plugin -B …` (or
`make build`).

## Why two layouts?

Out-of-tree (this repo's `plugin/`) and in-tree (TR's `plugins/`) build
the same code differently:

| Concern              | Out-of-tree (`plugin/`)                                | In-tree (`upstream/plugins/squelch_uploader/`) |
|----------------------|--------------------------------------------------------|-------------------------------------------------|
| TR headers           | Fetched at configure time via `FetchContent` (`v5.2.1`). | Already on the include path — TR is the source tree. |
| Boost / libcurl      | Discovered with `find_package`.                        | Inherited from TR's top-level CMake.            |
| `nlohmann/json`      | TR ships `<json.hpp>`; we add TR's `lib/` to includes.  | Same header, already in scope.                  |
| Plugin\_Api include  | `#include "trunk-recorder/plugin_manager/plugin_api.h"` | `#include "../../trunk-recorder/plugin_manager/plugin_api.h"` |
| CMake               | ~50 lines (project, FetchContent, find_package, target). | ~10 lines (`add_library MODULE` + `target_link_libraries` + `install`). |

## Producing a clean upstream copy

Use [`scripts/make-upstream-tree.sh`](../scripts/make-upstream-tree.sh).
It rewrites the include path and copies the upstream `CMakeLists.txt`
into a destination directory inside a TR checkout:

```bash
git clone https://github.com/robotastic/trunk-recorder.git ../tr
./scripts/make-upstream-tree.sh ../tr/plugins/squelch_uploader
```

Then in `../tr/CMakeLists.txt` add (next to the existing
`add_subdirectory(plugins/...)` lines):

```cmake
add_subdirectory(plugins/squelch_uploader)
```

…and build TR end-to-end:

```bash
cd ../tr
cmake -S . -B build && cmake --build build
```

The output is `build/plugins/squelch_uploader/squelch_uploader.so`,
loaded the same way TR loads its other bundled uploaders.

## Keeping the two in sync

The script reads `plugin/squelch_uploader.cc` directly, so any edit to
the source-of-truth file flows into the upstream copy on the next
script run. The only file that has to be hand-maintained here is
[`upstream/plugins/squelch_uploader/CMakeLists.txt`](plugins/squelch_uploader/CMakeLists.txt) —
keep it in line with how the other bundled TR uploaders set up their
`target_link_libraries` (look at e.g. `plugins/openmhz_uploader/`
upstream).
