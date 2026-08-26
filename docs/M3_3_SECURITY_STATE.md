# M3.3 — ESP32 persistent security state

Status: **software candidate**. Physical power-cut acceptance is still pending.

## Purpose

M3.3 gives the ESP32-S3 runtime two small, independent, fail-closed stores:

1. a transmit-counter reservation store that prevents AEAD nonce reuse; and
2. a membership store that preserves ACTIVE or REVOKED authorization across a
   reboot without silently rolling epochs backward.

It does not implement EDHOC, credential authentication, Join messages, policy,
or Relay. Those layers provide already-authenticated session and membership
facts to this storage boundary. It stores no private key or traffic key.

The counter partition represents one active local transmit context. This fits
an Endpoint, a leaf Sensor, or an upstream Relay session. A many-peer Gateway
must either establish fresh sessions after Gateway restart or use a later
multi-peer authority store; M3.3 does not pretend one partition is a Gateway
session database.

## Partitions

The standard ESP32-S3 partition table contains:

| Label | Subtype | Size | Purpose |
|---|---:|---:|---|
| `ninlil_counter` | `0x41` | 8 KiB | one transmit direction and session |
| `ninlil_membership` | `0x42` | 8 KiB | current authority and membership |

Each partition is exactly two 4 KiB sectors. Only the first 64 bytes of a
sector hold a record; the rest must remain erased. A non-erased tail is hard
corruption.

## Commit protocol

Updates alternate between the two sectors.

```text
select inactive sector
→ erase it
→ verify the entire sector is erased
→ write bytes 0..55
→ read back and compare
→ write the 8-byte commit marker and complement last
→ read back all 64 bytes and compare
→ new sector becomes authoritative
```

The previous committed sector remains intact until the new commit is complete.
A fully erased commit field is an uncommitted record and may be ignored when an
older valid record exists. A partly programmed marker is ambiguous: it could be
a power cut or later corruption of a committed record. M3.3 returns
`NINLIL_ERR_CORRUPT` rather than risk rollback.

When both sectors are valid, their generations must differ by exactly one. The
higher generation is current. Duplicate or skipped generations are corruption.
Any committed CRC error, invalid field, impossible counter high-water mark, or
non-erased sector tail is also corruption.

## Counter reservations

`NINLIL_COUNTER_CREATE_NEW` requires a completely erased partition. It writes
generation 1 and reserves the first block before returning.

`NINLIL_COUNTER_RESUME_EXISTING` requires an exact match of:

- 16-byte session fingerprint;
- packet direction;
- reservation size; and
- exclusive counter limit.

Resume never continues inside the block used by the previous boot. It first
commits the next high-water mark, then starts at the old high-water mark. Unused
counters are intentionally lost, never reused.

The persistent generation count must be able to represent every reservation.
Configurations needing more than `UINT32_MAX` reservation commits are rejected.
The protocol maximum counter is 40 bits. M2.1 may use a narrower session limit;
for example, a 4,096-counter reservation with a `2^20` packet limit needs 256
commits.

If erase, program, or verification becomes uncertain, the open store is
poisoned. It returns I/O errors until closed and reopened. No new counter is
issued from an uncertain reservation.

The session fingerprint is a non-secret identifier derived by the authenticated
session layer. It prevents a partition from being resumed with different key
material; it is not used as a cryptographic key.

## Membership persistence

A record contains:

- authority fingerprint;
- node ID;
- membership epoch;
- binding epoch;
- validated capability bitset; and
- ACTIVE or REVOKED state.

The persistence layer does not assign capability meanings. The Join or policy
layer must reject unknown bits before calling it.

An identical ACTIVE write is idempotent. A replacement must:

- retain the same authority fingerprint;
- strictly increase `membership_epoch`; and
- keep `binding_epoch` equal or higher.

`revoke()` persists REVOKED at a newer storage generation. Reactivation then
requires a higher membership epoch. Formatting is not revocation.

## Ownership and threading

A partition has one execution owner. The portable layer creates no task, mutex,
or hidden global state. The caller owns the I/O context and must keep it alive
until the store is closed. ESP-IDF adapters use `esp_partition_*` synchronously.

## Failure semantics

| Failure | Result |
|---|---|
| power cut before commit field | older valid state survives |
| partial commit field | hard corruption; re-provision/rejoin required |
| commit completed but API returned error | reopen selects newer generation |
| erase claimed success but bytes remain | I/O error and poisoned store |
| committed CRC or field corruption | hard corruption |
| session fingerprint/config mismatch | conflict; never resume |
| authority change or epoch rollback | conflict; never commit |
| exhausted counter/generation space | capacity error |

## Remaining physical gate

Software tests model NOR 1-to-0 writes, partial writes, failed reads, failed or
ineffective erases, and reboot replay. M3.3 is not physically accepted until an
ESP-IDF build and repeated hard-power interruption campaign on the standard
XIAO ESP32-S3 B2B hardware have reproduced these guarantees.
