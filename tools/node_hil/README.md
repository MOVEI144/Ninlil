# Autonomous reference-node integration

These tools drive application requests and collect evidence. EDHOC, Join,
membership, clocks, measured routes, custody and recovery run on the MCUs.
Current acceptance and limitations: [integration record](../../docs/OSS_COMPLETION_2026-09-08.md).

## Build and provision

Use an activated ESP-IDF 6.0.2 environment and the pinned dependencies fetched
by `scripts/fetch_edhoc.sh` and `scripts/fetch_sx126x_driver.sh`. No hosted CI is
required. The Python USB tools require Python 3.12, pyserial 3.5 and esptool 5.3.0.

1. Configure `embedded/esp32s3` with `idf.py menuconfig`: autonomous mode,
   8 MiB flash, custom `partitions-autonomous-preserve.csv`, local node ID,
   and the board's verified RF profile. Keep TX disabled for initial setup.
   The normal default remains diagnostic mode. The reference uses Root 1;
   every node needs the same public trust roster with its own entry.
2. Build without a roster for initial identity setup:
   `python tools/node_hil/build.py path/to/sdkconfig - .verify-node-identity`.
   No key is supplied by the computer. Explicit `P` creates a random stable
   identity and P-256 key only in unused identity storage; `I` exports only the
   32-byte identity and 65-byte uncompressed public key.
3. Write a public C header defining `NODE_ROSTER_COUNT` and
   `static const ninlil_node_member node_roster[]`. Each entry contains the
   exact public key and `ninlil_join_grant`: stable identity, shared stable
   16-byte authority, unique node address, nonzero membership/binding epochs,
   role, capabilities and explicit service grants. Never replace the authority
   during key rotation. See `ninlil_node.h` and `ninlil_join.h` for the contract.
   The three-board fixture grants service 256, payload 64, 16 live contracts,
   both directions and traffic mask 15. Roles are gateway, powered Relay and
   powered endpoint; only node 2 receives Relay custody permission.
4. Build the trusted configuration:
   `python tools/node_hil/build.py path/to/sdkconfig path/to/roster.h .verify-node-trusted`.
   `--nodes 1` builds one identity-specific image. Each build records input
   hashes, final image/config hashes and its complete build log. A fresh output
   directory prevents accidental evidence replacement. Each node has a separate
   build cache, reconfigured for its current inputs.
   `--build-directory path/to/cache-parent` reuses those ESP-IDF build caches;
   each node's resulting images and configuration remain in the fresh output.
5. Before flashing, verify physical chip/flash identity and the entire partition
   map. `hardware.py` is intentionally restricted to the recorded three-board
   fixture and its protected legacy ranges; it is not a generic board flasher.
   It writes and verifies only bootloader, partition table and application.
   `--initial` additionally requires all new store ranges to be erased; never
   use it on provisioned nodes. Never erase identity, Root era or custody stores
   to bypass a recovery error. No new device backup or private-key read occurs.
6. Run `P` again with the roster installed. It creates matching Core, control
   and application bindings for a new identity, or verifies existing bindings.
   Lost established storage is a corruption error, not a new installation.

The additive map is specific to the recorded legacy 8 MiB boards. It reserves
128 KiB each for Core/control/application, 256 KiB for session counters, and
8 KiB each for identity and Root era. Identity and business stores never share
erase sectors with ephemeral session counters. The example owns SAR entropy;
it does not run Wi-Fi, Bluetooth, ADC or I2S application drivers.

## Run and interpret

`console.py NODE COMMAND [PAYLOAD_HEX]` sends one bounded USB request. `G` starts
an explicitly bounded run (uint32 big-endian milliseconds, maximum 600000);
`X` stops RF and closes the owner; `R` resets the MCU and boots stopped. A reset
test must explicitly start the next bounded run. Autonomous protocol work does
not imply automatic RF transmission on boot.

`S` submits the generated 64-byte example message: target uint16 plus sequence
uint32, big-endian. Reusing a sequence reuses its durable application key.
`Q` takes its returned 16-byte Core ID; `01 05` means satisfied at application
acceptance, separately from radio TX and Relay storage. The receiver's append-only
application ledger deduplicates by Core ID and payload digest. It represents an
application commit, not an actuator or other external physical effect.

`D` takes 1 to drain or 0 to resume the Relay role. Existing custody is retained;
readiness also requires removal of dependent routes. `V` is Root-only durable
revocation (node uint16, expected membership epoch uint64). `D` with an empty
payload reads removal readiness (one byte); a successful drain request alone
does not mean removal is ready. Revoked enrollment
requires an explicitly higher membership epoch and a matching trusted roster.
`H` reports public owner/radio/application status; `B` reports a peer's public
session fingerprints and link observations. `L` takes source and target uint16
addresses and reports the local and authority plans, readiness, and lease time.
Inspection does not request a route or authorize transmission. No command exports signing keys.

For the three-board fixture, enable `NINLIL_NODE_HIL_ENABLE` in all test images:
`python tools/node_hil/campaign.py .verify-campaign-1 --seconds 300 --sequence 1`.
The test masks direct node 1↔3 data/probes in software. Bootstrap/control remains
physically available, so this is not an out-of-range or field-coverage test.
The source must report application acceptance and the receiver must gain one
application record. Use a new sequence after success; preserve the same sequence
when recovering a still-pending message. Every exit attempts to stop all boards.
`--existing-message CORE_ID` requires an active source and a receiver record
already committed; acceptance must recover without growing the application ledger.
`--restart-relay --sequence NEW_SEQUENCE --seconds 500` starts with empty Relay
custody, holds final DATA reception, and resets the Relay and receiver only after
new custody is observed. It checks replayed custody and the unchanged receiver
ledger before releasing DATA. This is an MCU reset, not a timed power cut.

Record each image hash, wiring/hardware revision, date, console/result logs and
protected-range checksums with the outcome. Journal capacity is finite and
append-only: full storage keeps ownership and reports backpressure/error.
Controlled mid-program power cuts require a separate power fixture.

`--drain --sequence NEW_SEQUENCE --seconds 500` leaves the direct path available,
requests Relay drain even with old custody, waits for empty custody and removed
route dependencies, then requires a fresh application receipt. Cleanup resumes
the Relay role and checks that removal readiness clears before stopping RF.
