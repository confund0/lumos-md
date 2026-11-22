#!/bin/sh
set -eu

ver=${TAG#v}
grep -qx "pkgver=$ver" packaging/alpine/APKBUILD || { echo "FATAL: APKBUILD pkgver is not $ver" >&2; exit 1; }
grep -q "^project(lumos VERSION $ver " CMakeLists.txt || { echo "FATAL: CMakeLists.txt version is not $ver" >&2; exit 1; }

apk update
apk add abuild build-base git openssl

# abuild runs as a normal user, as on Alpine's builders: root ignores
# file modes, so a build as root tests less. The group abuild lets
# abuild -r install the build dependencies (abuild-sudo).
adduser -D builder
addgroup builder abuild
home=/home/builder

key=$home/lumos-md.rsa
printf '%s\n' "$ABUILD_KEY" > "$key"
chmod 600 "$key"
openssl rsa -in "$key" -pubout 2>/dev/null | cmp -s - keys/lumos-md.rsa.pub || { echo "FATAL: ABUILD_KEY does not match keys/lumos-md.rsa.pub" >&2; exit 1; }
cp keys/lumos-md.rsa.pub /etc/apk/keys/
cp keys/lumos-md.rsa.pub "$key.pub"

work=$home/abuild
mkdir -p "$work/pkg" "$work/distfiles" "$work/repo"
git config --global --add safe.directory /src
git archive --prefix="lumos-md-$ver/" -o "$work/distfiles/lumos-md-$ver.tar.gz" HEAD
cp packaging/alpine/APKBUILD "$work/pkg/"
chown -R builder:builder "$key" "$key.pub" "$work"

# su without -l keeps these variables.
export SRCDEST="$work/distfiles" REPODEST="$work/repo" PACKAGER_PRIVKEY="$key"
export PACKAGER="confund0 <110197472+confund0@users.noreply.github.com>"
su builder -c "cd $work/pkg && abuild checksum && abuild -r"

mkdir -p out
for f in "$work"/repo/*/x86_64/lumos-md-"$ver"-r*.apk "$work"/repo/*/x86_64/lumos-md-dbg-"$ver"-r*.apk "$work"/repo/*/x86_64/lumos-md-doc-"$ver"-r*.apk; do
	apk verify "$f"
	base=$(basename "$f" .apk)
	cp "$f" "out/$base-alpine$ALPINE-x86_64.apk"
done
