#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
  printf '%s\n' 'Build this package on Linux with Qt 6 and KDE Frameworks 6 installed.' >&2
  exit 1
fi
build_dir="${M1ZUKI_BUILD_DIR:-$project_dir/build-linux}"
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure
if command -v node >/dev/null 2>&1; then
  node "$project_dir/tests/test_captureprotection_script.js"
fi

package_name="m1zuki-linux-$(uname -m)"
mkdir -p "$project_dir/dist"
staging_dir="$(mktemp -d "$project_dir/dist/.linux-package-XXXXXX")"
trap 'rm -rf -- "$staging_dir"' EXIT
cmake --install "$build_dir" --prefix "$staging_dir/$package_name"
install -m 755 "$project_dir/install-package.sh" "$staging_dir/$package_name/install-package.sh"
install -m 644 "$project_dir/LINUX.md" "$staging_dir/$package_name/LINUX.md"
tar -C "$staging_dir" -czf "$project_dir/dist/$package_name.tar.gz" "$package_name"
printf 'Built: %s\n' "$project_dir/dist/$package_name.tar.gz"
printf '%s\n' 'This package uses the host Qt/KDE libraries; it is not a standalone AppImage.'
