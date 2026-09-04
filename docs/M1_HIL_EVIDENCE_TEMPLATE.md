# M1 HIL evidence record

Copy this file for one physical campaign. Do not replace missing evidence with a
summary claim. Use `PASS`, `FAIL`, `UNKNOWN`, or `SKIP`; only reviewed `PASS`
satisfies a required gate.

## Purpose

- Campaign ID:
- Expected real-world result:
- Issue: `MOVEI144/Ninlil#7`
- Operator:
- Reviewer:
- Start/end time with timezone:
- Overall result:

## Source and toolchain

| Item | Exact value | Evidence or command |
|---|---|---|
| Git commit |  | `git rev-parse HEAD` |
| Worktree clean |  | `git status --porcelain` |
| Host OS/kernel |  |  |
| ESP-IDF |  | Must be v6.0.2 |
| Compiler |  |  |
| Python |  |  |
| esptool.py |  |  |
| CMake/Ninja |  |  |
| Semtech driver commit |  |  |
| Local host gate |  | Command, result, log hash |
| ESP32-S3 build gate |  | Command, result, log hash |

Hosted CI run URL/result, or reason not run:

## Hardware and wiring

| Role | Node/peer | Board identity | USB identity and TTY | Board/hardware revision | Wiring revision | Antenna/load | Photo reference |
|---|---|---|---|---|---|---|---|
| Board A | 1 / 2 |  |  |  |  |  |  |
| Board B | 2 / 1 |  |  |  |  |  |  |

GPIO38 RF-gate polarity confirmation:

- Method:
- Board A result:
- Board B result:
- Reviewer/date:

## RF profile

| Field | Value |
|---|---|
| Region/profile label |  |
| Test location |  |
| Frequency |  |
| TX power |  |
| Bandwidth |  |
| Spreading factor |  |
| Coding rate |  |
| Preamble |  |
| Antenna/load |  |
| Permission/review reference |  |
| Reviewer/date |  |

## Firmware manifest

| Variant | Git commit | Configuration hash/reference | Binary SHA-256 | Size | Target board | Flash log hash | Result |
|---|---|---|---|---:|---|---|---|
| init-a |  |  |  |  | A |  |  |
| init-b |  |  |  |  | B |  |  |
| diag-a-init |  |  |  |  | A |  |  |
| diag-b-resp |  |  |  |  | B |  |  |
| diag-b-init |  |  |  |  | B |  |  |
| diag-a-resp |  |  |  |  | A |  |  |
| delivery-a |  |  |  |  | A |  |  |
| delivery-b |  |  |  |  | B |  |  |

## Phase results

| Phase | Required observation | Result | Artifact references | Notes/unknowns |
|---|---|---|---|---|
| Board identity and clean erase | Fixed A/B identity; verified erase/write; clean journal |  |  |  |
| Radio init A x100 | Complete markers without unexplained reset/fault |  |  |  |
| Radio init B x100 | Complete markers without unexplained reset/fault |  |  |  |
| Diagnostic A to B x1000 | At least 995 matching responses |  |  |  |
| Diagnostic B to A x1000 | At least 995 matching responses |  |  |  |
| Durable A to B x100 | Distinct messages reach `REMOTE_STORED`/`SATISFIED` |  |  |  |
| Durable B to A x100 | Distinct messages reach `REMOTE_STORED`/`SATISFIED` |  |  |  |
| Fault/ambiguity matrix | Stage-specific bounded outcomes |  |  |  |
| Reboot and USB recovery | Replay and identity remain correct |  |  |  |
| Hard-power journal matrix | Recover proven state or fail closed |  |  |  |

## Fault and recovery cases

| Case ID | Injection and index | Message IDs | Expected evidence/outcome | Observed evidence/outcome | Recovery bound | Result | Artifacts |
|---|---|---|---|---|---|---|---|
| DATA loss |  |  |  |  |  |  |  |
| Receipt loss |  |  |  |  |  |  |  |
| Duplicate DATA |  |  |  |  |  |  |  |
| Duplicate receipt |  |  |  |  |  |  |  |
| CRC rejection |  |  |  |  |  |  |  |
| Radio BUSY |  |  |  |  |  |  |  |
| DIO1 fault |  |  |  |  |  |  |  |
| TX timeout |  |  |  |  |  |  |  |
| Sender reboot |  |  |  |  |  |  |  |
| Receiver reboot |  |  |  |  |  |  |  |
| USB reconnect |  |  |  |  |  |  |  |

## Hard-power cases

| Boundary | Cut trigger/index | Repetitions | Expected restart state | Observed restart state | Result | Artifacts |
|---|---|---:|---|---|---|---|
| Erase |  |  |  |  |  |  |
| Record program |  |  |  |  |  |  |
| Commit marker |  |  |  |  |  |  |
| First attempt marker |  |  |  |  |  |  |
| Inbox commit |  |  |  |  |  |  |
| Receipt handoff |  |  |  |  |  |  |
| Terminal evidence |  |  |  |  |  |  |

## Artifact manifest

| Artifact ID | Description | SHA-256 | Durable storage reference | Captured by/date |
|---|---|---|---|---|
|  |  |  |  |  |

## Deviations, failures, and unknowns

- Deviation from canonical procedure:
- Failed cases:
- Unknown or truncated evidence:
- Reruns and why they were necessary:
- Open follow-up:

## Review decision

- Required phases all reviewed `PASS`: yes/no
- M1 acceptance recommendation: accept/reject/pending
- Reviewer and date:
- Issue #7 update/reference:
- `docs/STATUS.md` update/reference:

This record does not claim M2 secure-session, M3 Join/membership, Relay,
scheduled MAC, fragmentation, regulatory certification, or field readiness.
