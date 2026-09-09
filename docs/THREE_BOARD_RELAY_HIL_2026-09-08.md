# Three-board encrypted Relay HIL — September 8, 2026

## Purpose and decision

The user connected a third kit, confirmed the same Wio-SX1262 hardware,
attached antenna and R 201-250230 marking, and explicitly declined new backups.
This bounded continuation tests real MCU Relay custody, RF forwarding, and
restart recovery. It does not complete autonomous product integration.
Source: `dfde82404b7a0d444e9457de06183d2354162c6e`. All implementation/test sources are ordinary repository files.

## Results

The final campaign `relay-20260908-run3` passed all five grouped checks.

| Observable condition | Result |
|---|---|
| Real EDHOC between all three pairs, separate E2E and hop contexts | Pass |
| A→B→C and C→B→A, 10 opaque encrypted test packets each way | Pass; 232-byte maximum hop frame |
| Altered ciphertext, wrong sender, stale epoch, premature/wrong ACK | Rejected without custody retirement |
| Duplicate admission and an intentionally withheld forward | Same owned packet retained |
| Restart only Relay B while it owns a packet and is draining | Flash restores the same opaque packet and drain state |
| Forward before fresh hop authentication after reset | No eligible transmission; ownership remains visible |
| Reauthenticate A–B and B–C, preserve the A–C session | Original end-to-end ciphertext delivered unchanged |
| Fill all 8 custody slots, then submit a ninth | Backpressure; 8 retained and forwarded, ninth subsequently admitted |
| Drain, reopen the Flash log, and inspect local removal readiness | Ready only after all local custody is retired |

Independent analysis paired **155 physical TX/RX frames**, checked **167 unique
monotonic encrypted envelopes**, and reconciled **31 source-owned test packets**
with host-observed plaintext and hop retirement. It independently matched the
Relay's last pre-reset opaque record to its first post-reset record.

## Exact boundary

The USB owner schedules the fixed path A–B–C (or reverse), grants a static test
route with epoch 1, and pins MCU-generated public credentials. Relay B itself
unseals and validates the hop, checks the opaque packet digest, commits and
revalidates actual Flash custody, then produces its hop ACK. It never possesses
the A–C end-to-end session and cannot decrypt that ciphertext.

Downstream ACKs are test-owner claims after validated endpoint decryption and
an fsynced host observation. They are **not MCU Core or application receipts**.
The source fixture is also fsynced before its RF submission. Local Relay
readiness is not Coordinator dependency removal. No automatic route/PHY change,
production enrollment, autonomous pump, timed Flash-write power cut or field
range/interference acceptance is claimed.

## Hardware, configuration and preservation

A: COM3 / E0:72:A1:F7:FF:0C; B: COM5 / E0:72:A1:D8:3E:74;
C: COM7 / E0:72:A1:D7:77:28 (initially application USB COM8).
All are verified ESP32-S3/Wio-SX1262 kits using the established GPIO38-high RX
wiring, SPI4MHz, 921.4MHz/-9dBm/SF7/BW125kHz/CR4/5/preamble8. Existing CCA,
TXDONE, pause and maximum-airtime checks remain enabled.

**No new device backup was created.** Only partition metadata and device-side
checksums were inspected. Before and after flashing/testing, checksums matched
for NVS/PHY on all boards, A/B's original journal/security area, and C's complete
4MiB `ninlil_st` at 0x190000..0x590000. C's dedicated additive partition table
retains that store and puts new control/session storage above it, in unused
space checked erased before provision. Existing records were never cleared.

A/B finish on their known TX-disabled initialization images. C finishes on a
new TX-disabled Ninlil image with its data-preserving table; its prior executable
was replaced, not backed up or restored. All three completed **100 initialization
cycles** and reported `freq=0 tx=disabled`. Test custody/counter records remain
in the dedicated test areas; session keys existed only in MCU RAM.

## Verification, corrections and evidence

Local SDK/toolchain remains ESP-IDF v6.0.2 / Xtensa GCC15.2.0, pinned libedhoc
and Semtech dependencies, esptool5.3.0 and pyserial3.5. Both GCC and Clang,
with and without ASan/UBSan, passed all 27 tests (**108/108**); all **711**
adapted vendor tests, two 10,000-run fuzz campaigns, provenance, static and
ESP syntax checks passed locally. The four bench C modules also passed target
GCC `-fanalyzer` with generated node-2 configuration. Hosted CI was not run.
The cached local build directories were rebuilt and tested normally; no test
sources were staged into generated archives and no checks were weakened.

- A strict narrowing-conversion diagnostic was fixed with an explicit cast
  of the already bounded direction value; the warning remains enabled.
- Initial run1 found C still in ROM download mode after its manual BOOT entry.
  Espressif's watchdog reset and USB reconnection restored normal boot.
- Run2 completed all three handshakes but timed out at A's first data TX.
  The USB owner had omitted polling A's RX while A overheard B–C traffic.
  The driver preserves pending RX and backpressures TX. The test owner now
  processes bounded overheard RX on sender and receiver before a scheduled TX.
  All three boards were restarted and the complete campaign rerun as run3.

[Machine-readable summary](THREE_BOARD_RELAY_RESULTS_2026-09-08.json) records
image hashes, protected-region checksums, scope and the artifact manifest.
Raw captures, failed attempts, build logs and independent analysis are retained
under `.verify-m1-evidence/relay-20260908-*`. Tools are in
`tools/secure_bench/relay_hardware.py`, `relay_campaign.py`, `relay_analyze.py`;
SDK builds use `build.sh` with explicit `idf.py reconfigure build`.
