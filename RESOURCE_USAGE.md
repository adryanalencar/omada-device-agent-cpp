# Resource Usage

Measurements must be refreshed after each major phase and for every target
profile that matters.

| Build/Target | Stripped Binary | Shared Libraries | Idle RSS | Adoption RSS | SET RSS | INFORM RSS | Startup Time |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| Native host Phase 1 | Pending | Pending | Pending | N/A | N/A | N/A | Pending |

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

