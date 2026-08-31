# Building

## Native Development Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

## Size-Oriented Build

```sh
cmake -S . -B build-small \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"
cmake --build build-small
strip build-small/openomada-agent-native
size build-small/openomada-agent-native
```

The current Phase 1 code is written to avoid exceptions in runtime paths. Some
toolchains may still require enabling exceptions for third-party/system library
headers; that must be measured per OpenWrt target.

## OpenWrt SDK

The OpenWrt package skeleton lives under `openwrt/`. The intended final package
should be built by copying this source directory into an OpenWrt SDK package
feed as `openomada` and running the SDK package build.

The native runtime must be developed assuming:

- musl libc;
- explicit network byte order for ECSP frames;
- no Boost, Qt, Node.js, Go runtime, or embedded Python;
- small target flash/RAM budget.

