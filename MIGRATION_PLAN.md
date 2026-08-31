# Native Migration Plan

The Python implementation in the parent repository is the behavioral reference.
This C++ implementation must preserve validated protocol behavior before it
tries to optimize or extend functionality.

## Reference Architecture

The current Python agent is organized around bounded contexts and ports:

```text
Controller
  -> ECSP inbound adapter
  -> typed mappers/domain/application use cases
  -> platform ports
  -> OpenWrt adapters
```

Telemetry flows in the opposite direction:

```text
OpenWrt observations
  -> typed wireless/client/portal state
  -> Inform projection
  -> ECSP serialization
  -> Controller
```

Important reference files:

- `README.md`
- `docs/architecture.md`
- `docs/protocol-overview.md`
- `docs/ecsp-wire-format.md`
- `docs/device-lifecycle.md`
- `docs/protocol-status.md`
- `docs/configuration.md`
- `SECURITY.md`
- `tests/*.py`
- `src/open_omada_device_agent/adapters/inbound/ecsp/protocol.py`
- `src/open_omada_device_agent/adapters/inbound/ecsp/crypto.py`
- `src/open_omada_device_agent/discovery.py`
- `src/open_omada_device_agent/adoption.py`
- `src/open_omada_device_agent/projections/inform.py`
- `src/open_omada_device_agent/adapters/outbound/openwrt/*`

## Proposed C++ Architecture

```text
include/openomada/
  domain/        typed value objects and AP/device models
  protocol/      ECSP constants, framing, DTO boundary
  crypto/        ECSP auth primitives behind a replaceable backend
  lifecycle/     controller/device state machine
  application/   use cases and platform ports
  platform/      generic platform capability interfaces
  openwrt/       OpenWrt implementation using native APIs where practical
  persistence/   non-secret managed-state repository
  transport/     UDP and TLS/TCP transport
  cli/           daemon entry point
```

ECSP protocol code must not call OpenWrt commands directly. OpenWrt remains the
first backend, not a permanent protocol dependency.

## Dependency Choices

Initial dependencies are deliberately conservative:

- C++17 and CMake.
- CommonCrypto on macOS development builds.
- OpenSSL for TLS transport and secure random generation.
- OpenSSL/libcrypto on non-Apple builds for MD5/SHA256 fixtures.
- `json-c` for the ECSP JSON boundary.
- No Boost, Qt, Node.js, Go runtime, or embedded Python.

Planned OpenWrt-oriented dependencies:

- `libuci` instead of spawning `uci`.
- `libubus`/`libubox`/`uloop` instead of spawning `ubus` and for the event loop.
- The smallest target TLS/crypto stack already selected by the OpenWrt image
  where practical.

Every dependency choice must consider flash size, RAM use, musl compatibility,
MIPS/ARM portability, OpenWrt feed availability, and duplicated functionality.

## Migration Phases

1. Build skeleton, MAC policy, ECSP constants, frame codec, auth hashes, and
   characterization tests.
2. Lifecycle state machine, UDP discovery, TLS/TCP transport, authentication,
   negotiation, and initial sync.
3. Non-secret managed-state persistence, managed reconnect, managed rediscovery,
   and periodic INFORM.
4. SET parsing, typed configuration models, `SET_RESPONSE`, `GET_RESPONSE`,
   defensive NOTIFY behavior, and config version semantics.
5. OpenWrt capability detection, `libuci` WLAN/radio/VLAN reconciliation,
   `libubus`/hostapd telemetry, DHCP enrichment, and stale-client filtering.
6. openNDS captive portal policy, ThemeSpec redirect, auth/deauth, conntrack
   cleanup, and portal state reporting.
7. OpenWrt package hardening, cross-compilation, resource benchmarking, and
   real controller parity validation.

## Compatibility And Resource Risks

- JSON key order may be insignificant in most places, but any wire-sensitive
  path must be characterized before changing serialization.
- MAC format must remain lower-case colon internally and upper-case hyphen when
  emitted toward Omada.
- `cipherType=5` authentication requires preserving MD5/SHA256 uppercase hex
  formatting exactly.
- Do not advertise capabilities just because names are known; unsupported
  controller requests must fail explicitly like the Python reference.
- Avoid retaining multiple copies of large JSON messages during INFORM/SET.
- Old OpenWrt targets may lack development headers or use a different TLS
  provider; crypto/TLS backend selection must remain replaceable.
- `REPORT`, upgrade, remote terminal, mesh, switch/gateway/OLT profiles, and
  unvalidated security modes remain unsupported until proven.
