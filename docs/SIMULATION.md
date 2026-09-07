# Static direct-network simulation

This host tool runs the actual Ninlil C11 delivery Core with a POSIX durable
journal per node. It is the first W02 slice from
[ADAPTIVE_NETWORK_CONTRACT.md](ADAPTIVE_NETWORK_CONTRACT.md), not a simulator
for the complete adaptive-network Epic. All outputs are synthetic host evidence.

## Build and run locally

On Linux with the repository's GCC/Clang, CMake, Ninja and clang-format tools:

The Clang sanitizer/fuzzer runtime must also be installed (for Ubuntu 24.04's
Clang 18, `libclang-rt-18-dev`). Shell scripts and checked-in manifests use LF
through `.gitattributes`, including Windows checkouts mounted into local Docker.

```sh
cmake -S . -B .verify-sim -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .verify-sim --parallel 4
ctest --test-dir .verify-sim -R w02 --output-on-failure
.verify-sim/ninlil_sim tests/sim/manifests/recovery.txt
bash scripts/verify_sim.sh "$PWD/.verify-sim/ninlil_sim"
bash scripts/fuzz_sim.sh
```

The full local gate `bash scripts/ci.sh` includes the new targets, repeatability
and invalid-CLI checks in all four compiler/sanitizer configurations, parser fuzz,
format and static analysis. The existing firmware, protocol and runtime sources
are not staged or rewritten by these scripts. Hosted Actions need not run.

Each run uses a private temporary directory for real POSIX journals. Normal
completion removes it. No hardware access, RF transmission, credentials or
external service is needed. The process-crash regression uses `fork`/`_exit` and
reopens the same journals after the OS releases the child's file descriptors.
It does not emulate a filesystem cache loss or an interrupted physical Flash write.

Exit status 0 means all offered workload messages were admitted and reached their
requested evidence within the manifest latency target. Status 2 means an incomplete
or rejected workload/target miss; it is not a delivery `FAILED` state. Status 1
means invalid input, invariant violation or execution error. Do not mask status 2
with a shell pipeline when collecting evidence.

## Manifest v1

The format is strict `key=unsigned-decimal` with a terminating newline. Blank
lines and full-line `#` comments are allowed. There are at most 64 lines, each
shorter than 128 bytes including newline. All 17 fields are mandatory, unknown
and duplicate keys fail, and parsing commits no partial config on error. CRLF
and LF are accepted; signs, spaces around values, trailing text and integer
overflow are rejected.

| Field | Bound / meaning |
|---|---|
| version | Exactly 1 |
| seed | 1..4294967295, deterministic LCG fault/random-ID input, not cryptographic RNG |
| nodes | 2..5; node 1 parent, others Powered Endpoints |
| burst | 1..24 messages per direction per child; maximum 192 logical messages |
| payload_bytes | 4..52; includes test source/target/sequence markers |
| sf | 7..12 with fixed BW125 kHz, CR4/5, preamble8, explicit header, CRC |
| duration_ms | 10..600000, multiple of 10 |
| interval_ms | Multiple of 10; last burst release must precede duration; zero means simultaneous |
| latency_target_ms | 1..duration_ms, experiment target measured after durable admission |
| loss_permille | 0..1000, loss draw for each completed physical frame |
| duplicate_permille | 0..1000, an additional adapter RX copy of a surviving frame |
| offline_node | 0 disabled, otherwise 1..nodes |
| offline_start_ms / offline_end_ms | Both zero if disabled; otherwise start < end <= duration, multiples of 10 |
| restart_node / restart_ms | Both zero if disabled; otherwise valid node and 0 < time < duration, multiple of 10 |
| receipt_hold_ms | Drop receipts completing before this time, 0..duration, multiple of 10 |

An offline node pauses its Core and loses queued radio packets; in-flight frames
involving that node are marked lost even if it returns before the frame ends.
A runtime restart closes/reopens its journal, discards radio queues and reoffers
already admitted requests with identical idempotency keys. The returned logical
IDs must match. A separately tested process exit bypasses runtime shutdown.
Workload releases while a source is offline are deferred and appear as not offered
if it never returns; this does not pretend that an unpowered Application submitted.

## Radio and execution model

Time advances in 10 ms ticks. Every online Core gets a bounded step with eight
work items and retry interval 100 steps. No hidden wall-clock sleep drives the
simulation. Journal calls are real synchronous operations; their wall-clock time
is not charged to virtual time. Measured storage and driver timing must be added
through W03/W04 calibration before interpreting latency as physical performance.

Each node has an eight-packet FIFO TX staging queue, eight RX slots and the
existing role-profile limits for durable ownership. Byte-identical staged retries
coalesce into the same pending volatile obligation; differing bytes do not.
The calendar gives each node one maximum-frame slot in address order. A slot is
maximum DATA airtime rounded up to a 10 ms tick plus one 10 ms guard. A frame
must fit entirely in its slot. Idle/offline slots are not reassigned. Only one
physical frame is in flight, so a node cannot receive while transmitting.
Receiver delivery occurs on the first tick at or after frame completion.

DATA and receipts consume their actual encoded lengths. TX staging acceptance
does not increment physical transmission counters; transmission starts do. Loss
still consumes airtime. Duplicate injection models an adapter delivering a frame
twice and does not claim a second free on-air transmission. RX overflow is a
counted drop. The RNG takes two fixed draws per completed frame, including faults.

The airtime calculation is a restricted integer evaluation checked against the
[pinned Semtech driver](https://github.com/Lora-net/sx126x_driver/blob/a10c5dfdf89788c6ac805e9fe98889de44175aa2/src/sx126x.c).
For the fixed settings above, LDRO is enabled at SF11/12. Golden vectors in
`test_sim_model` are 158976/61696 us at SF7, 513024/205824 us at SF9, and
3776512/1646592 us at SF12 for 92-byte DATA / 26-byte receipt respectively.
These are nominal airtime values, not a regulatory profile or RF measurements.
The dependency pin is unchanged and no driver source is copied into the simulator.

The staged calendar is a reference policy, not the current ESP32 radio adapter.
It does not model CAD/LBT, collisions, hidden terminals, clock drift, sleep, RF
capture, asymmetric channel quality, secure sessions, Join, Relay or optimization.
It provides neither a radio-on energy model nor Flash-wear estimates. Those are
explicit later slices, not assumed zero-cost implemented features.

## Reports and assertions

Reports reproduce all manifest fields and the model version. Every logical
request gets a row with source, target, sequence, admission, outcome, offer count,
admission time, first receiver offer, sender evidence time, latency, censored age
and stable message ID. `NA` means no observation; active messages do not acquire
zero-latency success. First receiver offer is a sampled upper bound on inbox
commit, while sender evidence time is sampled after the actual Core query.

Summary counts retain admission rejection, active/censored work and target misses.
The maximum latency is explicitly for completed messages; per-message rows retain
the unsuccessful population. Airtime sums include the full nominal airtime of
frames started in the observation window, including a frame still in flight at
the end. TX/RX queue peaks, coalesced retries, BUSY, loss and duplicate counts are
reported. These are simulator observations and not MCU memory measurements.

The test Application inspects generic payload bytes to detect cross-peer/ID mixups.
The Core does not interpret them. It never records Application acceptance, so
default store evidence must complete independently. A second offer in the same
boot is an error; reoffer after restart is permitted. Unexpected terminal outcomes,
regressing evidence, wrong peer/payload and changed replay IDs fail immediately.

## Included acceptance scenarios

| Test | Observable condition |
|---|---|
| direct.txt | Two nodes, four messages each way; all eight store-evidence outcomes |
| star.txt | Parent plus four children, simultaneous 32 messages; per-target completion |
| recovery.txt | Seed 42, 10% loss, 25% duplicate injection, child 5 offline 1..60 s, parent reopen at 5 s, receipts held to 10 s |
| multi-seed fault regression | Seeds 7, 42, 20260907; parent or child reopen; 24 healthy deliveries complete while isolated work remains active |
| receipt blackout | All 32 receiver offers occur, all 32 sender outcomes stay ACTIVE; CLI exits 2 |
| capacity pressure | 192 offered, 160 admitted, 32 explicit rejections; all admitted deliveries survive and complete |
| periodic SF12 | Two nodes, spaced releases, longest supported frame airtime counted |
| abrupt process exit | Child commits communication then exits without close; all admitted requests recover with original IDs |
| model boundaries | Early RX prohibited, single in-flight frame, mid-frame offline loss, bounded queues, malformed/unauthorized packets |
| parser/CLI | Invalid input rejected without runtime creation, transactional parse, seeded mutation fuzz |
| repeatability | Each checked-in workload produces byte-identical full reports twice |

The contract does not promise that every arbitrary admitted manifest passes its
latency target. Failing workloads remain useful evidence. Use the same manifests,
seeds, model version and evaluation conditions for future policy comparisons;
do not raise thresholds after observing a worse result.

## Handoff to M1

Use [M1_HIL_ACCEPTANCE.md](M1_HIL_ACCEPTANCE.md) and its separate evidence
template. Keep the real test at fixed A/B initially. The simulator is preparation
for that work and a regression tool; it does not replace ESP-IDF builds, RF-path
polarity checks, clean firmware/board identities, serial logs, USB/reboot tests,
or hard-power Flash campaigns. A successful simulation does not close Issue #7.
