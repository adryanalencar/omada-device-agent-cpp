# OpenOmada Native Agent

This directory contains a new production-oriented C++ implementation of the
Open Omada Device Agent. The Python implementation in the parent repository
remains the behavioral reference and must not be replaced until native parity is
proven through characterization tests and real controller/OpenWrt validation.

The target is constrained OpenWrt hardware first: musl libc, MIPS32/MIPSel,
ARMv7, AArch64, 16 MB flash where practical, and 32-64 MB RAM devices.

## Current Scope

Phase 3 is still an incremental port, not a replacement daemon:

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
- characterization tests against Python-observed fixtures;
- OpenWrt package scaffolding.

The lifecycle code can build and serialize discovery, PRE_CONNECT,
DEVICE_VERIFY, SYSTEM_VERIFY, DEVICE_NEGOTIATION and INIT_SYNC messages, and
tests verify the happy path plus auth failure cases against scripted controller
frames. The Phase 3 code also persists reconnect metadata, schedules the first
reply-requested inform and later fire-and-forget informs, and models direct
reconnect exhaustion before managed rediscovery. It is not yet wired into a
long-running daemon.

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
