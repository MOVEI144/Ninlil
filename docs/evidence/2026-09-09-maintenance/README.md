# Lifecycle verification — battery hardware gate open

2026-09-09, Windows host and local Linux containers. Hosted CI was not run.
Root replacement without child reconfiguration, explicit transfer and address
reuse are implemented. Battery software/model support is implemented, but the
reference ESP32-S3 wake/recovery gate remains **FAILED / awaiting diagnosis**.

## Local verification

GCC 13.3 and Clang 18.1.3, each normal and ASan/UBSan, pass 73 CTests each.
After the final HAL/sleep changes, all five affected tests pass again in all four
builds. This is the full matrix plus affected regression runs, not a claim that
the interrupted umbrella scripts themselves passed. Formatting races in two
umbrella runs are retained; the final format/static/fuzz/package gates pass.
Python 3.12.10, pyserial 3.5, esptool 5.3.0 and cryptography 50.0.1 pass the three
issuer/management tests. CMake 3.28.3 and Ninja 1.11.1 were used on Linux.

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
- OS USB restart is denied and esptool USB reset receives no response. A request
  to unplug/reconnect only board 2 is pending. CPU-retained Light-sleep firmware
  is built as a diagnostic variant only; it has not been flashed or verified.

Board 1 and board 3 are stopped with autorun false, setup revision 19. Board 2's
last confirmed settings are battery role, autorun false, revision 20; its current
runtime state is unobservable. Restore it before more sleep/role testing.
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
