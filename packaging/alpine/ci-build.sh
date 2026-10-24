#!/bin/sh
set -eu

ver=${TAG#v}
grep -qx "pkgver=$ver" packaging/alpine/APKBUILD || { echo "FATAL: APKBUILD pkgver is not $ver" >&2; exit 1; }
grep -q "^project(lumos VERSION $ver " CMakeLists.txt || { echo "FATAL: CMakeLists.txt version is not $ver" >&2; exit 1; }

apk update
apk add abuild build-base git openssl

key=/root/lumos-md.rsa
printf '%s\n' "$ABUILD_KEY" > "$key"
chmod 600 "$key"
openssl rsa -in "$key" -pubout 2>/dev/null | cmp -s - keys/lumos-md.rsa.pub || { echo "FATAL: ABUILD_KEY does not match keys/lumos-md.rsa.pub" >&2; exit 1; }
cp keys/lumos-md.rsa.pub /etc/apk/keys/
cp keys/lumos-md.rsa.pub "$key.pub"

work=/tmp/abuild
mkdir -p "$work/pkg" "$work/distfiles" "$work/repo"
git config --global --add safe.directory /src
git archive --prefix="lumos-md-$ver/" -o "$work/distfiles/lumos-md-$ver.tar.gz" HEAD
cp packaging/alpine/APKBUILD "$work/pkg/"

export SRCDEST="$work/distfiles" REPODEST="$work/repo" PACKAGER_PRIVKEY="$key"
export PACKAGER="confund0 <110197472+confund0@users.noreply.github.com>"
(cd "$work/pkg" && abuild -F checksum && abuild -F -r)

mkdir -p out
for f in "$work"/repo/*/x86_64/lumos-md-"$ver"-r*.apk "$work"/repo/*/x86_64/lumos-md-doc-"$ver"-r*.apk; do
	apk verify "$f"
	base=$(basename "$f" .apk)
	cp "$f" "out/$base-alpine$ALPINE-x86_64.apk"
done
