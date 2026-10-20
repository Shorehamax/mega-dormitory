#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cache_root="${XDG_CACHE_HOME:-${HOME}/.cache}"
build_root="${MEGA_BUILD_DIR:-${cache_root}/mega-dormitory/native}"
cmake -S "$project_root" -B "$build_root" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$build_root" --parallel "${MEGA_BUILD_JOBS:-4}"
ctest --test-dir "$build_root" --output-on-failure
