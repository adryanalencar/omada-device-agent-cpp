# OpenWrt Package

This directory contains the OpenWrt package metadata and default service files
for the native C++ agent.

The package root expected by the OpenWrt SDK has this shape:

```text
package/openomada/
  Makefile
  files/
    openomada.config
    openomada.init
  src/
    CMakeLists.txt
    include/
    src/
```

Stage it from this native source tree with:

```sh
./scripts/stage_openwrt_package.sh /path/to/openwrt-sdk/package/openomada
```

Build it from the SDK root with:

```sh
make package/openomada/compile V=s
```

Runtime dependencies are intentionally ordinary OpenWrt packages:

- `libstdcpp`
- `libjson-c`
- `libopenssl`
- `opennds`
- `conntrack`

The init script reads `/etc/config/openomada`, starts under `procd`, and does
not pass the Device Account password through argv or environment variables.
The native daemon is still incomplete in Phase 7; the package scaffolding is
ready for SDK integration, but live controller/OpenWrt parity still requires a
real daemon entry point and native platform executor wiring.
