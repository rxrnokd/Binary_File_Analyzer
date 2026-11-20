#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $(uname -s) != Darwin ]]; then echo "Run on macOS with Xcode Command Line Tools installed." >&2; exit 1; fi
qt_prefix=${QT_PREFIX:-$(brew --prefix qt)}
build_dir=${BUILD_DIR:-build-macos}
archs=${ARCHS:-$(uname -m)}
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$qt_prefix" -DCMAKE_OSX_ARCHITECTURES="$archs"
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
bundle="$build_dir/HEXFORGE.app"
"$qt_prefix/bin/macdeployqt" "$bundle" -always-overwrite -codesign=-
codesign --verify --deep --strict "$bundle"
lipo -archs "$bundle/Contents/MacOS/HEXFORGE"
ditto -c -k --keepParent "$bundle" "$build_dir/HEXFORGE-macOS.zip"
