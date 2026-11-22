#!/bin/sh
set -eu

ver=${TAG#v}
grep -q "^project(lumos VERSION $ver " CMakeLists.txt || { echo "FATAL: CMakeLists.txt version is not $ver" >&2; exit 1; }

export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends build-essential devscripts equivs lintian
mk-build-deps -i -r -t 'apt-get -y -q --no-install-recommends' packaging/debian/control

# The build runs as a normal user, as on Debian's buildds: root ignores
# file modes, so a build as root tests less. debian/control has
# Rules-Requires-Root: no, so no fakeroot is needed.
useradd -m builder
debout=/home/builder/deb-out
su builder -c "sh packaging/debian/build-deb.sh '$PWD' $debout"
if grep -E '^[EW]:' "$debout/lintian.log"; then
	echo "FATAL: lintian errors or warnings" >&2
	exit 1
fi

mkdir -p out
for f in "$debout"/*.deb; do
	base=$(basename "$f" _amd64.deb)
	cp "$f" "out/${base}_debian${DEBIAN}_amd64.deb"
done
