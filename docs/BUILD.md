# Build lumos

Needs CMake 3.21 or later, a C++20 compiler and, for the Qt widget and
the demo, Qt 6 (Widgets). The core needs PCRE2 (8-bit, 10.30 or later)
and pkg-config. Tested with GCC 15, Clang 21 and Qt 6.10 on
Alpine; Debian 13 has GCC 14, Clang 19 and Qt 6.8.
The tests fetch Catch2 v3 from GitHub at configure time, so the first
configure with tests needs the network.

## Alpine Linux 3.23 and later

```sh
apk add build-base cmake samurai git pcre2-dev pkgconf python3 qt6-qtbase-dev
```

## Debian 13 and later

```sh
apt install build-essential cmake ninja-build git libpcre2-dev pkgconf python3 qt6-base-dev
```

## Other systems

Linux distributions with the same packages should work. Windows (MSVC)
and macOS are not tested yet.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build -j6 --output-on-failure
```

The Qt tests run with `QT_QPA_PLATFORM=offscreen`, so they need no
display. The tests named `lumos_random_*` edit random documents and take
the longest.

Editor app: `./build/app/lumos-md [file.md]`. With `--debug` it writes
debug log lines to stderr.

## Options

| Option | Default | Effect |
|---|---|---|
| `LUMOS_BUILD_QT` | on | the Qt adapter `lumos::qt` |
| `LUMOS_BUILD_TESTS` | on when top-level | the tests (needs Catch2 from GitHub) |
| `LUMOS_BUILD_APP` | on when top-level | `lumos-md` (needs `LUMOS_BUILD_QT`) |
| `LUMOS_BUILD_BENCH` | on when top-level | `bench/lumos_bench`, not run by ctest |
| `LUMOS_SANITIZE` | off | ASan and UBSan; on musl UBSan in trap mode |
| `LUMOS_CMARK_DIR` | empty | cmark source checkout: adds the `lumos_oracle` test, which compares lumos with cmark on random documents |
| `LUMOS_CMARK_GFM_DIR` | empty | cmark-gfm source checkout: adds `lumos_oracle_gfm`, the same with the GFM extensions |

Core only, without Qt: `-DLUMOS_BUILD_QT=OFF`.

## Compiler flags

GCC and Clang: `-Wall -Wextra -Wpedantic -Werror`,
`-D_GLIBCXX_ASSERTIONS`, `-D_FORTIFY_SOURCE=3` (with optimization),
`-fstack-protector-strong`. The flags are set per target, so a host
project that adds lumos with `add_subdirectory` keeps its own flags.
