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
| macOS arm64 Phase 7 `MinSizeRel -fno-exceptions -fno-rtti` | 34,480 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | N/A | N/A | N/A | N/A | Not measured |
| macOS arm64 daemon-live `MinSizeRel -fno-exceptions -fno-rtti` | 306,816 bytes | `libjson-c.5.dylib`, `libssl.3.dylib`, `libcrypto.3.dylib`, `/usr/lib/libc++.1.dylib`, `/usr/lib/libSystem.B.dylib` | 7,440 KB local no-controller discovery loop | N/A | N/A | N/A | Not measured |

Recommended commands:

```sh
./scripts/benchmark_resource_usage.sh /tmp/openomada-native-size
```

On OpenWrt, measure daemon RSS from `/proc/<pid>/status` and avoid protocol
trace logging during steady-state measurements.

The daemon-live row was produced with:

```sh
./scripts/benchmark_resource_usage.sh /tmp/openomada_cpp_daemon_size
```

The RSS sample ran the stripped native daemon locally against the enabled test
fixture with no controller responding, then read `ps -o rss`. It is useful only
as a host sanity check: macOS dynamic loader/accounting and discovery-only
behavior do not represent OpenWrt adoption, managed SET, or INFORM memory use.

Real OpenWrt package measurements must still be refreshed on target hardware or
an SDK/rootfs environment after validating live controller adoption. Capture at
least idle managed RSS, adoption peak, SET handling, and INFORM build/send RSS.
