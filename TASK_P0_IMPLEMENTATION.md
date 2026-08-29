# Pi implementation brief — P0 contracts

This branch is the autonomous implementation branch for Ninlil P0.

## Authority and order

Read and obey, in this order:

1. `AGENTS.md`
2. `docs/FOUNDATIONS.md`
3. `docs/P0_DELIVERY_CONTRACT_V2.md`
4. `docs/P0_OPERATIONAL_PROFILES_V1.md`
5. `docs/ARCHITECTURE.md`
6. `docs/ENGINEERING_STANDARD.md`
7. `docs/FAILURE_MODEL.md`
8. `docs/TESTING.md`
9. GitHub Issue #10
10. GitHub Issue #11

The current repository code is only a baseline. When code and the documents above conflict, the P0 documents are the implementation authority.

## Execution order

### Phase A — Issue #10 first

Implement Delivery Contract v2 completely before starting Issue #11:

- default durable success at `REMOTE_STORED`;
- per-message ownership, required evidence, and traffic class;
- receiver emits durable `REMOTE_STORED` receipt immediately after authoritative inbox commit;
- Application handoff remains at-least-once and separate from business results;
- remove the current APPLIED-only transport-success coupling;
- version public API, wire records, and journal records deliberately;
- add platform time/quality boundary before enforceable deadline behavior;
- implement precise `EXPIRED`, `FAILED`, `UNKNOWN`, and `CANCELLED` semantics only where evidence is sufficient;
- preserve stable message identity, dedupe, crash recovery, and fail-closed behavior.

Do not add product vocabulary, implicit request/response semantics, or a generic correlation field to the base radio header. Application correlation stays inside opaque payload for P0.

### Phase B — Issue #11 after Phase A is green

Implement the bounded operational contracts:

- typed role profiles and validated capacity/reserve limits;
- payload persistence separated from bounded RAM indexes;
- starvation-bounded weighted local scheduling (`8:4:3:1`, max 8 consecutive CRITICAL while another class waits);
- Host Adapter / Gateway / final target custody separation;
- idempotent replay across Host/Gateway reconnects;
- Battery Leaf uplink/poll/downlink opportunity contract;
- Host-side multi-Gateway dedupe and one active downlink parent with route epoch;
- reliable group operation expanded to explicit per-target logical deliveries at the Host;
- default-deny capability and bounded service-grant authorization before inbox mutation.

Relay data plane, EDHOC, Join protocol, production MAC, CAD/LBT policy, fragmentation, OTA, KGuard-specific semantics, and on-air reliable broadcast are out of scope.

## Engineering constraints

- C11 first-party implementation; preserve explicit platform boundaries.
- No hidden threads, tasks, sockets, global mutable state, or unbounded allocation in the portable core.
- Do not silently downgrade durable ownership, drop accepted messages, infer success, or convert missing ACK to failure.
- Treat persistent formats as protocols: version them, test corruption, torn writes, restart, full capacity, and incompatibility.
- Keep first-party implementation + tests + docs + build logic below the 50,000 nonblank-line hard limit. Prefer deletion and simplification over adding frameworks.
- Avoid compatibility scaffolding for unreleased formats unless a concrete accepted artifact requires it. Fail clearly on unsupported old versions.
- Do not weaken warning, sanitizer, static-analysis, or ESP-IDF gates to make code pass.
- Linux is the development environment. Do not introduce Windows-only tooling.

## Required workflow

1. Inspect the complete current implementation and tests before editing.
2. Write a concise implementation plan in the session and then execute it autonomously.
3. Work in small coherent commits on this branch only. Never force-push or modify `main`.
4. Run focused tests during development.
5. Run the full host matrix and ESP-IDF build before declaring completion.
6. After implementation, review the entire branch diff, looking specifically for durability lies, state-transition ambiguity, duplicate handling, bounds, parser mutation-before-validation, restart behavior, authorization bypass, memory growth, and misleading names.
7. Fix every discovered issue and rerun all gates.
8. Push the branch and open a draft PR against `main` with exact test evidence. Do not merge it.

## Minimum final gates

- GCC strict build and tests;
- Clang strict build and tests;
- GCC ASan/UBSan/leak tests;
- Clang ASan/UBSan/leak tests;
- GCC and Clang static analysis;
- formatter and `git diff --check`;
- LOC gate;
- ESP-IDF v6.0.2 ESP32-S3 configure/link;
- new restart, duplicate, receipt-loss, capacity-reserve, starvation, deadline/UNKNOWN, custody replay, sleeping Leaf, duplicate multi-Gateway reception, stale route epoch, group-wave, and unauthorized-service tests.

Do not report a gate as passed unless its command was actually executed successfully on the final commit.