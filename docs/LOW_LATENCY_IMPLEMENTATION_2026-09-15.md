# Low-latency implementation — 2026-09-15

Base: PR #20, `9bf0f2a3772bff1982db55c09f37b9d87fbaa475`.
This is a scoped, source-level follow-up, not completion of the broader
low-latency RFC, the draft integration, or physical acceptance.

## Implemented

- Boot-local, monotonic DATA retry timing (`include/ninlil_retry.h`). Repeated
  Core calls at one timestamp cannot accelerate retries; delayed execution does
  not require replaying ticks. Old standalone `ninlil_step` clients keep their
  existing behavior unless they explicitly enable the new mode.
- Per-message RTO from the selected route, not the maximum RTO of every flow.
  The reference routed owner retains its conservative 1,000 ms floor. Link
  acceptance starts a 30,000 ms staging watchdog, not an ACK timer; confirmed
  source DATA TX_DONE starts the response timer. The 160 ms blocked backoff is
  now actual elapsed time. These reference values are candidates, not field SLOs.
- Actual AEAD-verified, read-only inspection of our outgoing frame identifies
  the source message for TX_DONE. It does not consume RX replay state or a new
  nonce, and does not create delivery evidence. The current reference port is
  synchronous; future asynchronous ports must correlate completion to the
  current offer, not deliver stale callbacks by message ID alone.
- Explicit known-not-sent airtime release. CCA/RX/post-TX BUSY and preparation
  failures refund only the current unused reservation, retain the staged job,
  and restore its DRR accounting. Ambiguous TX timeout/IO remains charged.
  Actual completion time, not scheduler-selection time, anchors post-TX pause.
- New, verified route PREPARED/APPLIED/RELEASED/EFFECTIVE_ACK progress makes the
  next control phase runnable without an unconditional two-second wait.
  Duplicate/stale replies do not reset the retry throttle. Existing identity,
  proof, lease, old-executor-release and activation checks are unchanged.
- Required-frame preflight: the reference secure protocol must carry 240 bytes.
  Both network pump openings check its airtime/budget before mutation; the
  reference node entry checks the PHY before opening durable node state. An
  incompatible profile returns an error, not repeated attempts that can never
  transmit. Existing 400 ms scheduler/frame cap and JP restrictions are retained.

## API and recovery

`ninlil_retry_enable` is called once after open/replay and before the first step.
After enabling, use `ninlil_step_at(runtime, monotonic_ms)`; legacy step calls are
rejected in that mode. `ninlil_retry_query` returns boot-local scheduling state
for an active message, never remote success. `ninlil_service_info.next_step` is
zero in this mode; use the retry query rather than interpreting a step number
as a time. A pause, authorization failure or staging-watchdog expiry does not
release durable ownership or manufacture an outcome.

Restart reconstructs durable messages and resets volatile timers. The existing
restart-safe absolute deadline clock remains separate. No wire format, journal
format, crypto tag length, key derivation, nonce reservation or durable receipt
semantics change. Build all static components together: outbound runtime state
changes and the public airtime scheduler layout advances to API version 4.

## Verification and limitations

Local verification of this source used GCC Debug (108/108 CTests), Clang
Debug with AddressSanitizer/UBSan and leak checks (108/108), both compiler
static analyses, and strict ESP32-S3 syntax against the pinned Semtech API.
These checks passed. An actual ESP-IDF target build is a separate remote gate.
New tests cover
monotonic/per-message retries on POSIX and raw-flash runtime backends, repeated
same-time calls, blocked links, staging expiry, restart identity, query failure,
known-not-sent refunds, DRR bypass, real completion pause, actual secure-frame
inspection, preflight rejection, and new versus duplicate route progress.
The prior snapshot passes 104 CTests; this candidate defines 108 CTests.

Native clocks, NOR and radios in tests are explicit fixtures. They do not
measure over-air delivery, ESP flash power interruption, 30-node capacity,
join time or a percentage improvement. No 500 ms / 1.5 s / 2.5 s field guarantee
is made. The existing integration has separate source-budget and release gates;
none are suppressed or increased here. The exact base already exceeds the
project budget (62,953/50,000), P0 budget (12,580/12,000), and autonomous-owner
budget (5,634/5,500). This patch adds explicitly counted sources; it does not
repair those baseline violations or authorize merging past them.

## Explicitly remaining

- Sparse, on-demand authentication and differential roster distribution.
- Separate PATH_USABLE from fully measured PATH_OPTIMIZED, including mixed
  versions and honest quality confidence. The eight-sample/full-size probe
  requirement is not silently reduced by this patch.
- Unified bounded DATA/receipt routing and authenticated return capabilities.
- Compact negotiated wire and bounded control fragmentation; no unreviewed
  abbreviation of identities, epochs or authentication tags.
- Fully asynchronous physical driver, adaptive contention and coordinated
  authorized local repair. The fixed random delay is not removed here.
- Representative startup/add/remove/relay-loss/battery-window and simultaneous
  event tests at 30 nodes, actual target builds and source-bound HIL acceptance.

These remaining protocol changes need separate review and acceptance. Passing
this patch's unit/model tests is not evidence that the earlier multi-minute
field delays have all been eliminated.
