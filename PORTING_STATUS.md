# Porting Status

| Area | Python Reference | C++ Native | Tested | Notes |
| --- | --- | --- | --- | --- |
| Build skeleton | Yes | Initial | Yes | CMake and CLI only |
| MAC policy | Yes | Planned Phase 1 | Pending | Internal colon-lower, Omada upper-hyphen |
| ECSP frame codec | Yes | Planned Phase 1 | Pending | `uint32_be` + UTF-8 JSON payload |
| ECSP message constants | Yes | Planned Phase 1 | Pending | Preserve numeric values |
| ECSP V2 auth hashes | Yes | Planned Phase 1 | Pending | `cipherType=5`, MD5 + SHA256 uppercase hex |
| UDP discovery | Yes | Missing | No | Phase 2 |
| TLS/TCP management | Yes | Missing | No | Phase 2 |
| Adoption/verification | Yes | Missing | No | Phase 2 |
| Negotiation/init sync | Yes | Missing | No | Phase 2 |
| Managed state | Yes | Missing | No | Phase 3 |
| INFORM | Partial | Missing | No | Phase 3 |
| SET/configVersion | Partial | Missing | No | Phase 4 |
| OpenWrt UCI WLAN | Partial | Missing | No | Phase 5 |
| hostapd/ubus telemetry | Partial | Missing | No | Phase 5 |
| openNDS portal | Partial | Missing | No | Phase 6 |
| REPORT | Not implemented | Not implemented | No | Do not fake support |
| Firmware upgrade | Not implemented | Not implemented | No | Do not fake support |

