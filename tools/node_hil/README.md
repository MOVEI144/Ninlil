# Autonomous reference-node tools

Protocol work runs on the MCUs. Current behavior and operations are described in
[deployment lifecycle](../../docs/DEPLOYMENT_LIFECYCLE.md); configuration, Join,
application receipt and physical evidence remain separate results.

## Build and install

Use ESP-IDF 6.0.2 with the pinned EDHOC/SX1262 dependencies. USB tooling uses
Python 3.12, pyserial 3.5, esptool 5.3.0 and cryptography for the optional issuer.
Set autonomous mode, 8 MiB flash, `partitions-autonomous-preserve.csv`, the local
node ID and the verified RF profile. The default diagnostic build has TX disabled.
`build.py SDKCONFIG - OUTPUT --nodes 1 2 3` builds the provisioning console;
`--build-directory CACHE` reuses caches while preserving fresh output evidence.
A public roster header can replace `-` for legacy installations. New deployments
use signed saved USB credentials and do not need a firmware rebuild per member.
`hardware.py NODE FOLDER` is restricted to the three verified legacy boards and
partition map. It checks chip identity, image hashes and protected-store checksums.
It writes only bootloader, partition table and app. Never erase existing stores or
use `--initial` on an established device. `P` explicitly provisions unused storage;
`I` returns public identity/key only. Neither command exports a signing key.

## Console and evidence

`console.py NODE COMMAND [HEX]` sends a bounded USB request. `G` takes BE32 run
milliseconds (maximum 600000); zero requires saved autorun. `X` disarms autorun
and stops RF. `R` resets; saved autorun determines whether RF restarts. `E` requests
battery-role Light-sleep. Full installation/transfer commands are in the lifecycle guide.
`S` submits target BE16 plus example sequence BE32; preserve the sequence for a
pending message, use a fresh one after completion. `Q` takes the returned Core ID:
`01 05` means application acceptance, independently of TX or Relay custody.
`H`, `B` and `L` inspect owner, peer and route state. `D 01` drains Relay custody;
`D` reads removal readiness, and `D 00` resumes Relay eligibility. `V` durably
revokes an address with its expected membership epoch. Requests are not receipts.
The application ledger is preserved and represents an application commit.

`campaign.py OUTPUT --seconds 300 --sequence N` exercises the three-board fixture.
`--existing-message ID` recovers an existing application receipt. `--restart-relay`
checks retained custody across an MCU reset; `--drain` checks removal readiness.
These software RF masks do not prove physical out-of-range coverage. Record exact
image hashes, hardware/wiring revision, date, console results and protected-store
checksums. Mid-program electrical cuts need a separate controlled power fixture.

Sleep tests retain late USB replies and separately report radio completion;
see [sleep diagnosis](../../docs/DEPLOYMENT_LIFECYCLE.md#sleep-failure-diagnosis).
