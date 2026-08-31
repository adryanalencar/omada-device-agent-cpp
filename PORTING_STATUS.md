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
| INFORM | Partial | Initial | Yes | Minimal `deviceInfo`, `lanInfo`, `needReply`; no OpenWrt telemetry yet |
| Periodic INFORM scheduling | Yes | Initial | Yes | First inform requests reply; later informs are fire-and-forget |
| AP configuration model | Partial | Initial | Yes | Parses known radio, WLAN, VLAN, portal, LED, client operation, and client rate-limit families into typed structures |
| SET/configVersion | Partial | Initial | Yes | Builds Python-compatible `SET_RESPONSE` bodies for absolute/incremental versions; actionable config is rejected until a platform applier exists |
| Defensive GET | Partial | Initial | Yes | Returns `GET_RESPONSE` with `unsupportedKeys` for requested keys |
| Defensive NOTIFY | Partial | Initial | Yes | Supports reply/no-reply handling and V2 reply type selection |
| FORGET | Yes | Initial | Yes | Sends `FORGET_RESPONSE`/`FORGET_RESPONSE_NO_RESET` and marks managed state/session for clearing |
| OpenWrt UCI WLAN | Partial | Missing | No | Phase 5 |
| hostapd/ubus telemetry | Partial | Missing | No | Phase 5 |
| openNDS portal | Partial | Missing | No | Phase 6 |
| REPORT | Not implemented | Not implemented | No | Do not fake support |
| Firmware upgrade | Not implemented | Not implemented | No | Do not fake support |
