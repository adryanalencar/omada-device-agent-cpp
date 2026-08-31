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

The init script reads `/etc/config/openomada`, starts under `procd`, passes the
config path through `OPENOMADA_CONFIG`, and does not pass the Device Account
password through argv or environment variables. The packaged service is disabled
by default until `controller.host`, `controller.password`, and `device.mac` are
configured.

The native daemon now performs managed-state reconnect, UDP discovery, TLS
adoption, V2 initial sync, managed request handling, state persistence, periodic
INFORM, UCI WLAN reconciliation, and openNDS portal policy/auth orchestration.
The current backend is process-backed (`uci`, `wifi`, `ubus`, `ndsctl`) rather
than direct `libuci`/`libubus`; that keeps the live daemon useful while leaving
the lower-RAM native adapter work as a later optimization.
