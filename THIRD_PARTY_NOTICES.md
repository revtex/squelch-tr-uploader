# Third-Party Notices

This project (`squelch-tr-uploader`) bundles or depends on third-party software
listed below. Each component retains its original copyright and license.

## Trunk-Recorder

- Upstream: https://github.com/robotastic/trunk-recorder
- Pinned tag: `v5.2.1`
- License: GPL-3.0-or-later
- Copyright: © 2014– Luke Berndt and contributors
- Upstream LICENSE: https://github.com/robotastic/trunk-recorder/blob/v5.2.1/LICENSE

A subset of Trunk-Recorder's public headers is fetched at CMake configure
time (via `FetchContent`) so the plugin can build against `Plugin_Api`
without requiring operators to check out Trunk-Recorder's full source tree.
The fetched headers retain their upstream GPL-3.0-or-later license; nothing
from Trunk-Recorder is committed to this repository.

`squelch-tr-uploader` is itself licensed under GPL-3.0-or-later (see
[LICENSE](LICENSE)), so there is no additional license-compatibility concern
from compiling against these headers.

To bump the pinned tag, edit `SQUELCH_TR_TAG` in
[plugin/CMakeLists.txt](plugin/CMakeLists.txt) and reconfigure.
