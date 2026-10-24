#!/bin/sh
set -eu

ver=${TAG#v}
grep -qx "pkgver=$ver" packaging/alpine/APKBUILD || { echo "FATAL: APKBUILD pkgver is not $ver" >&2; exit 1; }
grep -q "^project(lumos VERSION $ver " CMakeLists.txt || { echo "FATAL: CMakeLists.txt version is not $ver" >&2; exit 1; }

apk add --no-cache abuild build-base git openssl

key=/root/lumos-md.rsa
printf '%s\n' "$ABUILD_KEY" > "$key"
chmod 600 "$key"
openssl rsa -in "$key" -pubout 2>/dev/null | cmp -s - keys/lumos-md.rsa.pub || { echo "FATAL: ABUILD_KEY does not match keys/lumos-md.rsa.pub" >&2; exit 1; }
cp keys/lumos-md.rsa.pub /etc/apk/keys/

work=/tmp/abuild
mkdir -p "$work/src" "$work/repo"
git config --global --add safe.directory /src
git archive --prefix="lumos-md-$ver/" -o "$work/src/lumos-md-$ver.tar.gz" HEAD
cp packaging/alpine/APKBUILD "$work/"

export SRCDEST="$work/src" REPODEST="$work/repo" PACKAGER_PRIVKEY="$key"
export PACKAGER="confund0 <110197472+confund0@users.noreply.github.com>"
(cd "$work" && abuild -F checksum && abuild -F -r)

mkdir -p out
for f in "$work"/repo/*/x86_64/lumos-md-"$ver"-r*.apk "$work"/repo/*/x86_64/lumos-md-doc-"$ver"-r*.apk; do
	apk verify "$f"
	base=$(basename "$f" .apk)
	cp "$f" "out/$base-alpine$ALPINE-x86_64.apk"
done
