# Storage, bulk data and bounded radio adaptation

The September 8 user decision adds storage collection, large-message transfer
and automatic radio adjustment. OTA installation and automatic firmware updates
are excluded. Earlier small-message evidence remains tied to its original source.

## Contract

- Collection rewrites current authoritative state into an inactive store,
  verifies it, then durably selects it. Pending messages, required receipts,
  retained deduplication contracts, membership revocations, epoch fences and
  opaque Relay custody survive. It does not silently expire application records.
- POSIX publication uses atomic rename and parent-directory synchronization.
  NOR uses separate data banks and a durable generation selector. Existing
  journals are read without an in-place destructive conversion; extra Flash
  regions must be explicitly provisioned. Ambiguous committed corruption fails
  closed; a failed write never authorizes discarding the authoritative copy.
- Bulk is a separate, service-authorized profile above the existing Core:
  at most 64 KiB per object, bounded chunks/windows and missing-data recovery.
  Object completion requires durable complete bytes and a matching digest;
  fragment receipts alone do not imply object or application completion.
- BULK traffic shares the existing scheduler and cannot consume CRITICAL
  or CONTROL reserves. Restart retains transfer identity and acknowledged work.
- Initial radio adaptation changes transmit power inside an explicit validated
  local policy; it never increases the provisioned maximum. The Japanese fixture
  retains 921.4 MHz, SF7/BW125, CR4/5, 240-byte MTU, carrier sensing and existing
  pause/airtime gates. Adaptive levels are -9, -6 and -3 dBm, explicitly provisioned with maximum -3 dBm.
  The initial -15/-12 proposal was rejected on SX1262 PA range review before
  any physical transmission. The older -9 dBm image evidence is not relabeled. Frequency, SF or
  bandwidth hopping needs a separate interoperable radio-plan profile.

## Acceptance

Require current-state equivalence after repeated collection, restart at each
publication boundary, capacity/corruption rejection, missing/duplicate/reordered
bulk chunks, interrupted resume, conflicting object identities, and fair normal
traffic during bulk work. Power changes require hysteresis, stale-evidence
rejection, bounded changes and hardware driver evidence. Run the full local
compiler/sanitizer matrix, target builds and applicable hardware campaigns.
Real electrical power cuts remain separate from NOR fault injection.

The 50,000-line project ceiling remains unchanged. Historical captures may be
retired from the checkout only with exact recoverable Git revisions and hashes
recorded and verified first; test sources stay normal repository files.

## Integration and storage lifecycle

Link `Ninlil::bulk` plus the chosen journal backend. A powered role must have
BULK slots and the authenticated service grant must allow 64-byte BULK bodies.
Open one `ninlil_bulk` per dedicated object journal with fixed peer, service
and direction. Sender calls begin with an application-chosen nonzero 16-byte
identity, length and SHA-256; writes contiguous 40-byte chunks (short final
chunk allowed); then seals. Same-object repeated writes compare durable bytes.
Interrupted import resumes by repeating begin/writes. Seal verifies the whole
body before any transmission. Changing identity, length or digest conflicts.

Drive `ninlil_bulk_step` on the same execution owner as Core. Receiver uses
`ninlil_receive_class` to consume only its service's BULK messages, leaving
other services/classes for their own application owners. A distinct service
is recommended; the reference firmware uses the existing service 256's BULK
class, while its small-message example owns service 256 NORMAL. There is one
in-flight fragment and no additional transport retry/custody layer. Core
retains loss recovery and priority reserves. Reordered future chunks are
backpressured; duplicates compare already committed bytes. The receiver's
contiguous frontier and the sender's acknowledged frame cursor survive reboot.
Lost final receipts repeat an idempotent final seal/acceptance. Missing object
bytes or a digest mismatch never produce `remote_stored`.

`ready` proves complete local hash-verified storage. `remote_stored` proves the
remote bulk service adopted that complete object, not installation or business
execution. Read allows bounded access to committed bytes; callers wanting only
complete objects must check ready. The caller separately adopts the object.
An object store is not automatically reset to receive another identity: retain
it through the application's deduplication/retention horizon, then provision a
different store under that application's lifecycle. There is no silent TTL,
OTA installer, image activation, arbitrary-size streaming or filesystem API.
The profile is bounded to 64 KiB, one object per store and <28 KiB index RAM.

Automatic collection begins at 75% occupancy and preserves all current owned
payloads, pending receipts, retained completion/deduplication records, current
membership and revocation state, active/pending routes, historical epoch fence
and opaque Relay custody. Core avoids repeating a no-change collection. The
reference application's adopted-message ledger remains caller-owned; collection
does not discard its real history. Full live ownership still applies backpressure.
Collection is synchronous bounded maintenance, not a real-time latency guarantee.
Core's automatic scheduling can be disabled using `ninlil_set_collection` and
scheduled explicitly outside a receive-sensitive window.

POSIX reserves `<journal>.next`, locks and validates that scratch inode, then
syncs the new file, renames it and syncs the parent. Directory publication
uncertainty poisons the live handle until reopen. NOR uses two data banks and
two selector sectors. New file-backed NOR journals allocate both banks (128 or
256 KiB logical); old single-bank NOR files remain readable and report
NOT_FOUND for explicit collection. ESP requires a `<name>_gc` companion with
primary size plus 8192 selector bytes. Missing companions retain legacy mode;
never remove companions or downgrade firmware after collection has begun.
Ambiguous selector erasure/commit or committed corruption fails closed rather
than selecting a potentially obsolete ownership history. Simulated write cuts
are not proof of electrical power-failure behavior.

The reference 8 MiB layout preserves every prior address and appends Core and
control companions, a 256 KiB bulk journal and its companion. Its last region
ends at 0x78a000. No existing store is reformatted. `K` collects node stores;
`O` opens bulk (peer uint16, sending byte); `M` supplies id/length/digest;
`W` writes uint32 offset plus <=40 bytes; empty `V` seals, `J` queries, `Y`
reads uint32 offset/uint16 length<=120, `C` collects bulk, and `T` reports
requested/applied TX power and adaptive-enabled state. Existing `V` with its
10-byte revocation request retains its original meaning. `J` includes a signed
last-step error: unavailable authentication pauses bulk while the node keeps
authenticating; errors do not become completion. The console boots stopped.

## Radio sources and migration decision

Reviewed September 8 against Semtech's
[SX126x implementation](https://github.com/Lora-net/LoRaMac-node/blob/master/src/radio/sx126x/sx126x.c#L520)
(SX1262 branch clamps -9 through +22 dBm) and Seeed's
[Japanese module certificate](https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/113991436-TELEC.pdf)
(LoRa125, 920.6–928.0 MHz, rated maximum 10 mW). No dependency/toolchain change.
The new fixture explicitly provisions -3 dBm maximum (~0.50 mW), adapting
down to -6/-9 dBm with the existing PA, CCA, timing and waveform unchanged.
This is a new bounded bench profile, not a retroactive claim about older image
evidence or a new antenna/system certification. The earlier antenna mapping
limitation remains as recorded in M1_RF_CAMPAIGN_2026-09-07.md.

Only authenticated completed eight-probe windows inform each next-hop policy.
Three fresh perfect windows plus 30 seconds dwell lower power by 3 dB;
<=6/8 replies or evidence older than 60 seconds restore the configured maximum.
Repeated/out-of-order windows do not count as new evidence. Boot/rekey starts
conservatively. Power is staged and applied only at the next TX standby
boundary; a driver command failure prevents TX. Frequency/SF/BW negotiation,
automatic network-wide channel changes and battery duty-cycle tuning remain
outside this explicitly TX-power-only adaptation profile.

## Results

[Local verification](EXTENSION_LOCAL_RESULTS_2026-09-08.json) covers all 54
current CTests under GCC, Clang and each compiler's ASan/UBSan build, including
the added review regressions; it preserves the earlier full-matrix counts and
exact follow-up commands. The 64 KiB object, fake NOR interruption, POSIX
publication failure/exit, digest conflict and slow encrypted receiver-restart
cases pass. The full static/fuzz/vendor/package gates and both default-off and
three configured ESP-IDF builds pass locally; hosted CI was not run.

[Physical results](EXTENSION_HIL_RESULTS_2026-09-08.json) independently reconcile
the same 512-byte object across multiple bounded campaigns. Actual receiver
reset preserved 80 bytes. Subsequent stop/reopen cycles retained progress, and
the last run verified the full readback SHA-256, sender `remote_stored`,
collection after completion and all three owners stopped. The final campaign
took 517.528 seconds, starting with 440 bytes already stored: this is **not**
a 512-byte-in-517-second throughput result. Earlier runs timed out at 120, 240
and 440 bytes. Control/receipt latency on this bench remains a practical
limitation; no full 64 KiB RF or performance/field acceptance is claimed.

The actual policy maintained -3 dBm while probe success windows were below
the lowering threshold. Hardware autonomous decrease/increase is therefore
unobserved; bounds, hysteresis, stale/duplicate evidence and command application
failure are covered by software/driver tests. No electrical Flash write-time
power cut was performed. See the [review record](EXTENSION_REVIEW_2026-09-08.md).
