# Project status

Updated: 2026-08-23

## Canonical repository

`MOVEI144/Ninlil` is the implementation authority. Earlier repositories and generated delivery archives are provenance inputs only.

## Imported baseline

The first review branch imports a compact C11 baseline containing:

- POSIX durable delivery and restart recovery;
- a bounded direct-radio model;
- ESP32-S3/SX1262 HAL and physical-radio software;
- a raw-flash delivery journal;
- fail-closed persistent security-counter and membership stores;
- host, fault-injection, and fake-hardware tests.

## P0 implementation candidate

The P0 branch adds versioned per-message delivery evidence, immediate durable-store receipts, at-least-once Application handoff, restart-safe deadline boundaries, bounded role scheduling, default-deny service grants, and caller-backed Host custody/topology/group contracts. This is an implementation candidate, not accepted hardware or production-security evidence. See [`P0_IMPLEMENTATION.md`](P0_IMPLEMENTATION.md).

## Important non-claims

The official baseline does **not** yet contain a completed secure-link layer, EDHOC integration, Join protocol, multi-peer Gateway authority store, Relay, scheduled MAC, or fragmentation. Earlier conversational milestones do not become official implementation evidence unless their source and tests are imported and reviewed here.

## Acceptance state

- Host/model tests: candidate evidence exists and is re-run by repository CI.
- ESP-IDF configure/link: required remote CI gate.
- Two-board RF: not accepted.
- Hard-power flash interruption: not accepted.
- Production security: not accepted.

The next decision is based on repository commits and CI evidence, not on generated reports alone.
