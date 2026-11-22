#!/bin/bash
set -euo pipefail

fail() { echo "FATAL: $*" >&2; exit 1; }

ver=${TAG#v}
src=${SOURCE:-local}
grep -qx "pkgver=$ver" packaging/arch/PKGBUILD || fail "PKGBUILD pkgver is not $ver"
if [ "$src" = local ]; then
	grep -q "^project(lumos VERSION $ver " CMakeLists.txt || fail "CMakeLists.txt version is not $ver"
fi

printf 'Server = %s\n' \
	'https://geo.mirror.pkgbuild.com/$repo/os/$arch' \
	'https://mirrors.kernel.org/archlinux/$repo/os/$arch' \
	'https://mirror.rackspace.com/archlinux/$repo/os/$arch' > /etc/pacman.d/mirrorlist
if command -v systemctl >/dev/null; then
	timeout 300 systemctl is-system-running --wait >/dev/null || true
fi
[ -s /etc/pacman.d/gnupg/trustdb.gpg ] || pacman-key --init
if ! pacman-key --populate archlinux; then
	gpgconf --homedir /etc/pacman.d/gnupg --kill all || true
	rm -rf /etc/pacman.d/gnupg
	pacman-key --init
	pacman-key --populate archlinux
fi
pacman -Sy --noconfirm --needed archlinux-keyring
pacman -Su --noconfirm --needed base-devel git namcap

id builder >/dev/null 2>&1 || useradd -m builder
work=/home/builder/work
rm -rf "$work"
mkdir -p "$work/pkg" "$work/src"
cp packaging/arch/PKGBUILD "$work/pkg/"
if [ "$src" = local ]; then
	git config --global --add safe.directory "$PWD"
	git archive --prefix="lumos-md-$ver/" -o "$work/src/lumos-md-$ver.tar.gz" HEAD
fi
chown -R builder: "$work"

as_builder() { su builder -c "cd $work/pkg && SRCDEST=$work/src $1"; }

sums=$(as_builder "makepkg -g")
[ "$(grep -c . <<<"$sums")" -eq 1 ] || fail "makepkg -g gave more than one line"
sed -i "s|^sha256sums=.*|$sums|" "$work/pkg/PKGBUILD"
as_builder "makepkg --printsrcinfo > .SRCINFO"

mapfile -t deps < <(sed -n 's/^\t\(make\|check\)\{0,1\}depends = //p' "$work/pkg/.SRCINFO")
pacman -S --noconfirm --needed "${deps[@]}"

as_builder "makepkg -f"

pkg=$(ls "$work"/pkg/lumos-md-"$ver"-*-x86_64.pkg.tar.zst)
namcap "$work/pkg/PKGBUILD" "$pkg" | tee "$work/namcap.log"
if grep -E ' [EW]: ' "$work/namcap.log"; then
	fail "namcap errors or warnings"
fi

mkdir -p out
cp "$work"/pkg/*.pkg.tar.zst out/
if [ "$src" = github ]; then
	cp "$work/pkg/PKGBUILD" "$work/pkg/.SRCINFO" out/
fi
