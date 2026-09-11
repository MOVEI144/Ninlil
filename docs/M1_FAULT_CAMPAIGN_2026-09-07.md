# M1 available bench tests — 2026-09-07

## Purpose

Confirm that the two connected boards retain messages and recover from bounded
communication failures and stored-data corruption. This record separates real
radio exchanges, software fault injection on real boards, host models, and
unrun physical gates. It does not close Issue #7 or assert field readiness.

## Method

Firmware source `3254ae3f4f76a51061c2d9a79b1b37eb8804c302` adds an opt-in,
default-off `CONFIG_NINLIL_DELIVERY_FAULT_CAMPAIGN` to the M1 test application.
Core, wire format, storage format, RF profile, and radio driver are unchanged.
The fixed HIL payload is 12 bytes (52-byte DATA, 26-byte receipt).

The devices remain the Seeed XIAO ESP32-S3 / Wio-SX1262 B2B kits identified in
[M1 RF evidence](M1_RF_CAMPAIGN_2026-09-07.md): board A serial
`E0:72:A1:F7:FF:0C` on COM3 and B `E0:72:A1:D8:3E:74` on COM5, USB-only power,
user-confirmed R 201-250230 labels and Antenna 2. Use the previously reviewed
limited JP bench profile, 921.4 MHz / -9 dBm / SF7 / BW125 / CR4/5, existing
CCA and duty pauses. This adds no regulatory or antenna certification claim.

| Injection | Method and expected observation |
|---|---|
| DATA loss, sequence 1 | Discard first successfully received DATA before the adapter; retry must use the same ID. |
| Receipt loss | Discard first successfully received receipt before the adapter; receiver must not consume the retry twice. |
| Duplicate receipt | Queue the next received receipt twice; exactly one terminal ownership result. |
| Duplicate DATA, sequence 2 | Queue one received DATA twice; exactly one durable inbox and consumer acceptance. |
| Reserved flags, sequence 3 | Set reserved wire bit after RF reception; reject before durable inbox creation, then recover on valid retry. |
| Wrong target, sequence 4 | Change received target to node 99; no acceptance for that altered frame, then recover on retry. |
| Missing DIO1 / TX timeout, sequence 5 | Temporarily disable GPIO39 interrupt around a real send; require the real driver's bounded timeout, restore interrupt and recover. Physical RF may already have succeeded; timeout is not proof of no transmission. |
| Receiver restart before receipt, sequence 6 | Defer the matching receipt, durably consume, then call esp_restart. Replay must recover the receipt without consuming the same ID twice. The boot-local sequence-4 marker prevents repeating the hold after restart. |

The first six rows are deliberately software injections on actual endpoints,
not RF interference or measured over-the-air packet loss/duplication. GPIO39
interrupt masking uses the pinned locally installed ESP-IDF v6.0.2 public API;
it does not electrically disconnect the DIO1 wire. The receipt defer is adapter
backpressure, not a physical BUSY-pin fault.

## Stored-data corruption and restoration

On the existing `80ab9ac` recovery firmware, read and verify both complete
128 KiB journals. On board B only, alter byte 68 of a copy of the first 4 KiB
journal sector at 0x200000, retaining the original checksum and commit marker.
Verify the written sector, boot, require `durable Runtime open failed` and
return from app_main without delivery-ready, link-send, or consumer markers.
Restore the original sector in the host helper's finally block and require the
entire journal's device MD5 to match the saved original. Reboot both endpoints
and reconcile all 100 original IDs against logs and read-back journals.

This checks detection of already committed corruption. It is not an interrupted
Flash program/erase test. The executable procedure is
[tools/m1_corrupt_journal_hil.py](../tools/m1_corrupt_journal_hil.py).

## Results and evidence

All scoped campaigns passed. [Structured results](M1_FAULT_RESULTS_2026-09-07.json)
contain every message ID, device/application/journal/log identity, injected
fault event, observed reset reason and final device state.

| Test | Measured result |
|---|---|
| GCC / Clang / ASan+UBSan | 16 CTests in each of 4 configurations, 64/64 PASS |
| Static analysis, strict ESP syntax, formatting, simulator reproducibility | PASS |
| Manifest parser fuzz | 10,000 runs, seed 20260907, PASS |
| Four ESP-IDF role builds | PASS, complete configurations and image hashes saved |
| Real committed-data corruption on B | Detected before delivery; original full journal restored byte-identically |
| Original 100-message campaign after restoration | Same 100 IDs, no retransmission, both journals unchanged |
| A to B with faults | 100/100 REMOTE_STORED / SATISFIED; no missing or duplicate ownership/consumer acceptance |
| B to A with faults | 100/100 REMOTE_STORED / SATISFIED; no missing or duplicate ownership/consumer acceptance |
| Missing TX completion interrupt | Both senders report timeout -10, restore interrupt, recover and complete |
| Receiver restart before receipt | Both show RTC_SW_CPU_RST; sequence 6 consumed exactly once |
| Completed fault campaign, both-board reboot | Same 100 IDs, zero retransmission, byte-identical journals |
| Monitor-process restart while sender pending | Another 100/100, same pending ID, no sender reboot, no missing/duplicate ownership |
| Final TX-disabled images | Verified on both devices, frequency 0, 100 initialization cycles each |

Each direction has nine injection markers for eight logical cases (receipt hold
and restart form one case). Sender logs show 106 completed TX calls and receiver
logs show 103; the intentionally ambiguous timeout is not counted as a completed
TX. Each journal has 100 create records and 100 of each expected transition.
Both sender stack checks retain 12,876 / 16,384 bytes (78%). This is one bounded
run per direction, not a soak test.

The old TX-disabled initialization image emits 99 known ISR-service-already-
installed diagnostics during its 100 cycles; the existing validator explicitly
counts these. New fault campaigns have no unaccounted error/reset or truncated
log. Final devices retain the last journal and run TX-disabled source 8e8a6ea.

Two setup failures are preserved. CRLF in the generated build helper stopped
the first builds before compilation. Premature flash invocations were refused
by the final-local-PASS guard before serial access. LF scripts and completed
local checks resolved both; neither is hidden or labelled a hardware failure.

Exact local gate and bench commands:

```text
NINLIL_BUILD_ROOT=/tmp/ninlil-fault-final NINLIL_JOBS=4 bash scripts/ci.sh
ctest --test-dir /tmp/ninlil-all-20260907/gcc --output-on-failure -R "m1_hil_delivery|m1_radio_delivery|m1_sx1262"
python tools/m1_corrupt_journal_hil.py .verify-m1-evidence
python tools/m1_bench/flash-fault.py fault-a-init
python tools/m1_bench/flash-fault.py fault-b-resp
python tools/m1_bench/capture-fault.py fault-a-init fault-b-resp fault-forward-01
python tools/m1_bench/fault-journal.py a fault-forward-01 read fault-a-init
python tools/m1_bench/fault-journal.py b fault-forward-01 read fault-b-resp
python tools/m1_bench/analyze-fault.py fault-forward-01 new
```

The reverse run uses fault-b-init / fault-a-resp and label fault-reverse-01.
Completed replay uses those roles, label fault-completed-replay and analyzer
mode replay. Each fresh campaign is preceded by verified clean-start snapshots.
The four IDF builds use tools/m1_bench/build-fault-cached.sh in the local pinned
cache with source commit, variant and artifact as arguments; exact idf.py
commands and resolved sdkconfig are retained. Toolchain versions are in the
JSON. The artifact manifest records all retained run files by size and SHA-256.


## Monitor-process restart follow-up

After a verified clean start, a first Python monitor boots A while B remains in
its ROM loader. It records sequence 1 committed and a completed physical send,
then closes its port and exits without resetting A. A second Python invocation
reopens A's USB console without ROM synchronization/reset, then boots B. The
same pending ID recovers and all 100 messages reach REMOTE_STORED/SATISFIED.
The same fault suite also executes during this extra run, including the separate
planned receiver restart at sequence 6. The sender has exactly one boot marker
across the two log segments; all message IDs and journals reconcile.

The procedures are `tools/m1_bench/start-pending-monitor.py` followed by
`tools/m1_bench/capture-monitor-restart.py fault-a-init fault-b-resp
fault-monitor-restart`; journal readback and analyzer are the same as above.
The combined sender log retains the entire original pre-monitor segment and
subsequent capture. TX-only log lines emitted in the interval are unobserved,
so this run does not claim a complete TX-attempt count. This is a real monitor
process exit/replacement, not a reboot of the user's Windows machine. Both
boards were finally restored to verified TX-disabled images at 20:44 JST.

## Boundaries still open

- Precise electrical power removal during Flash erase/program/commit requires
  the controlled power fixture the user does not currently have. Manual USB
  removal remains the separate [USB test](M1_USB_POWER_2026-09-07.md).
- Physical bad-CRC frames, electrically held BUSY, full host-machine restart,
  temperature/voltage corners, distance/interference sweeps, and full radio MTU
  payload sweeps are not implied by these results. CRC/BUSY paths are exercised
  by host tests; those are not physical acceptance.
- No secure-session, Join, Relay, fragmentation or application side-effect
  acceptance claim. HIL consumer acceptance only retires a validated test offer.
- Hosted CI is not run; no push, issue comment, project update, or other GitHub
  mutation is performed.
