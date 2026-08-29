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
- P0 host tests cover store-before-receipt, restart handoff, duplicate, lost, pre-attempt, and reordered receipts, archive pressure, saturated receive/outbound phases, three-class receipt progress across repeated exact bound windows, per-class entry rotation, policy-error classification, reserves and starvation, deadline uncertainty, custody replay, Leaf windows, multi-Gateway dedupe, route epochs, group waves, and authorization-before-inbox.
- Test evidence records exact commit and tool versions.

The default host gate is `./scripts/ci.sh`. The standard ESP32 target is built separately with ESP-IDF v6.0.2.
