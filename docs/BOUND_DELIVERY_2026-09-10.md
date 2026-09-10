# Identity-bound delivery: actual Core integration

This increment integrates the cumulative observation/routing/fanout-store work
with the real delivery Core. It does not complete coordinated PHY scheduling,
raise the autonomous 16-member limit, or certify seven physical radios.

## One immutable destination through every retry

`ninlil_submit_bound` accepts a durable submission plus source identity, target
identity, authority identity/generation and target membership/binding epochs.
The source Core must already be storage-bound. A matching radio address alone
never satisfies this contract. The same key with a different payload, contract
or binding is CONFLICT, including attempts to downgrade through legacy submit.
Exact duplicate recovery can return the existing ID while the peer is offline.

The same execution owner supplies `ninlil_config.binding_lookup`. The node
adapter implements this from the current authenticated membership, not from
unauthenticated radio announcements. Current binding is checked before first
admission, before each automatic DATA retry, before final staged transmission
through `ninlil_transmit_check`, and before accepting new outbound receipts.
A mismatch holds the original ownership; it never rewrites the destination,
claims failure/success, cancels the message, or resets a cryptographic counter.

The node adapter uses the Root membership generation as authority generation,
not the Root boot-era counter. An ordinary Root reboot therefore resumes the
binding. Actual Root replacement conservatively holds earlier bound commands;
a separate approved authority migration is needed. This increment does not
claim transparent continuation across authority changes.

## Persistent format and downgrade boundary

The existing journal envelope and DATA/receipt wire formats are unchanged.
New journal record type 11 stores an `NDB1` binding (124 bytes). Its immediately
following bound OUT_CREATE uses record version 6 and the existing 52-byte body
header. Legacy unbound creates still use version 5. An orphan binding prefix
owns no delivery; a bound create without its matching prefix is corrupt.

Both POSIX and raw-Flash journal parsers recognize the additions. Checksums,
read-back verification, committed-corruption rejection and generation fences
remain in force. Binding references are revalidated before use and relocated
by collection. The table keeps an offset rather than another full binding copy.
Private validated enum fields are byte-sized to pay for that reference without
raising the standard role RAM ceilings. No private struct is written to disk.

**After bound records have been written, old binaries must not reopen that
journal.** Old parsers reject the new type/version. Do not erase stores to hide
that rejection. New binaries can open existing v5 unbound histories; there is
no automatic data-destroying conversion or rollback.

## Group/Core crash boundary and retention

`ninlil_fanout_core_connect` installs the real eligible/admit/query callbacks in
a `ninlil_fanout_store_config`. The store keeps its verified contract, targets,
payload and INTENT before invoking the adapter. The adapter checks SHA-256 and
calls real `ninlil_submit_bound`; the store then saves the returned ID. A crash
between those two journals is recovered using the exact saved idempotency key,
not by assuming both stores committed atomically.

Core terminal histories for bound messages stay pinned against automatic
archive eviction. Only after the Group terminal record is durably saved does
`ninlil_fanout_core_step` call `ninlil_release_bound`. This adds record type 12:
permission to reclaim terminal deduplication history, not cancellation or
immediate deletion. If the process stops before that permission is observed,
replay and repeating the handoff are safe. A pending message cannot be released.
The Group's terminal history remains authoritative after Core reclamation.

The application calls Group step and node/Core step sequentially. No threads,
background work, implicit RF start or unbounded queue are introduced. SHA-256
is a caller-supplied reviewed backend; the real-node tests use existing PSA,
while the lower-level native storage tests use OpenSSL. No custom crypto.
The ESP32 component includes the adapter and store sources; selecting a new
Flash partition remains an explicit deployment decision.

## Integration pattern

1. Open the real node and its existing source-bound stores normally.
2. Snapshot each desired peer using `ninlil_node_peer_binding`; sort target
   identities canonically and persist the complete Group contract/target set.
3. Configure a separate Group journal, the source/operation IDs, resource bound
   and SHA-256 callback; connect it to `ninlil_node_core(node)` before opening.
4. Open Group storage using INITIALIZE only for explicitly new/interrupted
   identical creation. Normal restart uses RESUME, never an EMPTY fallback.
5. Drive `ninlil_fanout_core_step(adapter, store, now, work)` and the existing
   node/radio owner sequentially; query per-target and aggregate evidence.

`work` is 1..32 and limits Group service opportunities, not RF simultaneity.
The adapter does not convert the Core's bounded ownership table into unlimited
storage. A large unreachable already-admitted prefix can still exhaust that
configured table. Parking/service-state separation and full 512-node admission
remain distinct unfinished work. Neither a 511-target store fixture nor this
7-node test proves those features.

## Executed tests and their exact boundary

New tests use the actual Core, actual public/private SDK headers and both
existing POSIX and raw-Flash-model journal backends. They cover wrong identity,
membership and binding generations; pre-send/retry/final-send/receipt fences;
legacy downgrade; exact duplicate recovery; active/terminal compaction; archive
pressure and explicit release; committed corruption; and process termination
between binding/create commits.

A separate real Group/Core test exits the child process immediately after real
Core submit has committed and before Group receives its ID. Restart recovers
one identical message, completes delivery and durably transfers retention.

The seven-node test runs real node owners, pinned EDHOC/PSA, Core/control stores
and simulated radio queues. It delivers to six fixed identities across a Root
restart and verifies durable aggregate results after reopen. It explicitly uses
the existing 32-peer powered resource profile for the static seven-member roster;
four candidate-role nodes have no custody capability and remain endpoints.
It does not assert that the default 3-peer endpoint profile supports that roster.
The model and process-cut results are not physical RF or electrical power cuts.

The final compiler/memory-sanitizer, package, firmware and source-budget results
are recorded with the candidate evidence rather than inferred from these tests.
The project remains Draft until all applicable gates and physical acceptance
have evidence. In particular, the unchanged 50,000-line project ceiling is a
release condition, not a number increased to accept accumulated code.
