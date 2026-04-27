#!/usr/bin/env bash
# Post-create hook for the Squelch TR uploader devcontainer.
# Runs once per container creation. Idempotent.
set -euo pipefail

cd "$(dirname "$0")/.."

echo ">> Configuring Python venv for script/"
# Recreate the venv if it is missing OR if its interpreter symlink is dangling
# (common after a container rebuild that discarded the old python3 binary).
if [[ ! -x script/.venv/bin/python ]]; then
    rm -rf script/.venv
    python3 -m venv script/.venv
fi
# shellcheck disable=SC1091
source script/.venv/bin/activate
pip install --upgrade pip --quiet
pip install -e "script[dev]" --quiet

echo ">> Pre-configuring CMake build directory"
cmake -S plugin -B plugin/build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSQUELCH_BUILD_TESTS=ON \
    >/dev/null || echo "   (initial cmake configure failed — re-run after editing CMakeLists.txt)"

echo ">> Devcontainer ready."
echo "   - C++ plugin:  cmake --build plugin/build && ctest --test-dir plugin/build"
echo "   - Python:      source script/.venv/bin/activate && pytest -q"
