# Testing

## Test levels

- Pure unit tests for state machines and codecs.
- Storage integration tests for commit, replay, corruption, and capacity.
- Restart tests that bypass clean shutdown.
- Property and fuzz tests for parsers and idempotency.
- Fake-hardware tests for ESP32/SX1262 ownership and deadlines.
- Simulator tests for loss, duplicate, reorder, and saturation.
- Hardware-in-the-loop tests for RF, USB, reboot, and hard power cuts.

## Rules

- Do not use arbitrary sleeps as completion assertions.
- Randomized tests print or preserve their seed.
- A flaky test is a defect, not an accepted state.
- Coverage percentage is not an acceptance criterion by itself.
- Every milestone distinguishes host/model gates from physical gates.
- P0 host tests cover store-before-receipt, restart handoff, duplicate, lost, pre-attempt, and reordered receipts, required-evidence-based terminal suppression and replay, archive pressure, saturated receive/outbound phases, three-class receipt progress across repeated exact bound windows, per-class entry rotation, policy-error classification, durable permanent-rejection tombstone replay/conflict/capacity/corruption, CRITICAL preservation of CONTROL reserve across restart, starvation, custody replay including zero-byte tokens, Leaf windows including full endpoint overflow at `UINT64_MAX`, multi-Gateway dedupe and replay overlap, route epochs, group waves, capability-bitset forward compatibility, and authorization-before-inbox.
- Deadline matrices cover absent, failed, invalid, unavailable, runtime-only, before-deadline, and proven-expired clocks; unattempted local expiry; attempted ambiguity, retry suppression, and explicit `UNKNOWN` across restart; unrelated traffic progress; invalid `APPLICATION_ACCEPTED` deadline contracts at API/wire/replay; live and replayed `EXPIRED` validation; and matching stored duplicate receipt recovery without a clock or after the deadline.
- Referenced-read corruption tests mutate committed header, body, checksum, raw-Flash padding, commit state, and trailer after open. Direct POSIX and raw-Flash reads must fail before copying payload; runtime integration must prevent outbound Link send and inbound Application handoff; committed mutations must fail reopen where the format classifies them as corruption.
- Traffic-class tests exercise values below `CRITICAL` and above `BULK`, require `NINLIL_ERR_INVALID` before journal mutation, and run under both sanitizer matrices.
- Test evidence records exact commit and tool versions.

The default host gate is `./scripts/ci.sh`. The standard ESP32 target is built separately with ESP-IDF v6.0.2.
