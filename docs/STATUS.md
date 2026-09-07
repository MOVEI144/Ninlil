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
