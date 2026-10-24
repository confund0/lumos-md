#!/bin/sh
# Builds the binary .deb. Full log: OUT/build.log.
# Usage: build-deb.sh [SRC] [OUT]
set -eu

src=$(cd "${1:-$(dirname "$0")/../..}" && pwd)
out=${2:-$PWD/deb-out}
ver=$(sed -n 's/^project(lumos VERSION \([0-9.]*\) .*/\1/p' "$src/CMakeLists.txt")
debver=$(sed -n '1s/^lumos-md (\([^)]*\)).*/\1/p' "$src/packaging/debian/changelog")
case $debver in
"$ver"-*) ;;
*) echo "FATAL: debian/changelog version $debver does not match CMake version $ver" >&2; exit 1 ;;
esac

mkdir -p "$out"
log=$out/build.log
work=$out/work
rm -rf "$work"
mkdir -p "$work"
tree=$work/lumos-md-$ver
cp -a "$src" "$tree"
rm -rf "$tree/debian" "$tree/build" "$tree/build-clang" "$tree/build-oracle"
cp -a "$tree/packaging/debian" "$tree/debian"
chmod +x "$tree/debian/rules"

if ! (cd "$tree" && dpkg-checkbuilddeps) >"$log" 2>&1; then
	cat "$log" >&2
	exit 1
fi
if ! (cd "$tree" && dpkg-buildpackage -us -uc -b) >>"$log" 2>&1; then
	grep -nE 'error|Error|FAILED|\*\*\*' "$log" | head -20 >&2
	echo "FAIL: full log in $log" >&2
	exit 1
fi
if command -v lintian >/dev/null 2>&1; then
	lintian --tag-display-limit 0 "$work"/lumos-md_"$debver"_*.changes >"$out/lintian.log" 2>&1 || true
fi
mv "$work"/*.deb "$out/"
echo "ok"
ls "$out"/*.deb
grep -E 'tests passed|tests failed' "$log" || true
[ -f "$out/lintian.log" ] && grep -cE '^[EW]:' "$out/lintian.log" | sed 's/^/lintian E+W: /' || true
