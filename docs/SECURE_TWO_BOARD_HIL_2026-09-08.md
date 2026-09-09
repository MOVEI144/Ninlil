# Two-board secure hardware campaign — 2026-09-08

## Purpose and scope

The user reconnected both boards and explicitly resumed all feasible two-board
hardware tests on September 8 JST. This supersedes the earlier decision to stop
physical testing. This campaign tests MCU-executed cryptography, Join storage,
Flash counters and actual SX1262 RF. The USB controller invokes library
boundaries explicitly; it is not the autonomous production network pump.

The bounded campaign and independent RF reconciliation passed. Both boards
were restored byte-for-byte to their original Flash and verified TX-disabled.
Do not interpret the completed software profile as field release acceptance.

## Hardware and preservation

- A: COM3, ESP32-S3 identity E0:72:A1:F7:FF:0C, logical node 1.
- B: COM5, ESP32-S3 identity E0:72:A1:D8:3E:74, logical node 2.
- Seeed XIAO ESP32-S3 / Wio-SX1262 B2B; GPIO38 high RX gate; SPI4MHz,
  non-DMA, NSS41/reset42/DIO1 39/BUSY40/MOSI9/MISO8/SCLK7.
- Antennas attached, Japan, both R 201-250230, USB-only power: previously
  confirmed by the user and carried forward.
- Unchanged bench PHY: 921.4MHz, -9dBm, SF7, BW125kHz, CR4/5, preamble8.
  Existing CCA, 50ms pause and 400ms maximum airtime checks remain active.
- Each complete 8MiB Flash image was read in ROM mode and matched against the
  MCU's whole-Flash MD5 before writing. Saved SHA256: A
  `ee66e7c606ae75fc3fe5ee563a1570ae776f337018e3404103aaf7fe9b116726`, B
  `69dc0dc291ab1878cde60b7d7b7f9512c9dcbdc806b73122e5265bcd84b3f683`.
- Initial app and partition bytes matched the prior TX-disabled images.
  New control/session area 0x224000..0x284000 was fully erased on both boards.
  Existing delivery journals and original security partitions are preserved.

## Software and local verification

Source: `8aacb0c4fae46517055da66ecca8de413098853b` on `codex/static-star-foundation`
(foundation `9c5e886` plus the lab counter-limit correction). The final source revision and
artifact hashes are recorded in the companion results JSON. Bench mode is default-off;
private keys are generated in MCU RAM and never logged or exported. Every
boot uses new credentials pinned by the USB test owner and fresh EDHOC.

ESP-IDF v6.0.2 / Xtensa GCC15.2.0, esptool5.3.0, pyserial3.5,
Windows Python3.12.10. SDK/image provenance remains pinned as in the
[software evidence](SECURE_NETWORK_LOCAL_EVIDENCE_2026-09-07.md).

Target build used the existing `/tmp/ninlil-456-build` in
`ninlil-456-target-v2`, explicit `SDKCONFIG=/tmp/ninlil-456-sdkconfig`,
`PROJECT_VER=secure-bench-20260908`, and **`idf.py reconfigure build`**.
Copied config timestamps initially left the prior generated node header in
place; identical image hashes caught this before flashing. The invalid
preflight artifacts and logs are retained. Explicit reconfiguration produced
and verified distinct generated node headers and distinct binaries.

- A final image SHA256: `5963295a9f44bc2f0d617eb05a1403f902a423bec883e7cb4cdfe638dcec347c`.
- B final image SHA256: `f2dd7a74a49cac65c168f030487e148e0db06f1ede2bb6f114502f9d7a1de41f`.
- Whole-Flash stub reads failed before any mutation; bounded ROM reads succeeded.
- Initial strict compile rejected a 16-byte string initializer omitting its
  terminator; fixed without suppressing the warning.
- Local command: `docker exec -w /work -e NINLIL_BUILD_ROOT=/tmp/ninlil-secure-hil-local ninlil-static-star-verify bash scripts/ci.sh`.
- New MCU bench sources also passed target GCC `-fanalyzer` using actual SDK
  compile commands. Hosted CI was not run.

## Completion conditions and results

Main campaign: September 8, 00:47:03–00:50:08 JST. Reset campaign:
00:50:47–00:51:10 JST. All 20 reported checks passed (17 main, 3 reset).

| Observable condition | Physical result |
|---|---|
| Two distinct MCU-generated keys; mutual EDHOC over RF; cached handshake retry | Pass |
| Before approval, after pending approval, and after revoke, peer policy denies access | Pass |
| Encrypted Join accept and commit; repeated commit; Flash journal reopen | Pass |
| AES-CCM plaintext lengths 1/23/24/25/63/64/65/160/199/200 in each direction | 20 passes; 240-byte maximum RF frame; 201-byte plaintext rejected |
| Continuous encrypted test data | 100/100 each direction |
| Change each of the 240 envelope bytes separately and transmit it | All 240 rejected; the untouched original still decrypted afterward |
| Replayed, reflected, wrong-channel, stale-session and wrong-key inputs | Rejected |
| Withhold one encrypted frame, generate a new retry; reopen Flash counter store | Retry received; unused reserved numbers skipped, no counter reuse |
| Out-of-order packets within and outside the 64-counter receive window | Valid reorder accepted; old packet rejected |
| 1024-byte control message fragmented over RF, reordered and duplicated | Reassembled in both directions |
| Conflicting fragments, poisoned assembly, missing fragment past 5s, fresh retry | Rejected/expired, followed by successful fresh reassembly |
| Rekey and durable revocation | Old ciphertext rejected; revoke persists and local session closes |
| Actual MCU reset | Join/revoke records retained; no session or authority permission restored |
| Fresh post-reset EDHOC and encrypted RF | 20/20 each direction; old ciphertext rejected; EDHOC alone still gives no membership permission |
| Main task stack / minimum free heap | A: 13,624 / 320,572 bytes; B: 13,608 / 321,052 bytes |

The independent analyzer paired **504 + 52 = 556** actual successful RF sends
with the opposite board's captured receives, byte for byte. It separately
checked **300 + 40 = 340** emitted encrypted envelopes for strictly increasing,
nonreused counters per session and direction. Unsent test envelopes are not
counted as RF sends. These counts are not Core/application delivery receipts.

The first physical run stopped at secure-session open with `INVALID`: the lab
requested 2^40 counters in blocks of 32, requiring more than the Flash store's
32-bit generation permits. The library correctly rejected that configuration.
The lab now caps its counter range at 1,000,000; no production check was weakened.
Both revised target builds and target analysis passed; the whole campaign was
rerun. The initial failed capture and pre-update Flash readbacks remain saved.
The full local matrix, static/syntax/provenance checks, two 10,000-run fuzz
campaigns and 711 adapted vendor tests passed. Unchanged vendor verification
was reused at flash preflight while its complete repeat finished alongside HIL.

## Restoration and evidence

Actual post-test control and counter partitions were saved and matched to the
MCUs' region digests. Restoration then matched each **entire 8MiB Flash** to
its original backup, preserving prior journals and security state. Finally,
both restored boards booted through **100/100 radio initialization cycles**,
reported `freq=0 tx=disabled`, and explicitly ended with operational RF disabled.
No secure keys are restored: the bench held them in RAM only.

The [machine-readable results](SECURE_TWO_BOARD_RESULTS_2026-09-08.json) contain
all check outcomes, RF reconciliation, image hashes, restoration evidence and
artifact-manifest identity. Raw artifacts are retained in `.verify-m1-evidence`
under `secure-20260908-*`, including failed attempts. Commands were:

```
python -B tools/secure_bench/hardware.py flash a   # initial r3, then app-only update
python -B tools/secure_bench/hardware.py flash b
python -B tools/secure_bench/campaign.py secure-20260908-run1
python -B tools/secure_bench/hardware.py update a
python -B tools/secure_bench/hardware.py update b
python -B tools/secure_bench/campaign.py secure-20260908-run2
python -B tools/secure_bench/analyze.py secure-20260908-run2
python -B tools/secure_bench/restart.py secure-20260908-restart1 secure-20260908-run2
python -B tools/secure_bench/analyze.py secure-20260908-restart1
python -B tools/secure_bench/hardware.py restore a
python -B tools/secure_bench/hardware.py restore b
python -B tools/secure_bench/verify_idle.py a
python -B tools/secure_bench/verify_idle.py b
```

The current `flash` helper selects corrected r4 artifacts; `update` is only the
recorded r3-to-r4 transition. Never overwrite an existing evidence label.

## What this cannot establish

A physical three-node Relay, alternate paths, large-peer field behavior,
autonomous on-device control dispatch, Core application delivery through the
new secure pump, production credential provisioning, timed Flash-write power
interruption, RF interference/environment/range, and physical application
actuation are not established by this USB-owned two-board crypto campaign.
Manual USB removal and earlier durable Core fault tests remain dated,
separate evidence. Software reset is not power removal. Counter-store reopen
is not a reset or a power-cut test. Synthetic withheld frames are not natural
RF-loss measurements. Restored membership does not imply restored keys or
application permission.
