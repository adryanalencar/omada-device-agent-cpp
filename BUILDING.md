# Building

## Native Development Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The current Phase 6 build requires `json-c` and OpenSSL development files
discoverable through `pkg-config`. On macOS the hash helpers still use
CommonCrypto, but TLS transport and secure random generation use OpenSSL. On
non-Apple systems OpenSSL is also used for MD5/SHA256.

This is a pragmatic Phase 6 backend choice. Later OpenWrt builds may swap the
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

The current Phase 6 code is written to avoid exceptions in runtime paths. Some
toolchains may still require enabling exceptions for third-party/system library
headers; that must be measured per OpenWrt target.

Phase 6 adds openNDS portal policy/client mapping, but the portable build still
uses injected interfaces and JSON/text fixtures. A live OpenWrt backend should
bind those interfaces to `libuci`, `libubus`, `uloop` and a small process/native
executor instead of adding shell coupling to ECSP protocol code.

## OpenWrt SDK

The OpenWrt package skeleton lives under `openwrt/`. The intended final package
should be built by copying this source directory into an OpenWrt SDK package
feed as `openomada` and running the SDK package build.

The native runtime must be developed assuming:

- musl libc;
- explicit network byte order for ECSP frames;
- `json-c`;
- OpenSSL-compatible TLS/crypto, currently represented by `+libopenssl`;
- `opennds` and `conntrack` when captive portal support is enabled;
- no Boost, Qt, Node.js, Go runtime, or embedded Python;
- small target flash/RAM budget.
