# 2026-09-10 radio feedback integration — scoped evidence

Base: `4357286cf3cff58e9b3413eef45412dc28c4771e` (PR #18).

## Executed, with an explicit test boundary

GCC 14.2.0 and Clang 17.0.0, normal and ASan/UBSan: each passes 11 component
CTest entries and two separate actual-node/pump executable modes (explicit API
and compile-time default). One CTest entry includes 13 Python seven-board runner
fixtures. Sanitizer runs enable leak detection and stop on errors.
The six main component sources include the new feedback state machine. The
integration executables additionally compile the actual node IO/radio/link/sleep,
network pump and feedback adapter sources. The IO ingress is real source; the
cryptographic verification backend is a named double, NOT actual EDHOC/AEAD.

This environment could not obtain a complete checkout. It uses reconstructed
base declarations (`ninlil.h`) and explicitly sliced declaration headers for the
isolated integration executables. They are NOT SDK headers, a production layout
or ABI validation. Other Core, clock, authority and physical-driver boundaries
are also named doubles. The complete-checkout CMake targets use actual headers,
but that configuration has not been built here. The downloadable verification
bundle carries the exact fixture headers, source subset, commands and raw logs;
those fixtures must never be copied into a production checkout.

Tests cover physical setting confirmation rather than staging success, three
disjoint windows, direct-authenticated ingress only, no observation after the
simulated authentication failure, no use of E2E/control replies as direct RF
samples, queue coalescing timestamps, BUSY retries, sleep/rekey invalidation,
bootstrap at approved max, failed setting stage and unexpected applied output.
The seven-board runner checks optional power_mode against artifacts and actual
T replies; a mismatch fails before application submissions and stops owners.
These are synthetic tests, not evidence from seven connected boards.

GCC -fanalyzer and Clang --analyze also inspect 11 translation units each under
the same isolated declarations: 22 zero exits, no diagnostic output. This is not
the repository-wide analyzer/fuzzer gate or an independent cryptographic audit.
`results.json` records source hashes, verified original Git blob hashes, exact
compiler versions, scope, return codes and hashes of the four raw reports.

## Reproduction and prior unsuccessful attempts

In a complete checkout, run the existing full SDK verification and the new
`radio_feedback_pump_explicit` / `radio_feedback_pump_default` CTest targets.
A portable component-only entry remains `tests/adaptive/CMakeLists.txt`.
The isolated bundle has `run_stage2_verification.py` with explicit root/fixture/
output arguments and --compiler gcc or clang; --static runs its scoped analyzer.
The runner rejects changed source hashes and preserves separate output folders.

An early component configure failed because the local subset lacked the unchanged
baseline test_airtime.c; the exact Git blob was retrieved/reconstructed and checked.
An early IO integration test expected UNAUTHORIZED from a former frame-current
double. The actual probe wrapper maps that failure to STATE. Its assertion was
corrected to the real existing contract; the no-physical-send assertion remains.
No production authorization check was weakened to obtain a passing result.
The final runs are gcc-run-002 / clang-run-002; analyzer sources are identical to
gcc-static-001 / clang-static-001. Older attempts are not relabeled as final PASS.

## Not run / not claimed

Full SDK/type/ABI/package/fuzz/format/50,000-line gates, ESP-IDF, seven-board HIL,
electrical power cuts, current, range, long-term and field acceptance remain
NOT_RUN. No physical board was flashed, erased or started. Hosted Actions stay
skipped. Remote queue_us reports, joint routing, PHY negotiation and durable
fanout integration remain outside this increment. See
[the integration record](../../RADIO_FEEDBACK_INTEGRATION_2026-09-10.md).
