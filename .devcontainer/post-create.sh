#!/usr/bin/env bash
# Post-create hook for the Squelch TR uploader devcontainer.
# Runs once per container creation. Idempotent.
set -euo pipefail

cd "$(dirname "$0")/.."

echo ">> Pre-configuring CMake build directory"
cmake -S plugin -B plugin/build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSQUELCH_BUILD_TESTS=ON \
    >/dev/null || echo "   (initial cmake configure failed — re-run after editing CMakeLists.txt)"

echo ">> Devcontainer ready."
echo "   - Build + test:  cmake --build plugin/build && ctest --test-dir plugin/build"
