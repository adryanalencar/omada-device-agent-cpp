# OpenOmada Native Agent

This directory contains a new production-oriented C++ implementation of the
Open Omada Device Agent. The Python implementation in the parent repository
remains the behavioral reference and must not be replaced until native parity is
proven through characterization tests and real controller/OpenWrt validation.

The target is constrained OpenWrt hardware first: musl libc, MIPS32/MIPSel,
ARMv7, AArch64, 16 MB flash where practical, and 32-64 MB RAM devices.

## Current Scope

Phase 7 is still an incremental port, not a replacement daemon:

- project/build skeleton;
- tiny CLI entry point;
- protocol/domain utility types;
- ECSP framing and constants;
- ECSP V2 auth primitives;
- small `json-c` ECSP boundary helpers;
- native UDP discovery transport and PRE_ADOPT parser;
- native TCP/TLS length-prefixed frame transport;
- V2 initial adoption handshake through `INIT_SYNC_RESULT`;
- non-secret JSON managed-state persistence;
- direct managed reconnect policy and managed rediscovery fallback;
- minimal managed `INFORM_REQUEST` projection and deterministic scheduler;
- typed Access Point configuration parsing for known controller `SET_REQUEST`
  families;
- protocol-correct `SET_RESPONSE`, `GET_RESPONSE`, defensive NOTIFY replies,
  FORGET responses, and config-version bookkeeping;
- OpenWrt capability modeling, including conservative tool/capability flags and
  `iw list` AP-interface capacity parsing;
- OpenWrt UCI WLAN/radio/SSID VLAN/management VLAN reconciliation planning and
  an injected executor interface for applying a rendered batch plus Wi-Fi reload;
- hostapd/ubus-style wireless telemetry parsing for `wSettings_*`,
  `ssidStats_*`, `radioTraffic_*`, active wireless clients, DHCP enrichment, and
  stale-client filtering;
- openNDS captive portal policy mapping from Omada portal/free-policy config,
  including walled-garden FQDNs, preauthenticated IP rules, controller/external
  portal redirect selection, and `gatewayfqdn=disable` to avoid depending on
  client DNS resolving `status.client`;
- openNDS ThemeSpec generation with TP-Link-style external portal parameters
  (`clientMac`, `clientIp`, `site`, `redirectUrl`, `apMac`, `ssidName`,
  `radioId`);
- openNDS client state parsing and `EVENT_PORTAL_AUTH` handling through
  `ndsctl auth`/`ndsctl deauth`, including conntrack cleanup after portal
  deauth when the openNDS client IP is known;
- composite platform applier support so UCI and openNDS adapters can both
  reconcile one managed `SET_REQUEST`;
- characterization tests against Python-observed fixtures;
- OpenWrt package scaffolding with `procd` init, `/etc/config/openomada`,
  openNDS package dependency, SDK staging helper, and resource benchmark helper.

The lifecycle code can build and serialize discovery, PRE_CONNECT,
DEVICE_VERIFY, SYSTEM_VERIFY, DEVICE_NEGOTIATION and INIT_SYNC messages, and
tests verify the happy path plus auth failure cases against scripted controller
frames. The Phase 7 code also persists reconnect metadata, schedules the first
reply-requested inform and later fire-and-forget informs, models direct
reconnect exhaustion before managed rediscovery, parses known AP configuration
payloads, sends defensive managed-mode replies, and can route actionable
configuration through injected platform appliers. It is not yet wired into a
long-running daemon, a live `libuci`/`libubus` backend, real process execution
for those injected interfaces, or real OpenWrt process startup.

## OpenWrt Package

Stage the native package into an OpenWrt SDK with:

```sh
./scripts/stage_openwrt_package.sh /path/to/openwrt-sdk/package/openomada
```

Build it from the SDK root with:

```sh
make package/openomada/compile V=s
```

The package depends on ordinary OpenWrt components: `libstdcpp`, `libjson-c`,
`libopenssl`, `opennds`, and `conntrack`.

Unsupported protocol families such as `REPORT`, firmware upgrade, mesh, remote
terminal, switch/gateway/OLT profiles, and unvalidated WLAN/security modes must
remain unadvertised until independently implemented and tested.

## Design Rules

- Keep ECSP protocol code independent from OpenWrt command execution.
- Keep raw JSON at the ECSP boundary and translate into typed domain/application
  structures before platform adapters see it.
- Internally normalize MAC addresses as `aa:bb:cc:dd:ee:ff`.
- Serialize MAC addresses toward Omada as `AA-BB-CC-DD-EE-FF`.
- Never log or persist Device Account passwords.
- Prefer OpenWrt-native libraries (`libuci`, `libubus`, `libubox`, `json-c`,
  target TLS/crypto stack) over large bundled dependencies.
- Keep captive portal enforcement delegated to openNDS; the native agent only
  translates Omada intent into openNDS policy and client authorization commands.
