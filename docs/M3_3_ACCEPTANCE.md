# M3.3 acceptance specification

M3.3 may be marked `SOFTWARE CANDIDATE` only when all host gates below pass.
It may be marked `ACCEPTED` only after the physical gates also pass.

## Host and model gates

| ID | Requirement |
|---|---|
| SEC-IO-001 | Reject null callbacks and partitions not exactly 8 KiB |
| SEC-IO-002 | Format erases and verifies both sectors |
| SEC-IO-003 | ESP32 adapters require exact labels, subtypes, and sizes |
| SEC-CTR-001 | Fresh create durably reserves before counter 0 is returned |
| SEC-CTR-002 | Resume skips the previous boot's unused reservation |
| SEC-CTR-003 | Session fingerprint, direction, size, and limit must match |
| SEC-CTR-004 | Counter and generation limits return capacity, never wrap |
| SEC-CTR-005 | Torn precommit retains the older high-water mark |
| SEC-CTR-006 | Completed-but-reported-failed commit skips the ambiguous block |
| SEC-CTR-007 | Partial commit marker is hard corruption |
| SEC-CTR-008 | Failed or ineffective erase poisons the live store |
| SEC-MEM-001 | Empty, ACTIVE, REVOKED, and idempotent states replay correctly |
| SEC-MEM-002 | Authority changes and epoch rollback are rejected |
| SEC-MEM-003 | Revoked membership needs a higher epoch to reactivate |
| SEC-MEM-004 | Torn precommit keeps the previous state |
| SEC-MEM-005 | Completed-but-reported-failed commit replays the new state |
| SEC-MEM-006 | Partial marker, CRC error, dirty tail, and generation ambiguity fail closed |
| SEC-REG-001 | Existing M1 tests remain green |
| SEC-QA-001 | GCC and Clang strict builds pass |
| SEC-QA-002 | GCC and Clang ASan/UBSan/leak runs pass |
| SEC-QA-003 | GCC and Clang static analysis pass |
| SEC-QA-004 | ESP32 first-party syntax check passes |
| SEC-QA-005 | M3.3 and project LOC gates pass |

## Physical gates

| ID | Requirement |
|---|---|
| SEC-HIL-001 | ESP-IDF v6.0.2 builds the standard ESP32-S3 target |
| SEC-HIL-002 | Counter reservation survives cuts before, during, and after commit |
| SEC-HIL-003 | No counter observed before a cut is ever returned after reboot |
| SEC-HIL-004 | ACTIVE and REVOKED membership survive the same cut matrix |
| SEC-HIL-005 | Ambiguous partial commit enters a visible fail-closed recovery state |
| SEC-HIL-006 | Flash wear campaign covers at least the full M2.1 session reservation count |

## Evidence

Evidence must record the exact Git commit, compiler and ESP-IDF versions,
partition-table hash, board identity, cut point, result, and raw serial log.
A passed host model is not a substitute for hard-power evidence.
