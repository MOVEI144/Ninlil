# Project status

Updated: 2026-09-07

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

The P0 implementation candidate adds versioned per-message delivery evidence, immediate durable-store receipts, at-least-once Application handoff, restart-safe deadline boundaries, bounded role scheduling, default-deny service grants, and caller-backed Host custody/topology/group contracts. This is not accepted hardware or production-security evidence. See [`P0_IMPLEMENTATION.md`](P0_IMPLEMENTATION.md).

## Important non-claims

The official baseline does **not** yet contain a completed secure-link layer, EDHOC integration, Join protocol, multi-peer Gateway authority store, Relay, scheduled MAC, or fragmentation. Earlier conversational milestones do not become official implementation evidence unless their source and tests are imported and reviewed here.

## Acceptance state

- Host/model tests: candidate evidence exists; run the full applicable matrix locally.
- ESP-IDF configure/link: required local target-build gate under the current no-hosted-CI decision.
- Two-board RF: not accepted.
- Hard-power flash interruption: not accepted.
- Production security: not accepted.

The physical M1 procedure is now defined in
[`M1_HIL_ACCEPTANCE.md`](M1_HIL_ACCEPTANCE.md), with a separate
[`M1_HIL_EVIDENCE_TEMPLATE.md`](M1_HIL_EVIDENCE_TEMPLATE.md). Defining the
procedure does not complete any physical gate; Issue #7 remains the canonical
acceptance tracker.

## W01 / initial W02

The scoped [adaptive-network contract](ADAPTIVE_NETWORK_CONTRACT.md) and
[static direct-network simulator](SIMULATION.md) add a product-independent
one-to-one/one-to-many host baseline using the existing C Core and POSIX journals.
Loss, duplicates, receipt blackout, isolated peers, bounded capacity and process
restart are separate cases. The model's fixed calendar and packet staging are
not production firmware, automatic optimization, or physical acceptance.

The [2026-09-07 local evidence](W01_W02_LOCAL_EVIDENCE_2026-09-07.md) records
15 CTest entries passing in each of four compiler/sanitizer configurations,
reproducibility, static analysis and 10,000 manifest fuzz executions. Hosted CI,
the actual ESP-IDF link, physical RF and power-cut campaigns were not run for
this host-only slice.

Use repository source and recorded local evidence for the next decision.

## M1 preflight (2026-09-07)

The [local M1 preflight record](M1_PREFLIGHT_2026-09-07.md) records successful
ESP-IDF v6.0.2 default and TX-disabled Board A initialization builds.
The later [physical initialization record](M1_BOARD_INIT_2026-09-07.md) records
Board A identification, verified 8MB backup, erase, verified flash, a real
first-cycle SPI API failure, and its dedicated-bus HAL fix. The full local host
matrix and target build pass; Board A then completes 100 initialization cycles.
Board B is independently identified, backed up, flashed with node 2 / peer 1,
and also completes 100 initialization cycles. Each selected raw boot log retains
99 accounted-for shared GPIO ISR service messages. Both images keep RF TX
disabled and frequency unset. Both recorded USB identities and concurrent COM3 /
COM5 port access are confirmed at 17:30 JST. Two-board RF, hard-power recovery and overall M1
acceptance remain unrun/unaccepted.
