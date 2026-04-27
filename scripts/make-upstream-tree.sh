#!/usr/bin/env bash
# scripts/make-upstream-tree.sh
#
# Produce an upstream-shaped copy of the squelch uploader, ready to drop
# under `plugins/squelch_uploader/` inside a Trunk-Recorder source tree
# and submit as a PR.
#
# This script is non-destructive: it only writes into the directory you
# pass as $1 (and deletes that directory first if it already exists).
#
# Usage:
#   scripts/make-upstream-tree.sh <output-dir>
#
# Example end-to-end flow:
#   git clone https://github.com/robotastic/trunk-recorder.git tr
#   cd squelch-tr-uploader
#   ./scripts/make-upstream-tree.sh ../tr/plugins/squelch_uploader
#   # Then edit ../tr/CMakeLists.txt to add:
#   #   add_subdirectory(plugins/squelch_uploader)
#   # And open a PR.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-dir>" >&2
  exit 2
fi

dst="$1"
src_root="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "$(dirname "$dst")"
rm -rf "$dst"
mkdir -p "$dst"

# 1. Drop in the upstream-shaped CMakeLists.txt verbatim.
cp "$src_root/upstream/plugins/squelch_uploader/CMakeLists.txt" "$dst/CMakeLists.txt"

# 2. Translate the plugin source: rewrite the Plugin_Api include from the
#    out-of-tree style ("trunk-recorder/plugin_manager/plugin_api.h", which
#    we relied on through FetchContent) to the in-tree relative style used
#    by every other bundled uploader ("../../trunk-recorder/...").
sed -E \
  -e 's|^#include "trunk-recorder/plugin_manager/plugin_api\.h"$|#include "../../trunk-recorder/plugin_manager/plugin_api.h"|' \
  "$src_root/plugin/squelch_uploader.cc" > "$dst/squelch_uploader.cc"

# 3. Sanity-check: every #include of a TR header should now be relative.
if grep -nE '^#include "trunk-recorder/' "$dst/squelch_uploader.cc"; then
  echo "error: source still references trunk-recorder/ via absolute include" >&2
  exit 1
fi

cat <<EOF
Wrote $dst:
$(cd "$dst" && ls -la)

Next:
  1. Add the following to the top-level Trunk-Recorder CMakeLists.txt,
     next to the other 'add_subdirectory(plugins/...)' lines:

         add_subdirectory(plugins/squelch_uploader)

  2. Build TR end-to-end to confirm it links:

         cmake -S . -B build && cmake --build build

  3. Open the PR.
EOF
