# Building

## Native Development Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The current daemon-live build requires `json-c` and OpenSSL development files
discoverable through `pkg-config`. On macOS the hash helpers still use
CommonCrypto, but TLS transport and secure random generation use OpenSSL. On
non-Apple systems OpenSSL is also used for MD5/SHA256.

This is a pragmatic initial backend choice. Later OpenWrt builds may swap the
TLS/crypto backend if a target image already carries a smaller compatible stack.

## Size-Oriented Build

```sh
cmake -S . -B build-small \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"
cmake --build build-small
strip build-small/openomada-agent-native
size build-small/openomada-agent-native
```

The current code is written to avoid exceptions in runtime paths. Some
toolchains may still require enabling exceptions for third-party/system library
headers; that must be measured per OpenWrt target.

The live daemon currently binds platform ports through a small argv-based
process executor for `uci batch`, `wifi reload`, `ubus`, `ndsctl`, openNDS
policy commands, and read-only capability probes. ECSP protocol code remains
independent from those commands. A later OpenWrt optimization pass should
replace the higher-churn process calls with `libuci`, `libubus`, `libubox` and
`uloop` where that reduces flash/RAM cost on real targets.

## OpenWrt SDK

The OpenWrt package skeleton lives under `openwrt/`. Stage it into an SDK with:

```sh
./scripts/stage_openwrt_package.sh /path/to/openwrt-sdk/package/openomada
```

Then build from the SDK root:

```sh
make package/openomada/compile V=s
```

The staged package root contains `Makefile`, `files/`, and a compact `src/`
copy of the native C++ source tree. It intentionally does not copy `.git`,
local build directories, or generated binaries.

The native runtime must be developed assuming:

- musl libc;
- explicit network byte order for ECSP frames;
- `json-c`;
- OpenSSL-compatible TLS/crypto, currently represented by `+libopenssl`;
- `opennds` and `conntrack` when captive portal support is enabled;
- no Boost, Qt, Node.js, Go runtime, or embedded Python;
- small target flash/RAM budget.

## Cross-Compilation Matrix

| Target | Expected Path | Daemon-Live Status |
| --- | --- | --- |
| x86_64 Linux/glibc | Native CMake | Prepared, not run in this macOS workspace |
| OpenWrt x86_64/musl | OpenWrt SDK package build | Package staging prepared |
| OpenWrt AArch64/musl | OpenWrt SDK package build | Package staging prepared |
| OpenWrt ARMv7/musl | OpenWrt SDK package build | Package staging prepared |
| OpenWrt MIPS32/MIPSel musl | OpenWrt SDK package build | Package staging prepared; must be measured on target SDK |

This workspace did not include OpenWrt SDK toolchains, so cross-builds
were not executed locally. Treat successful SDK compilation on each target as a
release gate before deploying the native daemon.
