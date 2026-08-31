#!/bin/sh
set -eu

usage() {
	printf '%s\n' "usage: $0 <openwrt-sdk-package-dir>"
	printf '%s\n' "example: $0 /path/to/openwrt-sdk/package/openomada"
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
	usage
	exit 0
fi

if [ "$#" -ne 1 ]; then
	usage >&2
	exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dest=$1

case "$dest" in
	''|'/')
		printf '%s\n' "refusing unsafe package destination: $dest" >&2
		exit 2
		;;
esac

mkdir -p "$dest/files"
rm -rf "$dest/src"
mkdir -p "$dest/src"

cp "$root/openwrt/Makefile" "$dest/Makefile"
cp "$root/openwrt/files/openomada.init" "$dest/files/openomada.init"
cp "$root/openwrt/files/openomada.config" "$dest/files/openomada.config"

cp "$root/CMakeLists.txt" "$dest/src/CMakeLists.txt"
cp -R "$root/include" "$dest/src/include"
cp -R "$root/src" "$dest/src/src"

for doc in README.md BUILDING.md PORTING_STATUS.md RESOURCE_USAGE.md MIGRATION_PLAN.md; do
	if [ -f "$root/$doc" ]; then
		cp "$root/$doc" "$dest/src/$doc"
	fi
done

printf '%s\n' "staged OpenWrt package at $dest"
printf '%s\n' "build from the SDK root with:"
printf '%s\n' "  make package/openomada/compile V=s"
