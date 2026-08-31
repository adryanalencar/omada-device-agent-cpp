# Porting Status

| Area | Python Reference | C++ Native | Tested | Notes |
| --- | --- | --- | --- | --- |
| Build skeleton | Yes | Initial | Yes | CMake and CLI only; lifecycle is not wired into daemon yet |
| MAC policy | Yes | Initial | Yes | Internal colon-lower, Omada upper-hyphen |
| ECSP frame codec | Yes | Initial | Yes | `uint32_be` + UTF-8 JSON payload |
| ECSP message constants | Yes | Initial | Yes | Preserve numeric values |
| ECSP V2 auth hashes | Yes | Initial | Yes | `cipherType=5`, MD5 + SHA256 uppercase hex |
| ECSP JSON boundary | Yes | Initial | Yes | `json-c` parser/helpers; raw JSON remains at protocol boundary |
| UDP discovery | Yes | Initial | Yes | Discovery payload builder, UDP send/receive transport, PRE_ADOPT parser |
| TLS/TCP management | Yes | Initial | Build only | TCP RAII socket plus OpenSSL frame transport; no live controller test yet |
| Adoption/verification | Yes | Initial | Yes | V2 PRE_CONNECT, DEVICE_VERIFY, SYSTEM_VERIFY against scripted frames |
| Negotiation/init sync | Yes | Initial | Yes | DEVICE_NEGOTIATION and INIT_SYNC_RESULT against scripted frames |
| Managed state | Yes | Initial | Yes | JSON file repository, identity validation, owner-only file mode, no password persistence |
| Managed reconnect | Yes | Initial | Yes | Direct reconnect attempt policy and managed rediscovery fallback are callback-tested |
| INFORM | Partial | Initial | Yes | Minimal `deviceInfo`, `lanInfo`, `needReply`; OpenWrt telemetry projection is fixture-tested but not wired into a daemon |
| Periodic INFORM scheduling | Yes | Initial | Yes | First inform requests reply; later informs are fire-and-forget |
| AP configuration model | Partial | Initial | Yes | Parses known radio, WLAN, VLAN, portal, LED, client operation, and client rate-limit families into typed structures |
| SET/configVersion | Partial | Initial | Yes | Builds Python-compatible `SET_RESPONSE` bodies for absolute/incremental versions; actionable config is rejected until a platform applier exists |
| Defensive GET | Partial | Initial | Yes | Returns `GET_RESPONSE` with `unsupportedKeys` for requested keys |
| Defensive NOTIFY | Partial | Initial | Yes | Supports reply/no-reply handling and V2 reply type selection |
| FORGET | Yes | Initial | Yes | Sends `FORGET_RESPONSE`/`FORGET_RESPONSE_NO_RESET` and marks managed state/session for clearing |
| OpenWrt capability detection | Partial | Initial | Yes | Models tool availability, conservative feature flags, radio bands, openNDS detection, and `iw list` AP-interface limits |
| OpenWrt UCI WLAN | Partial | Initial | Yes | Builds validated UCI batch plans for radio, WLAN, WPA2/WPA3 policy checks, SSID VLAN, management VLAN, and Wi-Fi reload through an injected executor |
| Managed SET applier routing | Partial | Initial | Yes | Actionable SET can advance `configVersion` only when injected platform appliers succeed; failure keeps local version |
| Composite platform appliers | Partial | Initial | Yes | UCI and openNDS appliers can be chained without coupling protocol handling to OpenWrt commands |
| hostapd/ubus telemetry | Partial | Initial | Yes | Parses `network.wireless status` and `hostapd.* get_clients` JSON into `wSettings_*`, `ssidStats_*`, `radioTraffic_*`, and client state |
| DHCP/stale client filtering | Partial | Initial | Yes | Parses dnsmasq lease text; hostapd associated clients remain the active source of truth and DHCP only enriches matching MACs |
| openNDS portal policy | Partial | Initial | Yes | Maps Omada portal/free-policy config to openNDS walled-garden FQDNs, preauthenticated IP rules, ThemeSpec redirect, `gatewayfqdn=disable`, and service restart plan |
| openNDS portal clients | Partial | Initial | Yes | Parses `ndsctl json`, handles `EVENT_PORTAL_AUTH`, calls `ndsctl auth`/`ndsctl deauth`, and flushes conntrack after deauth when a client IP is known |
| OpenWrt package scaffold | Partial | Initial | Yes | SDK package Makefile, CMake install target, procd init, default UCI config, and package staging helper are fixture/syntax-tested |
| OpenWrt service integration | Partial | Initial | Yes | Init script loads UCI, honors `enabled`, sets non-secret runtime env, declares reload trigger, and avoids passing Device Account password via argv/env |
| Cross-compilation | N/A | Prepared | No | SDK staging path is documented, but no OpenWrt SDK target was available in this workspace |
| Resource benchmarking | N/A | Initial | Yes | Local size script records stripped binary/dependencies; RSS waits for real daemon entry point |
| REPORT | Not implemented | Not implemented | No | Do not fake support |
| Firmware upgrade | Not implemented | Not implemented | No | Do not fake support |
