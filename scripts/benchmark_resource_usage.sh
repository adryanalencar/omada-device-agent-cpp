#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-/tmp/openomada-native-size}
binary="$build_dir/openomada-agent-native"
stripped="$build_dir/openomada-agent-native.stripped"

cmake -S "$root" -B "$build_dir" \
	-DCMAKE_BUILD_TYPE=MinSizeRel \
	-DOPENOMADA_BUILD_TESTS=OFF \
	-DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"

cmake --build "$build_dir"

cp "$binary" "$stripped"
strip "$stripped"

printf '%s\n' "# OpenOmada Native Resource Snapshot"
printf '%s\n\n' ""
printf '%s\n' "Build directory: $build_dir"
printf '%s\n' "Binary: $stripped"

if stat -f %z "$stripped" >/dev/null 2>&1; then
	bytes=$(stat -f %z "$stripped")
else
	bytes=$(stat -c %s "$stripped")
fi
printf '%s\n' "Stripped binary bytes: $bytes"
printf '%s\n\n' ""

printf '%s\n' "## size"
size "$stripped" || true
printf '%s\n\n' ""

printf '%s\n' "## shared libraries"
if command -v otool >/dev/null 2>&1; then
	otool -L "$stripped"
elif command -v readelf >/dev/null 2>&1; then
	readelf -d "$stripped"
else
	printf '%s\n' "No otool/readelf available"
fi
