#!/bin/sh
set -eu

ver=${TAG#v}
grep -q "^project(lumos VERSION $ver " CMakeLists.txt || { echo "FATAL: CMakeLists.txt version is not $ver" >&2; exit 1; }

export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends build-essential devscripts equivs lintian
mk-build-deps -i -r -t 'apt-get install -y -q --no-install-recommends' packaging/debian/control

sh packaging/debian/build-deb.sh "$PWD" /tmp/deb-out
if grep -E '^[EW]:' /tmp/deb-out/lintian.log; then
	echo "FATAL: lintian errors or warnings" >&2
	exit 1
fi

mkdir -p out
for f in /tmp/deb-out/*.deb; do
	base=$(basename "$f" _amd64.deb)
	cp "$f" "out/${base}_debian${DEBIAN}_amd64.deb"
done
