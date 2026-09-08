# Project status

Updated: 2026-09-08

## Latest extension: storage, bulk and transmit power

The subsequent user request adds automatic journal collection, a resumable
64 KiB bulk profile and bounded automatic TX power adjustment; OTA installation
and automatic firmware updates are excluded. See
[the extension contract and integration guide](STORAGE_BULK_RADIO_2026-09-08.md).
Core/control/Relay ownership survives collection, with POSIX publication and
NOR bank selection fault tests. Bulk adopts complete SHA-256-verified bytes,
preserves progress, and shares the existing priority scheduler. Power changes
use authenticated measurements, hysteresis and the provisioned maximum.

Local compiler/sanitizer, static, fuzz, package and target-build gates pass.
The added application regression covers authentication not being ready: it
pauses the transfer while the node continues its control work. The actual
three-board transfer retained 80 bytes across a receiver MCU reset and completed
the same 512-byte object across resumed campaigns, including full readback,
SHA-256, sender completion and collection after completion. Earlier deadlines
failed at 120/240/440 bytes and remain failures. Measured RF power stayed at
-3 dBm; automatic downward adjustment was not observed under these conditions.
See [local checks](EXTENSION_LOCAL_RESULTS_2026-09-08.json) and
[hardware results](EXTENSION_HIL_RESULTS_2026-09-08.json). Slow physical transfer,
full-size RF, electrical write-interruption and field qualification remain open.

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

## Current steps 4-6 software implementation

The September 8 review fixes and autonomous-owner integration are implemented.
Three-board application delivery, actual Relay/receiver reset recovery, and
Relay drain/removal readiness pass with preserved console evidence. Stable
identity, storage binding, lease synchronization and application commits are
integrated. Earlier Root/source-reset campaigns retained the message and receiver
commit but missed their receipt deadline; campaign 24 recovered that same receipt
without duplicate application records. Campaign 26 separately passed a fresh
Root/source reset through source application acceptance, with receiver growth 6→7.
The final proof-isolation source passes 45 tests in four local compiler/sanitizer
configurations, 711 vendor tests, 20,000 fuzz executions and package/build gates. See
[the current checkpoint](OSS_COMPLETION_2026-09-08.md). Results below belong
to their dated revisions and do not constitute final acceptance of these edits.

At its September 7 revision, the [secure network profile](SECURE_NETWORK_PROFILE_2026-09-07.md) implemented
EDHOC/AES-CCM, committed Join and membership, secure many-peer delivery, powered
Relay custody/recovery/removal, and fixed-profile route/retry/airtime adjustment.
The actual C modules run together in two host examples and have an ESP-IDF
component/pump integration. See the [local evidence](SECURE_NETWORK_LOCAL_EVIDENCE_2026-09-07.md)
for exact final results: 108/108 local CTest runs, 711/711 adapted upstream tests,
20,000 fuzz executions, static/syntax checks and a TX-disabled ESP32-S3 build
pass. These conversation steps do not mean roadmap M6 bulk/OTA.

On September 8 the user reconnected both boards and explicitly resumed feasible
two-board testing. The [secure HIL campaign](SECURE_TWO_BOARD_HIL_2026-09-08.md)
passed mutual EDHOC, encrypted Join/Flash replay, 100 encrypted test frames each
way plus 20 each way after actual MCU reset, 240-byte RF boundaries, all-byte
tamper/replay/rekey rejection, and fragmented-control failures. Independent
capture reconciliation matched 556 physical RF frames. Both original full Flash
images were restored and 100 TX-disabled initialization cycles passed per board.

A subsequent [three-board Relay campaign](THREE_BOARD_RELAY_HIL_2026-09-08.md)
passed fixed-path encrypted forwarding, actual Flash custody and Relay reset
recovery, duplicate/loss/invalid-ACK handling, 8-slot capacity backpressure and
local drain readiness. Independent analysis matched 155 RF frames and 31
source-owned/host-observed test packets. No new backups were made as requested;
protected data checksums matched on all boards and all finish TX-disabled.

These are USB-owned library/RF integration results, not autonomous product pump
or MCU Core/application receipt evidence. Automatic route failover, Coordinator
dependency removal, product integration, controlled power interruption, field
behavior and production security remain unaccepted. Earlier plaintext tests
remain separate dated evidence.

## Acceptance state of the earlier durable HIL baseline

- Host/model tests: all 16 tests pass in four local compiler/sanitizer configurations on the durable HIL source; hosted CI not run.
- ESP-IDF configure/link: local v6.0.2 builds pass for diagnostic roles and six durable/recovery variants.
- Two-board RF: diagnostic exchange passes 1,000/1,000 and durable delivery passes 100/100 in each direction. Completed-state replay and sender-pending reset recovery pass; overall M1 acceptance remains pending.
- Hard-power flash interruption: not accepted.
- USB-only supply removal/reconnect: pending data automatically recovers through 100 remote-stored messages; active-execution cut timing and early physical boot/reset-reason evidence remain unverified.
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

## M1 diagnostic RF (2026-09-07, later update)

The [two-board RF record](M1_RF_CAMPAIGN_2026-09-07.md) supersedes the earlier
RF-unrun status for diagnostic exchange only. Both certified-module markings
were confirmed by the user, GPIO38 polarity was checked against Seeed's example,
and the selected JP lab profile used 921.4 MHz, -9 dBm, SF7/BW125 with bounded
channel sensing and transmit pauses. Status/RSSI interpretation errors were
observed on hardware, corrected against the pinned driver, and retested.

On source `834c62f6b58257f3d116e27937c8803731623805`, each direction completed
1,000 matching PING/PONG exchanges with zero missing/duplicate sequences,
timeouts, or unexpected resets. Both complete serial logs, image identities,
and [structured results](M1_RF_RESULTS_2026-09-07.json) are recorded.
At completion both boards were verified back on TX-disabled initialization
images, so power cycling does not restart the diagnostic transmissions.

This is remote-receive evidence, not remote durable storage. Durable delivery,
restart/reconnect recovery, fault injection, controlled power interruption,
formal antenna combination verification, and overall M1 acceptance remain open.
Issue #7 was inspected. Neither the Issue nor GitHub Project was modified.

## M1 durable delivery and reset recovery (2026-09-07, 19:20 JST)

The [durable campaign](M1_DURABLE_CAMPAIGN_2026-09-07.md) supersedes the earlier
durable-unrun status. Source `80ab9acd444b577fc687bf6dbcddbf7e13ec2fa3` adds
campaign-bound HIL payloads/keys, per-message evidence logs, a read-only Flash
inspection tool, and a 100-message Flash-file restart test. Runtime and journal
formats are unchanged.

Both directions pass 100 distinct remote-stored deliveries. Raw device Flash
snapshots agree with both sides' logs and the sender's SATISFIED outcomes.
Restarting both boards preserves all 100 IDs and byte-identical journals, with
zero retransmission or consumer re-offer. A separate run resets A after a
committed attempt while B cannot respond; the pending ID survives and delivery
resumes through 100 stored messages. The initial pending-capture script confused
initialization PASS with delivery PASS; its preserved log and post-reset Flash
snapshot were reconciled before accepting the recovery result.

[Structured evidence](M1_DURABLE_RESULTS_2026-09-07.json) records image and log
hashes, each Flash snapshot, replay comparisons, and final device state. Both
boards are verified back on TX-disabled initialization images, retaining the
delivery journal. Physical USB disconnect/reconnect, receiver receipt-loss reset,
physical fault injection, and controlled hard-power cuts remain unrun.
Issue #7 and overall M1 acceptance remain open; no hosted CI or GitHub mutation.

## USB supply removal and automatic recovery (2026-09-07, 19:59 JST)

The user confirmed USB-only power and physically disconnected/reconnected both
boards. The [USB record](M1_USB_POWER_2026-09-07.md) and
[structured evidence](M1_USB_POWER_RESULTS_2026-09-07.json) show unchanged USB
identities, recovery of the pending message ID, and 100 matching remote-stored
messages verified in both Flash journals. After reconnect, no host reset was
needed for delivery to resume. Both boards are back on TX-disabled images.

The initial monitor timed out and reset A into its loader before physical
unplug. The follow-up captured both boards absent and their automatic return;
early physical boot/reset-reason logs were not captured. This proves retained
pending-work recovery across USB supply removal, not a cut during active
execution or Flash programming, and does not close the entire canonical USB
phase. The user has no controlled power-cut fixture, so precise Flash power
interruption remains unrun. Firmware source remains the verified `80ab9ac`.

## Two-board fault tests and full local verification (2026-09-07, 20:36 JST)

[Fault campaign evidence](M1_FAULT_CAMPAIGN_2026-09-07.md) and its
[structured results](M1_FAULT_RESULTS_2026-09-07.json) record 64/64 local CTests,
static/syntax/format/reproducibility checks and 10,000 seeded fuzz runs.
Default-off test firmware 3254ae3 passes 100 messages each way with software
DATA/receipt loss and duplication, malformed/wrong-target frames, an actual
masked-interrupt TX timeout, and receiver restart after durable consumption
before its receipt. Both journals and every message ID were reconciled.
Completed reboot preserves byte-identical journals and does not resend.

A real-Flash committed-byte corruption test on prior 80ab9ac firmware fails
Runtime open before delivery, restores the original journal and replays the
original 100 IDs without retransmission. Both boards are back on verified
TX-disabled source 8e8a6ea after 100 init cycles each. Setup failures are
preserved, including refusal to flash before final local PASS. No hosted CI
or GitHub mutation occurred.

These are scoped bench passes. Electrical Flash power-cut timing, physical
CRC/BUSY faults, full host-machine restart, RF payload/field/environment
sweeps and full M1 acceptance remain open. Software-injected RX faults are
not claimed as physical RF interference. The absent power fixture does not
block ordinary implementation or the tests completed here.

A follow-up monitor-process restart also passes 100 messages: A remains running
with a committed pending ID while one Python monitor exits and another opens
the USB console without resetting A. The same ID reaches remote storage and
all journals reconcile. The pre/post log interval is explicitly unobserved for
TX-only log lines; full Windows restart is not claimed. Final TX-disabled
restoration was reverified on both boards at 20:44 JST.
