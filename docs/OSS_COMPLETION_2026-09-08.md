# Autonomous small-message OSS integration

September 8 candidate: autonomous steps 4–6, powered fixed-PHY operation,
64-byte application / 240-byte RF profile. Source review fixes and a physical
Root-reset round trip pass; production and field qualification remain open.

## Delivered scope

- Nine original findings and additional integration fixes have regression tests;
  see [review resolution](REVIEW_RESOLUTION_2026-09-08.md). Original review witnesses remain unchanged.
- `ninlil_node` owns EDHOC, committed Join, membership, authenticated probes,
  route preparation/application/reconciliation, drain, revocation and recovery.
  The caller drives receive/step and actual TX completion; it creates no thread.
- Stable persistent identity survives explicit key rotation. NIv3 preserves an
  irreversible initialization marker. Core/control journals bind the identity;
  initialized missing stores cannot be silently reprovisioned. Old NIv2 is read
  conservatively as initialized. No automatic erase or private-key export.
- Fresh sessions follow reboot. Persisted plans never substitute for live proofs.
  Separate boot-era lease time has a 61-second Root quarantine, a 60-second maximum
  lease and conservative authenticated synchronization; it is not UTC.
- Core retains logical messages, Relay retains opaque copies, and the application
  ledger commits before application acceptance. No transport stage implies a
  physical effect. Queued DATA is revalidated before actual transmission.
- Source retransmissions reuse E2E ciphertext for a bounded 30-second window;
  hop counters remain fresh. Context changes, expiry and cache eviction renew it.
  Draining custody is returned only after its authenticated source ends the old
  context and verifies its retained Core records.
- CMake provides relocatable static packages, explicit journal backend selection,
  Core-only builds, installable license/provenance notices and consumer checks.

## Real three-board evidence

[Structured results and image/checksum identities](AUTONOMOUS_HIL_RESULTS_2026-09-08.json)
preserve public console captures. `tools/node_hil/evidence.py` independently
reconciles submissions, receipts, application counts, resets and cleanup using
serialized record order; tied host timestamps do not change original evidence.

| Campaign | Observed result | Application ledger | Elapsed, including setup |
|---|---|---:|---:|
| 18 | New message through Relay, source APPLICATION_ACCEPTED | 1 → 2 | 260.285 s |
| 19 | Relay and receiver MCU reset after custody, same-message recovery and source acceptance | 2 → 3 | 430.494 s |
| 20 | Outstanding custody drained, removal readiness confirmed, fresh delivery and role resumed | 3 → 4 | 174.435 s |
| 24 | Previously pending receipt recovered without another application record | 5 → 5 | 272.886 s |
| 25 | Drain/removal readiness, new delivery and role resume repeated | 5 → 6 | 278.760 s |
| 26 | Root/source MCU reset after custody, retained message recovered and source accepted | 6 → 7 | 542.920 s |

Campaigns 18/19 masked direct 1↔3 DATA/probes in software; bootstrap/control
remained physically available. Campaign 20 restored the direct path and began
with one outstanding Relay record. The receiver gained exactly one record in
each case. Reset replay retained its one owned Relay record before release.
These are bounded bench observations, not range, sustained-throughput or latency guarantees.

Campaign 21 timed out before its planned reset. Campaign 22 (`lifecycle-ack`)
reset Root/source with message `60e640d910a56454a316b8594bcf8651` pending:
Core retention and receiver growth 4→5 passed, but its 500-second receipt deadline
failed. Campaign 23 also timed out after 300 seconds with the receiver still at 5.
Campaign 24 recovered that receipt; campaign 26 separately passed a fresh Root
reset with `proof-isolation`. Earlier failures remain failures in the record.

The three USB-powered XIAO ESP32-S3/Wio SX1262 boards use attached antennas,
921.4 MHz, −9 dBm, SF7/BW125, CR4/5, preamble 8. Protected NVS/PHY/store hashes
match before/after flashing; no new backups or signing-key exports occurred.
Campaigns 18/19 used `cipherwindow`, 20 used `drain-recovery`; each result is
tied to its actual image. Campaigns 24/25 used `epoch-proof`, 26 `proof-isolation`.

At cleanup all owners were stopped and Relay drain was resumed. The test images
are TX-enabled but boot stopped and require an explicit bounded `G` command;
they are not described as compile-time TX-disabled images.

## Local verification

The final proof-isolation source passes 45 tests × four compiler/sanitizer builds,
711 vendor tests, 20,000 fuzz executions, package consumers, formatting, static
analysis and strict ESP syntax. [Commands, versions and hashes](LOCAL_VERIFICATION_2026-09-08.json)
record the local matrix and ESP-IDF builds. Hosted CI was not run.

Native cases cover actual PSA/EDHOC/Core with POSIX and NOR-model journals:
loss, bounded queues, receiver/Relay/Root restart, stale epochs and sessions,
application commit interruption, drain with outstanding custody, revocation,
key rotation and higher-epoch reenrollment. Eight public scheduling seeds were
also exercised at recorded integration checkpoints; PSA generates the keys.

## Build, use and release boundary

See the [reference-node guide](../tools/node_hil/README.md) for provisioning and
HIL, and the [package guide](../README.md) for backend selection and Core-only builds.

Default roster limits are four for a powered endpoint and two for a battery leaf;
16 is the owner ceiling. The LoRa-only example owns SAR entropy and uses ESP-IDF
6.0.2 PSA. See [dependency notices](../THIRD_PARTY_NOTICES.md) and [security](../SECURITY.md).

Stores are bounded and append-only. Capacity exhaustion retains ownership and
reports an error; automatic compaction is not implemented. Bulk/OTA, automatic
PHY tuning, arbitrary network scale, battery sleep qualification and physical
actuator verification are outside this profile. Deployment must protect the
plaintext identity-at-rest reference port and qualify its entropy and clocks.

Controlled Flash power cuts lack a switch fixture; manual USB removal is separate.
Field/environment and production-security qualification remain open. No hosted
CI, GitHub publication or production release is claimed.

The 50,000-line project ceiling remains. [Source budgets](SOURCE_BUDGETS.md)
records the old combined 12,000-line failure, retirement of 25 obsolete sources,
and the separate allocation for the new execution owner.
