# M1 direct-radio physical acceptance

Status: procedure defined; physical execution not yet accepted.

This document is the canonical procedure for Issue #7. It turns the M1
software candidate into reviewable physical evidence without treating a build,
a USB write, radio TX completion, or a local log line as proof of remote durable
storage.

## Purpose and expected result

Two fixed XIAO ESP32-S3 + Wio-SX1262 B2B assemblies exchange diagnostic and
durable messages in both directions. The campaign proves the required evidence
stage across restart, disconnect, injected radio faults, and selected hard-power
boundaries. Every result is tied to an exact source commit, firmware image,
board identity, RF profile, and preserved artifact.

M1 acceptance is a laboratory result. It is not regulatory certification,
field readiness, production security, Relay acceptance, or multi-Gateway
acceptance.

## Evidence language

Record each observation at exactly one of these stages:

- `LOCAL_ACCEPTED`: the sender durably owns the message.
- `LINK_SENT`: a link operation accepted the packet or the radio reported TX
  completion.
- `REMOTE_RECEIVED`: the peer decoded the packet.
- `REMOTE_STORED`: the peer durably committed the message and the sender
  received the corresponding evidence.
- `APPLICATION_ACCEPTED`: the remote Application durably adopted the message.
- `SATISFIED`: the message reached its declared required-evidence stage.

A lower stage never proves a higher stage. Missing, timed-out, truncated, stale,
or ambiguous evidence is `UNKNOWN`, not inferred `PASS` or `FAIL`.

Use these phase results:

- `PASS`: all stated observations and artifacts are present.
- `FAIL`: a stated completion condition was observably violated.
- `UNKNOWN`: the outcome cannot be proved from preserved evidence.
- `SKIP`: the phase was deliberately not run.

Only `PASS` satisfies a required M1 gate.

## Canonical hardware and roles

The required M1 campaign uses exactly two fixed assemblies:

| Role | Node | Peer | Required identity |
|---|---:|---:|---|
| Board A | 1 | 2 | Stable USB identity and exact TTY path |
| Board B | 2 | 1 | Stable USB identity and exact TTY path |

Extra boards may be used after the canonical A/B phases for repeatability or
pairwise exploration. They do not change M1 completion conditions and must not
be described as Relay or multi-Gateway evidence. A separate reviewed plan must
assign each extra board a unique node ID, role, USB identity, and firmware hash.

## Required equipment and environment

- Two known-good XIAO ESP32-S3 + Wio-SX1262 B2B assemblies.
- Correct antennas or reviewed RF loads attached before powering the radio.
- Independently identifiable USB data connections.
- Linux host with ESP-IDF v6.0.2 and the pinned Semtech driver revision.
- A reviewed local RF profile covering region, frequency, output power,
  bandwidth, spreading factor, coding rate, preamble, antenna, location, and
  operator.
- A controlled power-interruption fixture for hard-power phases. Pulling USB by
  hand is not sufficiently repeatable for a boundary-specific power campaign.
- A durable artifact location for raw serial logs, photographs or video, build
  output, configuration snapshots, and hashes.

Do not enable TX until antenna/load, local RF permission, frequency, output
power, and GPIO38 RF-gate polarity have been reviewed for the exact assemblies.
Repository defaults intentionally leave TX disabled and frequency unset.

## Campaign preparation

1. Create a campaign ID such as `m1-YYYYMMDD-01` and copy
   [`M1_HIL_EVIDENCE_TEMPLATE.md`](M1_HIL_EVIDENCE_TEMPLATE.md) into the review
   branch or evidence record.
2. Start from a clean canonical commit. Record `git rev-parse HEAD` and prove
   `git status --porcelain` is empty.
3. Record the host OS, ESP-IDF `v6.0.2`, compiler, Python, `esptool.py`, CMake,
   Ninja, and pinned Semtech revision.
4. Run the complete local host gate and ESP32-S3 build gate. A passing build is
   prerequisite evidence only; it does not complete a physical phase.
5. Assign the fixed Board A/B identities and capture their USB identities and
   exact TTY paths before flashing.
6. Inspect both assemblies, connect antennas or loads, and record the confirmed
   GPIO38 RF-gate polarity.
7. Review and record the RF profile. Never reuse a configuration snapshot whose
   board role or RF assumptions are unclear.

## Firmware variants and traceability

Create a separate configuration snapshot and firmware hash for every distinct
role and phase. At minimum the campaign needs:

| Variant | Board settings | Purpose |
|---|---|---|
| `init-a` | node 1, peer 2, 100 init cycles | Board A radio initialization |
| `init-b` | node 2, peer 1, 100 init cycles | Board B radio initialization |
| `diag-a-init` | node 1 initiator, peer 2, 1,000 pings | A to B diagnostic |
| `diag-b-resp` | node 2 responder, peer 1 | A to B diagnostic |
| `diag-b-init` | node 2 initiator, peer 1, 1,000 pings | B to A diagnostic |
| `diag-a-resp` | node 1 responder, peer 2 | B to A diagnostic |
| `delivery-a` | node 1 submits 100, peer 2 | A to B durable delivery |
| `delivery-b` | node 2 submits 100, peer 1 | B to A durable delivery |

For each image preserve the complete `sdkconfig`, source commit, build command,
build log, binary SHA-256, binary size, flash command, target USB identity, and
flash result. A filename or build timestamp is not an image identity.

## Physical phases

### 1. Identity, erase, and clean start

- Verify Board A and Board B never exchange USB identities or TTY ownership.
- Full-flash erase both boards and preserve erase logs.
- Flash the first phase-specific images and preserve write and verification
  output.
- Capture both fresh-boot markers, reset reasons, active variant IDs, and proof
  that the M1 delivery journal starts clean.

Completion requires two unambiguous board identities, verified flash writes,
matching firmware hashes, and clean-start evidence.

### 2. Radio initialization

- Run the 100-cycle initialization/deinitialization variant on Board A.
- Repeat on Board B.
- Preserve every failure and recovery marker; do not keep only the final summary.

Completion requires `NINLIL_HIL_INIT result=PASS cycles=100` on each board with
no unaccounted reset, watchdog event, radio fault, or truncated log.

### 3. Bidirectional diagnostic RF

- Run Board A initiator to Board B responder for 1,000 distinct PING sequences.
- Reflash the reversed variants and run Board B initiator to Board A responder
  for 1,000 sequences.
- Capture both boards from fresh boot through the final campaign marker.

The reviewed threshold is at least 995 matching PONG responses for each 1,000
PING campaign. Sequence, source, peer, timeout count, reset reason, and both
side logs must agree. The summary line alone is insufficient.

### 4. Bidirectional durable delivery

- Full-flash erase or use the reviewed clean-start procedure before the first
  durable phase.
- Run 100 campaign-bound messages from Board A to Board B.
- Reverse fixed roles and run 100 new campaign-bound messages from Board B to
  Board A.
- Preserve sender and receiver records across the complete evidence path.

Completion requires 100 distinct messages in each direction, matching identities
and payload contracts, receiver durable commits, and sender `REMOTE_STORED` and
`SATISFIED` evidence. Do not infer `APPLICATION_ACCEPTED` unless that stronger
evidence was explicitly requested and durably recorded.

### 5. Fault and ambiguity campaigns

Run bounded, reproducible cases for DATA loss, receipt loss, duplicate DATA,
duplicate receipt, CRC rejection, radio `BUSY`, DIO1 fault behavior, and TX
timeout. Record the injection method, exact message IDs, attempt indexes,
expected stage, observed stage, and recovery bound.

A case passes only when retry/deduplication behavior and the final evidence stage
match the declared expectation. If the injection or observation is incomplete,
record `UNKNOWN`.

### 6. Reboot and USB disconnect/reconnect

Run controlled sender reboot, receiver reboot, monitor-host restart, and USB
disconnect/reconnect cases. Record reset reasons, journal replay indexes, USB
identity before and after, pending message identities, and final evidence.

Communication must recover without silently changing board identity, losing
durably owned messages, duplicating Application acceptance, or inventing remote
success.

### 7. Delivery-journal hard power

Use the reviewed power fixture to interrupt erase, program, commit-marker, first
attempt-marker, inbox-commit, receipt, and terminal-evidence boundaries. Repeat
each boundary enough times to cover the documented timing window and preserve
the cut trigger/index with every boot log.

After restart, the journal must either recover the last proven committed state or
fail closed visibly. An ambiguous write must never become a successful delivery.

## Artifact and review requirements

Preserve a manifest containing:

- source, toolchain, dependency, configuration, and firmware hashes;
- hardware and wiring revisions and dated board photographs;
- RF profile and its reviewer;
- raw logs and their SHA-256 values;
- reset reasons, phase markers, injection indexes, and flash history;
- every `FAIL`, `UNKNOWN`, and `SKIP`, including reruns;
- storage references for artifacts not committed to Git.

Do not commit credentials, signing material, network secrets, or unrelated host
information. Repository ignore rules exclude binary and log artifacts, so the
review branch normally contains the human-readable record and hashes while raw
artifacts remain in the declared durable evidence location.

## Acceptance decision

M1 is accepted only when every required phase has reviewed `PASS` evidence and
Issue #7 links to the committed evidence record. Update `docs/STATUS.md` in the
same evidence PR. Keep M2 secure-session, M3 Join/membership, Relay, scheduled
MAC, fragmentation, regulatory, and field-readiness claims explicitly separate.

