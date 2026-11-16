#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
  printf '%s\n' 'run.sh is for Linux. Use the native build instructions for your platform.' >&2
  exit 1
fi
build_dir="${M1ZUKI_BUILD_DIR:-$project_dir/build-linux}"
# Always run an incremental build: an existing executable may predate new sources.
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target mini-answer --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
exec "$build_dir/m1zuki" "$@"
