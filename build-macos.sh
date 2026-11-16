#!/bin/bash
set -euo pipefail
[[ "$(uname -s)" == Darwin ]] || { echo 'Run this script on macOS 13+.' >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="${M1ZUKI_BUILD_DIR:-$ROOT/build-macos}"
if [[ -z "${QT_ROOT:-}" ]]; then
  command -v brew >/dev/null || { echo 'Set QT_ROOT to a Qt 6.5+ macOS installation, or install Qt with Homebrew.' >&2; exit 1; }
  QT_ROOT="$(brew --prefix qt)"
fi
ARCHS="${M1ZUKI_MAC_ARCHS:-$(uname -m)}"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  "-DCMAKE_OSX_ARCHITECTURES=$ARCHS" -DBUILD_TESTING=ON
cmake --build "$BUILD" --parallel "${M1ZUKI_BUILD_JOBS:-2}"
ctest --test-dir "$BUILD" --output-on-failure
STAGE="$(mktemp -d "$BUILD/package.XXXXXX")"
cp -R "$BUILD/m1zuki.app" "$STAGE/"
"$QT_ROOT/bin/macdeployqt" "$STAGE/m1zuki.app" -always-overwrite
RESOURCES="$STAGE/m1zuki.app/Contents/Resources"
mkdir -p "$RESOURCES"
cp "$ROOT/MACOS.md" "$ROOT/THIRD_PARTY_NOTICES.txt" "$RESOURCES/"
cp -R "$ROOT/licenses" "$RESOURCES/"
cp "$ROOT/assets/mini-answer-character-credits.txt" "$RESOURCES/"
# Local ad-hoc signature preserves bundle integrity; it is NOT Developer ID notarization.
codesign --force --deep --sign - "$STAGE/m1zuki.app"
codesign --verify --deep --strict "$STAGE/m1zuki.app"
for ARCH in ${ARCHS//;/ }; do
  lipo -verify_arch "$ARCH" "$STAGE/m1zuki.app/Contents/MacOS/m1zuki"
done
# Exercise the deployed app without permissions, a screen capture or an API call.
QT_QPA_PLATFORM=cocoa "$STAGE/m1zuki.app/Contents/MacOS/m1zuki" --render-preview "$BUILD/settings-preview.png"
mkdir -p "$ROOT/dist"
LABEL="${ARCHS//;/+}"
ditto -c -k --sequesterRsrc --keepParent "$STAGE/m1zuki.app" "$ROOT/dist/m1zuki-macos-$LABEL.zip"
cp "$ROOT/MACOS.md" "$ROOT/dist/MACOS.md"
echo "Built: $ROOT/dist/m1zuki-macos-$LABEL.zip (ad-hoc signed; not notarized)"
