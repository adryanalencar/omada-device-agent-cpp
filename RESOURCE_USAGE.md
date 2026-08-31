# Resource Usage

Measurements must be refreshed after each major phase and for every target
profile that matters.

| Build/Target | Stripped Binary | Shared Libraries | Idle RSS | Adoption RSS | SET RSS | INFORM RSS | Startup Time |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| macOS arm64 Phase 1 `MinSizeRel -fno-exceptions -fno-rtti` | 34,472 bytes | `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |
| macOS arm64 Phase 2 `MinSizeRel -fno-exceptions -fno-rtti` | 34,480 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |
| macOS arm64 Phase 3 `MinSizeRel -fno-exceptions -fno-rtti` | 35,032 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |
| macOS arm64 Phase 4 `MinSizeRel -fno-exceptions -fno-rtti` | 34,480 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |
| macOS arm64 Phase 5 `MinSizeRel -fno-exceptions -fno-rtti` | 34,480 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |
| macOS arm64 Phase 6 `MinSizeRel -fno-exceptions -fno-rtti` | 34,472 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |

Recommended commands:

```sh
cmake -S . -B build-small -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-small
strip build-small/openomada-agent-native
size build-small/openomada-agent-native
readelf -d build-small/openomada-agent-native
```

On OpenWrt, measure daemon RSS from `/proc/<pid>/status` and avoid protocol
trace logging during steady-state measurements.

Phase 6 still does not run a long-lived daemon. Idle/adoption/SET/INFORM RSS
measurements would be misleading until the lifecycle is connected to the CLI
process and event-loop work lands in a later phase.

The current macOS binary size is also limited as a resource signal: the CLI does
not yet reference every library object, so the linker can remove much of the
static core from the executable. Real OpenWrt package measurements must be
refreshed after the daemon entry point uses discovery, TLS adoption, managed
state, INFORM, and platform adapters. The Phase 6 row mainly confirms that
adding openNDS mapping code did not affect the current stub executable; it is
not a production resource measurement.
