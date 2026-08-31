# OpenOmada Native Agent

This directory contains a new production-oriented C++ implementation of the
Open Omada Device Agent. The Python implementation in the parent repository
remains the behavioral reference and must not be replaced until native parity is
proven through characterization tests and real controller/OpenWrt validation.

The target is constrained OpenWrt hardware first: musl libc, MIPS32/MIPSel,
ARMv7, AArch64, 16 MB flash where practical, and 32-64 MB RAM devices.

## Current Scope

Phase 1 is intentionally small:

- project/build skeleton;
- tiny CLI entry point;
- protocol/domain utility types;
- ECSP framing and constants;
- ECSP V2 auth primitives;
- characterization tests against Python-observed fixtures;
- OpenWrt package scaffolding.

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

