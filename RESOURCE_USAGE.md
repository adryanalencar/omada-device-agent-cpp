# Resource Usage

Measurements must be refreshed after each major phase and for every target
profile that matters.

| Build/Target | Stripped Binary | Shared Libraries | Idle RSS | Adoption RSS | SET RSS | INFORM RSS | Startup Time |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| macOS arm64 Phase 1 `MinSizeRel -fno-exceptions -fno-rtti` | 34,472 bytes | `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |

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

Phase 1 does not run a long-lived daemon yet, so idle/adoption/SET/INFORM RSS
measurements would be misleading. The current measurement is only a host build
sanity check for the skeleton plus core protocol/auth primitives.
