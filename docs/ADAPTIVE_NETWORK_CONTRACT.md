# Adaptive network contract — initial static-direct slice

Status: historical selected scope for W01 and the first W02 implementation,
2026-09-07. The later explicit user decision authorizes steps 4-6 software
implementation in [the secure network profile](SECURE_NETWORK_PROFILE_2026-09-07.md).
The initial simulator described here keeps its original 92-byte/fixed baseline;
new secure/Relay/Coordinator behavior is specified and evidenced separately.
Source: [Issue #14](https://github.com/MOVEI144/Ninlil/issues/14), with
[FOUNDATIONS](FOUNDATIONS.md), [P0 delivery](P0_DELIVERY_CONTRACT_V2.md),
[operational profiles](P0_OPERATIONAL_PROFILES_V1.md), and the
[implemented P0 contract](P0_IMPLEMENTATION.md) retained.

## Purpose

Ninlil must remain usable without any particular product's repository, cloud,
database, UI, or operating system. This slice establishes an executable baseline
for one-to-one and one-to-many delivery before two-board M1 measurements.
It does not adopt every algorithm proposed in #14 or declare W01–W12 complete.

The decisions below are the narrow implementation authority for the new host
simulator. They do not supersede production security or physical acceptance gates.

## D01 — Responsibilities

| Responsibility | Owner | Explicit boundary |
|---|---|---|
| Opaque payload meaning, physical effects, user approval | Product/Application adapter | No product semantics in Core or Wire |
| Durable admission, inbox/outbox, retries, deduplication, delivery evidence | Existing portable C11 Core | No radio timing or topology optimizer |
| Participation and service permission | Authority supplied through typed policy boundary | Discovery, signal strength and routing never grant permission |
| Membership/session/route/plan protocol state | Future Network Control modules | Bounded state, validated input, commit before effect |
| Observation aggregation, capacity planning, route and airtime proposals | Future reusable Ninlil Coordinator | No mandatory Linux, PC, cloud, language, or product database |
| Packet staging, TX/RX, clock, persistence | Explicit bearer/platform adapters | Radio ports do not interpret business payloads |

Coordinator and Authority are logical roles, not required separate processes.
The initial simulator has synthetic pre-authorized direct peers and a fixed
calendar, not a working Coordinator or an authentication service. Parent node 1
uses the existing Gateway resource profile; all other nodes use Powered Endpoint.
No new generic repositories, plugin frameworks, or device-management system.

## D02 — Identity and topology

Keep cryptographic peer identity, replaceable short node address, membership
epoch, attachment/binding epoch, ephemeral session, route epoch, plan epoch,
and logical message ID separate. A route or radio plan change does not change
membership, restore an old session, replace the message ID, or modify an accepted
message's required evidence. An epoch increment is not proof that an old physical
transmitter stopped; existing release/expiry requirements remain in force.

This slice uses fixed addresses 1..N with N in 2..5, one parent, and direct
parent/child unicast only. Parent-to-all means distinct deliveries to explicit
targets; there is no broadcast success or invented aggregate outcome. A child
cannot send directly to another child. Unknown peers and invalid addresses are
rejected before staging. No address reuse or dynamic Join is enabled.

## D03 — Time and success

| Time | Meaning | Initial implementation |
|---|---|---|
| Runtime monotonic time | Retry/service opportunities during a boot | Synthetic 10 ms ticks, existing step-based retry counter |
| Network schedule time | Agreed physical TX/RX opportunities, including clock uncertainty | Ideal shared time for the fixed simulation only |
| Restart-safe absolute time | Validity/expiry of deadlines and leases across reboot | Not supplied by this simulator |

The simulation clock reports `RUNTIME_ONLY`, even though the harness itself can
remember time across a runtime reopen. It must not authorize an absolute deadline
or lease by pretending to be restart-safe. Workload latency targets measure an
experiment; they do not become message deadlines or terminal outcomes.

`submit == OK` proves local journal ownership only. Link staging and on-air TX
are separate diagnostics. Receiver Application offer proves an inbox was stored,
but its sampled time is an upper bound on commit time, not an exact storage trace.
Sender `SATISFIED` requires its declared end-to-end evidence to be committed.
The initial workload requests `REMOTE_STORED` and deliberately never calls
Application acceptance. A receiver reboot may offer the same message again;
a duplicate during one boot must not create another Application offer.

A missing receipt leaves the sender `ACTIVE`. Ending the experiment or missing
a latency target must not call cancel, mark unknown, expire, or fail on its behalf.
The report distinguishes rejected, not yet offered, active, satisfied, observed
receiver offers, and missing observations. No physical-effect success is inferred.

## D04 — Bounded workload and failure handling

The checked workload manifest is versioned independently of the runtime API.
It specifies seed, node count, burst size, interval, payload size, PHY selection,
duration, latency target, packet loss/duplication, offline interval, runtime reopen,
and receipt suppression. All fields are mandatory; unknown, duplicate, missing,
overflowing, or inconsistent fields are rejected before opening a runtime.
Exact bounds and units are in [SIMULATION.md](SIMULATION.md).

Per-node journals remain authoritative; the test owns a bounded inventory of
expected messages only to assert identities, contents, and reported evidence.
Each message is offered once by the workload generator. Capacity rejection is
counted, not silently retried until a flattering success rate results. Accepted
messages remain owned by the actual C Core and its journal.

The baseline bearer uses eight staged packets per node, eight received packets,
one physical in-flight frame across the conservative shared channel, FIFO staging,
and byte-identical queued-retry coalescing. Staging can return BUSY. Overflow of
the receiver queue is counted as packet loss, not hidden by an infinite buffer.
Neither coalescing nor TX completion manufactures receipts. Loss of volatile
queues across disconnect/restart is recoverable only through Core/journal retry.

These are explicit simulator choices. The current SX1262 adapter has different
buffering/timing; passing this baseline does not certify its many-peer fairness.
The exploratory one-buffer, phase-locked BUSY polling configuration exposed
starvation under persistent receipt loss. Do not generalize Core work-selection
bounds to wall-clock or airtime guarantees. W07 must compare the actual adapter,
CPU stepping, staging limits and receipt/data scheduling under sustained load.

The fixed calendar deliberately excludes simultaneous transmission. It is a
conservative comparison policy, not a collision model, legal radio profile,
production MAC, or proof of optimum performance. Hidden terminals, external
interference, asymmetric links, sleeping nodes and drifting clocks require later
channel/plan models and measured calibration.

## D05 — Compatibility and migration

No public ABI, persisted config, Wire, Journal, or role-profile format changes
in this slice. Existing versions remain API 2, Wire 2, POSIX envelope NJL4/v4,
delivery record v5 and raw-Flash envelope v5. Simulator manifest v1 is test-only.
The new executable links the same `ninlil_posix` implementation as the existing
tests. No delivery protocol is rewritten for simulation and no firmware path is
silently switched to the simulator policy.

Future control/crypto/Relay overhead must fit an explicit Wire budget: the current
92-byte radio MTU minus the 40-byte DATA header leaves 52 bytes before additional
security/control overhead. Do not shrink authentication or silently fragment to
fit. Each proposed change must declare its API/Wire/Journal/Profile version,
mixed-version behavior, consumer-first rollout, and unsupported-version rejection.

## D06 — Remaining Issue ownership and entry conditions

| Work | Scope retained or assigned | Condition before enabling it |
|---|---|---|
| W02 now | Static direct workload, actual Core, fixed airtime baseline, reproducible faults | Host tests and explicit model limitations |
| W03 | Exact enqueue/TX/RX/commit provenance, cause-specific metrics | Bounded observation contract; no secret or payload logging |
| W04 / #7 | Two-board timing, RF, USB/restart and delivery-journal power cuts | Local host/target checks plus identified boards and reviewed RF profile |
| W05 / #4 | Secure-link envelope, replay and crypto backend | Selected/pinned reviewed crypto and exact Wire budget |
| W05/W06 / #8 | EDHOC integration and handoff to session/membership owners | Avoid duplicating #4 envelope or #5 membership state machines |
| W06 / #5 | Join, revoke, membership commit and fresh-session resume | Explicit generic Authority decisions, crash/stale-epoch tests |
| W07/W08 | Airtime scheduling, capacity admission, Coordinator, plan transactions | Compare to measured fixed baseline; model clock/lease/partial change |
| W09/W10 / #6 | Powered Relay, controlled repair, planned removal and drain | Existing direct/security/Join gates; keep final-source copy until end evidence |
| W11 | Retention, retirement and GC | Model stale messages and interrupted retirement before changing P0 formats |
| W12 | Multi-Gateway/group optimization and reusable examples | Basic unicast and profile-specific capacity evidence |

#8 currently excludes fragmentation. Any minimum fragmentation required by the
chosen EDHOC exchange needs a separate accepted scope/version decision before
implementation; general bulk transfer/OTA is not implied. Issue #3 is not a second
M1 tracker. #10/#11 remain existing P0 implementation work, not work to redo.

Dynamic plan staging/reconciliation, clock-quality rejection, lease fencing,
revocation versus planned removal, retirement ranges, and recovery profiles remain
future protocol decisions. Their algorithms and public APIs are not frozen by this
document. Model checks for their partial-commit boundaries precede enablement.

## Invariant-to-evidence map for this slice

| #14 invariant | Present evidence or explicit deferred gate |
|---|---|
| I01 product independence | Standalone C executable, generic payload, no KG imports |
| I02 identity/authority/route separation | D02; existing policy/topology tests; dynamic Join/route deferred |
| I03 separate evidence | Missing-receipt campaign plus store-only success checks |
| I04 no silent loss of owned data | Capacity campaign: accepted survivors complete; rejected count retained |
| I05 no success/failure from silence | Receipt blackout: 32 ACTIVE even after receiver offers |
| I06 stable message contract | Reopen and abrupt-exit tests reoffer identical keys and require original IDs |
| I07 one-radio limits | Model tests: not receivable before airtime ends; staged concurrent work does not transmit concurrently |
| I08 hard constraints | Manifest/peer/MTU/queue bounds; actual crypto/legal/power gates deferred |
| I09 commit before effects | Actual Core/journal; existing crash/corruption tests; abrupt process-exit campaign |
| I10 logical versus physical fencing | D02 and existing topology lease tests; multi-controller physical gate deferred |
| I11 at-least-once handoff | Per-boot offer check; receiver/parent reopen; no Application acceptance inference |
| I12 finite resources | Manifest bounds, queue boundary+1 tests, strict compilers/sanitizers, parser fuzz |
| I13 stop/restart preserves records | Disconnect and restart campaigns; actual Coordinator shutdown deferred |
| I14 distinct evidence levels | Host-only reports; M1/production security/field acceptance remain pending |

## Ready to start the two-board work

Completion of this slice means the contract, simulator, regression tests and
local evidence are reviewable. It does not erase or flash a board. Follow
[M1_HIL_ACCEPTANCE.md](M1_HIL_ACCEPTANCE.md): pin the source revision, run the
local ESP-IDF v6.0.2 build, identify A/B and their USB connections, establish the
RF profile/antenna/GPIO38 polarity, and preserve hashes before physical work.
Local host tests, target builds, RF results, and real power-cut evidence stay
separate. Hosted CI is not run under the current user decision.
