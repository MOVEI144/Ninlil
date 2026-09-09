# Project status



Updated: 2026-09-09

## Latest scope: deployment lifecycle

The [lifecycle implementation](DEPLOYMENT_LIFECYCLE.md) adds an independent
credential issuer for Root-only replacement, battery Light-sleep, explicit
site/role transfer and guarded address reuse. The completed-message transport
history can be retired only after pending ownership is empty; application data
and device identity remain separate. New tests cover interrupted transfer and
fresh Join/delivery after address reuse. Battery timer sleep still fails its
physical recovery gate, including a CPU-retained diagnostic build. The checkpoint
probe is installed; USB reconnection is needed to read its retained diagnostic. [The lifecycle record](evidence/2026-09-09-maintenance/README.md)
separates completed local checks, passing physical checks and failed sleep tests.
The runtime remains an alpha with separate field qualification gates.

## Previous scope: field growth and saved deployment settings

The [field deployment implementation](FIELD_DEPLOYMENT_2026-09-08.md) adds durable
live enrollment, Root-signed COSE credentials, bounded discovery through powered
Relays, saved USB configuration and opt-in boot autorun. Five-device native
scenarios cover alternate-Relay failover and recovery through a previously
unknown Relay. The three-device configuration/reset test preserves identities
and the existing application ledger. Model evidence and RF evidence are separate.
The [broader design](FIELD_NETWORK_EVOLUTION_2026-09-08.md) is historical context;
the lifecycle record above supersedes its deployment feature status.
The September 9 decision keeps Ninlil independent of KGuard: host integration,
sensor semantics and battery timing are application choices. Neither a configured
device nor a successful Join proves
application delivery or field readiness.
The latest local matrix passes 66 tests in each of four compiler/sanitizer builds.
Three consecutive fresh RF deliveries pass, but latency reaches 271 seconds per
message and Relay custody remains after completion. Final v19 configuration/reset
preserves all 15 receiver records. All 22 bench attempts, including failures, are
retained in the [field results](FIELD_RESULTS_2026-09-08.json).

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


## Repository and history

`MOVEI144/Ninlil` is the implementation authority. The earlier milestone status
sections remain in Git at `e213d49446463f363fd89c80ddfaab1749e9e384`: `docs/STATUS.md`.
The original revision and hash were verified before consolidating this page; see
[the history index](evidence/2026-09-09-maintenance/HISTORY.csv).
