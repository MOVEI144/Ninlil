# Storage/bulk/radio implementation review

Scope: changes after 6307d02 on codex/static-star-foundation. No hosted CI,
remote publication or device backup was used. Hardware evidence is kept separate
from local software tests and from real electrical interruption during Flash writes.

## Findings resolved

- A 64-bit reference generation expanded every journal reference and exceeded
  the small Core RAM profile. A checked 32-bit generation occupies existing
  alignment space; UINT32_MAX refuses another rewrite. Core collection uses
  constant scratch and relocates references only after publication.
- A deliberately full-log retry-marker test was invalidated by new automatic
  collection. The fixture explicitly selects manual maintenance, retaining all
  original capacity/failure assertions; separate tests exercise automatic GC.
- Scratch publication rejects symlinks, shared/hard-linked inodes and foreign
  ownership before truncation. Failed rename retains the original; failed
  parent sync poisons the live handle. A subprocess exit immediately after
  rename reopens a complete generation.
- Control snapshots retain the latest Join/revocation records, live/pending
  plans and a highest-ever epoch fence. They verify every owned Relay packet
  exists unchanged in the replacement before committing. Old persisted proofs
  never become fresh live session evidence after restart.
- Bulk receiver acknowledgment follows committed bytes. Final adoption follows
  complete-body SHA-256 verification and a committed ready marker. Wrong digest,
  conflicting duplicate bytes and wrong peer binding fail without readiness.
- The bulk restart test initially reset the fake link but omitted rebinding
  its pointers. ASan identified that fixture null-pointer fault; binding is
  restored explicitly, with the reset/loss assertions retained.
- A new empty V command initially intercepted the existing 10-byte revocation
  command. Dispatch now distinguishes the two lengths and retains revocation.
- Actual hardware exposed fatal handling of a cold session in the reference
  bulk worker. STATE/UNAUTHORIZED now pause only the transfer; status exposes
  that result while authentication/control continue. IO/corruption remain
  fatal. POSIX and NOR regressions exercise blocking, later admission and
  unchanged incomplete status; the failed original hardware run is preserved.
- SX1262 PA review rejected the proposed -15/-12 dBm settings before RF use.
  Both policy and driver enforce the actual -9 dBm lower bound. The new bench
  maximum is explicitly -3 dBm. A failed TX-parameter command prevents TX, and
  requested/applied values remain distinct.
- Package exports include the bulk archive, crypto dependencies and selected
  journal backend; a relocated consumer requires the bulk component explicitly.

## Verification and limits

The 64 KiB transfer runs on actual Core APIs with loss, duplicate frames,
sender/receiver restart, completion-receipt loss, collection and normal traffic
coexistence. A separate integration runs bulk through actual EDHOC, route and
opaque Relay modules. New store/cold-session and encrypted-path regressions run
with a 500 ms per-radio transmission bound and receiver restart before receipt
transmission in the encrypted integration. These model checks do not substitute
for real airtime/interference or measured throughput. Those regressions run
under both compilers and both sanitizer builds; the complete existing matrix,
711 vendor cases, 20,000 fuzz iterations and package/static gates also ran locally.

Review checked bounds, ownership, commit-before-effect, replay/duplicate
handling, external frame lengths, persisted record validation and key exposure.
The NOR failure model includes partial writes/erases and write-all-then-error;
ambiguous selectors deliberately fail closed. It does not demonstrate recovery
from arbitrary erasure of both selectors or electrical timing faults.

Full live history still consumes capacity. The application's retained ledger
and complete bulk object are not silently deleted. Old single-bank NOR files
require an explicit migration/storage plan to gain GC. Never downgrade/remove
the companion partitions once GC is in use. The extension does not negotiate
frequency/SF/BW, install OTA images, qualify battery sleep, promise throughput,
or establish antenna/system/field certification.
