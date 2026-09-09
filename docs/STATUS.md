# Project status



Updated: 2026-09-09

## Latest scope: deployment lifecycle

The [lifecycle implementation](DEPLOYMENT_LIFECYCLE.md) adds an independent
credential issuer for Root-only replacement, battery Light-sleep, explicit
site/role transfer and guarded address reuse. The completed-message transport
history can be retired only after pending ownership is empty; application data
and device identity remain separate. New tests cover interrupted transfer and
fresh Join/delivery after address reuse. Battery sleep returns through radio
recovery and achieves fresh RF application receipts. Restart-window HIL passes;
USB-host awake operation and forced-sleep timing are separate gates. [The lifecycle record](evidence/2026-09-09-maintenance/README.md)
separates completed local checks, passing physical checks and failed sleep tests.
The runtime remains an alpha with separate field qualification gates.

## Previous scope and evidence

Saved enrollment, discovery through powered Relays and route replacement are
described in [field deployment](FIELD_DEPLOYMENT_2026-09-08.md), with all passing
and failed bench attempts in [field results](FIELD_RESULTS_2026-09-08.json).
Ninlil remains independent of KGuard; host integration, sensors and timing are
application choices. Configuration and Join are distinct from application delivery.

[Storage, bulk and radio extensions](STORAGE_BULK_RADIO_2026-09-08.md) provide
automatic journal collection, resumable bulk transfer and bounded TX power
adaptation. OTA is excluded. The 512-byte physical transfer/restart/readback
result is in [hardware evidence](EXTENSION_HIL_RESULTS_2026-09-08.json); full-size
RF transfer, electrical write-interruption and field qualification remain open.

## Repository and history

`MOVEI144/Ninlil` is the implementation authority. The earlier milestone status
sections remain in Git at `e213d49446463f363fd89c80ddfaab1749e9e384`: `docs/STATUS.md`.
The original revision and hash were verified before consolidating this page; see
[the history index](evidence/2026-09-09-maintenance/HISTORY.csv).
