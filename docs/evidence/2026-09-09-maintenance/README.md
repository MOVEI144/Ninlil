# Lifecycle verification — battery hardware gate open

2026-09-09, Windows host and local Linux containers. Hosted CI was not run.
Root replacement without child reconfiguration, explicit transfer and address
reuse are implemented. Battery RF recovery is now observed; USB timing/ledger
and restart-window HIL are recorded separately below.

## Local verification

GCC 13.3 and Clang 18.1.3, each normal and ASan/UBSan, pass 73 CTests each again
after the restart-window fix (`cmake --build DIR --parallel 4` followed by
`ctest --test-dir DIR --parallel 4 --output-on-failure`, the four directories below).
After the final HAL/sleep changes, all five affected tests pass again in all four
builds. This is the full matrix plus affected regression runs, not a claim that
the interrupted umbrella scripts themselves passed. Formatting races in two
umbrella runs are retained; the final format/static/fuzz/package gates pass.
Python 3.12.10, pyserial 3.5, esptool 5.3.0 and cryptography 50.0.1 pass five
issuer/management/late-reply tests. CMake 3.28.3 and Ninja 1.11.1 were used on Linux.

Commands: `NINLIL_CLEAN_BUILD=0 NINLIL_BUILD_ROOT=/tmp/ninlil-oss-final
NINLIL_JOBS=4 CLANG=clang-18 CLANG_FORMAT=clang-format-18
CTEST=/work/.verify-m1-evidence/ctest-parallel bash scripts/ci.sh` (wrapper adds
`--parallel 4`). Final affected runs use `ctest --test-dir BUILD --parallel 4
--output-on-failure -R 'sx1262|esp_sleep|node_sleep'`, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.
Final remaining gates use repository `check_esp_syntax.sh`, `static_analysis.sh`,
`static_crypto.py`, `fuzz_sim.sh`, `fuzz_control.sh`, `verify_vendor.sh`,
`verify_package.sh`, all `loc*.sh`, clang-format 18 and `git diff --check`.
Python command: `.verify-m1-tools/Scripts/python.exe -m unittest discover -s tests -p 'test_*.py' -v`.
The exact output hashes and local artifact locations are in [INDEX.csv](INDEX.csv).

New tests cover retained child work across Root replacement, old-Root fencing,
20 address reuses followed by fresh encrypted Join/delivery through a Relay,
interrupted transfer publication/retirement/reopen, shrinking role resources,
pending-work refusal, NIv5 prior-Root history and sleep/error/clock boundaries.
A reproduced legacy pending-plan migration failure was fixed by aborting old
preparation before the new generation fence; both journal backends then pass.

## Physical checks and failures

Same Seeed XIAO ESP32-S3 + Wio SX1262 B2B boards/wiring as the preceding bench;
USB power and attached antennas. Radio settings remain Japan 921.4 MHz,
SF7/BW125/CR4/5, preamble 8, configured maximum -3 dBm. No range/current claim.
ESP-IDF 6.0.2 / Xtensa GCC 15.2.0 builds three v25 images and the default-off
configuration. `tools/node_hil/build.py` records inputs and image hashes;
`hardware.py` verifies identities and protected stores before/after additive
flashing. No identity/store erase or device private-key export was performed.

- Independent CA setup succeeds on all three boards, revision 18 to 19.
- v21 drain/delivery passes in 103.57 s: Relay custody 4 to 0 and removal ready;
  receiver records 15 to 16; source reports APPLICATION_ACCEPTED for
  `3b92e6d86df59f1a41e33083c4cb185b` (sequence 26090880).
- v23 USB transfer of board 2 from Relay to battery succeeds: same device ID,
  address 2, membership/binding 2, setup revision 20. New-role Join succeeds.
- Sleep attempt 1 returns IO: SetSleep keeps BUSY high. Added a failing HAL
  regression, then fixed the post-command wait and 500 us retention interval.
- Attempt 2 rejects a staged scheduler frame. The owner now retains that frame
  through sleep and revalidates it on wake; the affected model passes.
- Attempt 3 on v25 loses USB response to a 5-second sleep request. USB reopen
  and a separate 200-second Root radio observation do not recover communication.
  No new application submission occurs; sequence 26090881 remains unused.
  This is a failure, not proof of sleep duration or successful wake.
- OS USB restart is denied and esptool USB reset receives no response.
- The user reconnects all three boards; all respond stopped, autorun false.
  CPU-retained Light-sleep image `7b4d5dba31a3d74f1c07b10d068fb955ea167d7b9f124bdcd501756f5056acbe`
  is additively flashed on board 2 with protected-store hashes unchanged.
  Join succeeds, but the 5-second E request again loses USB response. A further
  202.63-second Root observation receives no child traffic. CPU retention alone
  does not fix recovery. No message is submitted; sequence 26090881 stays unused.
- A bench-only NVS checkpoint probe is built with strict warnings. Three wrapper
  symbols are present only in the diagnostic image; an ordinary rebuild in the
  same cache removes them. Three Python tests pass. The probe is not flashed:
  esptool again receives no serial response. Physical reconnect is required.
  NVS checkpoints diagnose progress, not release behavior or sleep/current proof.
- After another user reconnect, all three boards respond stopped/fault-free.
  The checkpoint image `bf3591fc03f18213363653a749cf719757f35b7d1a83c14f133c3476fea68f8c`
  is additively installed on board 2; protected stores match. Initial reboot
  prints stage 0. Join succeeds, then E5000 again times out; no S occurs.
  USB reset fails. The recorded checkpoint has not yet been read: another
  physical reconnect is needed. Diagnostic progress is not yet established.
- The subsequent boot reads stage 6, result 0, timestamp 34833479 us. A further
  test keeps the Root active after the USB timeout: sequence 26090881 reaches
  APPLICATION_ACCEPTED (`45bc09ab6465c44f68ec5194bca88ba3`). Both X replies are
  observed. The aggregate test still fails its unobserved USB timing/ledger gate.
  Earlier radio observations followed an unacknowledged X, so absence of traffic
  did not establish a CPU hang. Starting the battery again after the old awake
  deadline exposes a separate timer-reset defect; the fix resets the awake
  window on every successful start, with a native boundary regression.
  Diagnostic-only wrappers are retired to verified Git history. Normal images
  are prepared, but USB loader reset fails; the physical fix is not installed.
- The ordinary fix is subsequently installed: a 125-second stopped interval
  preserves a fresh awake window and record 1; sequence 26090882 then reaches
  APPLICATION_ACCEPTED (`c30c70ef2db548e24ef28bb8bf042bd5`) after forced sleep.
  Its USB reply remains absent. The USB-host guard then passes on hardware:
  after 125 seconds awake, sequence 26090883 reaches APPLICATION_ACCEPTED and
  receiver records grow 2 to 3; both owners acknowledge stop (137.48-second run).

The retired probe commands and initial/corrected symbol checks remain in Git at
`214d279:docs/evidence/2026-09-09-maintenance/README.md`; ordinary images omit them.

All three boards are verified stopped, fault 0, autorun false. Boards 1 and 3
retain setup revision 19; board 2 remains battery role, revision 20, with image
`15615084b248e63660fd5a0899b8e4bb49d8cf021acccddb45722d22362dc421`.
Protected stores match across flashing. Forced-sleep elapsed-time readout remains open.
A fresh, unregistered spare is still needed for physical Root replacement.
Timed electrical power cuts, current/lifetime and field/long-duration gates
remain unrun. No PR, push or field release was performed.

## Sources and retained evidence

No SDK or radio dependency upgrade. Sleep behavior was checked against installed
ESP-IDF 6.0.2 `docs/en/api-reference/system/esp_timer.rst` and
`examples/system/light_sleep/README.md` (USB may not recover after sleep), and
[Semtech SX1261/2 datasheet 13.1.1, p.67](https://files.waveshare.com/wiki/SX1262-XXXM-LoRaWAN-GNSS-HAT/DS_SX1261-2_V1.2.pdf).
The optional issuer dependency follows [cryptography 50.0.1 ES256](https://cryptography.io/en/50.0.1/hazmat/primitives/asymmetric/ec/).
Issuer working files are retained in protected user-profile storage under
`.ninlil/ninlil-bench-20260909`, outside disposable evidence and outside Git.
Historical reports remain recoverable by the revisions/hashes in [HISTORY.csv](HISTORY.csv).
