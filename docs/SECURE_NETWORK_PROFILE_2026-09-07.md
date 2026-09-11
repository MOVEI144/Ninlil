# Secure many-peer and adaptive Relay implementation

Historical September 7 profile. Current autonomous ownership, NRv2 and hardware
results supersede its host-controlled entry points; see
[the September 8 integration](OSS_COMPLETION_2026-09-08.md) and
[source accounting](SOURCE_BUDGETS.md). Removed source is indexed in
[the historical controller record](HISTORICAL_CONTROLLERS.md).

Decision date: 2026-09-07. This is the implementation profile for the user's
conversation steps **4, 5, 6**, not roadmap milestone M6 (bulk/OTA).
The user explicitly ended additional hardware testing and requested completion
of software implementation. Local verification and target compilation remain
required. No connected board is flashed or used for RF testing by this change.

## Purpose and observable result

A product supplies opaque messages and authorization decisions. Ninlil admits
messages durably, authenticates peers, forwards encrypted messages through
powered relays, and reports actual end-to-end storage evidence. If a relay
becomes unavailable, the source retains its original message while the
Coordinator installs an alternative route. Removing a relay requires both
empty custody and removal of all dependent routes.

| Conversation step | Implemented behavior |
|---|---|
| 4 | Real EDHOC mutual authentication; distinct direction keys; authenticated encryption; durable Join/commit/revoke/resume; authorized many-peer Core Link |
| 5 | End-to-end ciphertext plus separately authenticated hops; durable powered Relay custody; retries; repair; drain; route retirement; explicit return of obsolete envelopes after source recovery |
| 6 | Bounded observation graph and route selection; hysteresis; durable prepare/commit/applied plans; restart reconciliation; fixed-profile airtime scheduling; effective-plan Core retry adjustment; fixed/adaptive comparison |

Implementation evidence and remaining physical gates are recorded separately
in [local verification](SECURE_NETWORK_LOCAL_EVIDENCE_2026-09-07.md).
This profile supersedes the initial static-direct slice only for these modules.
It does not claim all Issue #14 proposals, production-security acceptance,
automatic PHY retuning, bulk fragmentation, firmware update, or physical effects.

## Ownership and wire path

```text
Source Core durable outbox (logical ID unchanged)
  -> NL v2 Core packet, up to 104 bytes (64-byte application payload)
  -> E2E AES-CCM envelope, up to 144 bytes
  -> NR v1 route/custody envelope, up to 192 bytes
  -> independent hop AES-CCM envelope, up to 232 bytes
  -> 240-byte physical MTU / bounded airtime queue / existing SX1262 driver
  -> powered Relay commits opaque E2E ciphertext before its hop ACK
  -> final endpoint authenticates and synchronously commits to Core
  -> final hop ACK; separate authenticated Core receipt travels in reverse
```

A hop ACK never retires the original source Core message. `ninlil_ingest` returns
OK only for a matching durable inbound contract or a matching committed receipt
state. Malformed frames, wrong targets, conflicting contracts, unknown receipts,
and storage failures do not authorize final ACKs. Final replay state advances
only after successful Core ingress; an unsuccessful store cannot consume the
only usable retransmission. A 64-entry endpoint ACK cache permits exact envelope
retries without reapplying plaintext. An older evicted envelope may require
fresh-session/source recovery; it is never silently treated as delivered.

The Relay preserves traffic class in its authenticated route envelope and keeps
control/critical traffic eligible for reserved scheduler capacity. Repair changes
only the authorized route and epoch, preserving opaque ciphertext. Reads of
owned relay data revalidate the committed storage envelope before forwarding.

## Security and participation

- EDHOC: pinned libedhoc v1.13.0, method 0, suite 2, message-4 confirmation;
  P-256/ES256, SHA-256, AES-128-CCM with 8-byte tags.
- Private exporter labels 32768 (end-to-end) and 32769 (hop) each derive 74
  bytes: independent two-direction keys, IVs, and a session fingerprint.
- NS v1: 32-byte authenticated header plus 8-byte tag; 40-bit reserved TX
  counter and a 64-packet receive replay window. Header byte 31 is authenticated
  channel 0 (data) or 1 (Join/network control); cross-channel substitution fails.
- The existing durable counter store commits a reservation before encryption.
  Failure closes TX. Keys and receive windows are never restored after reboot.
- Membership binding records both local and peer epochs in the live session.
  It is immutable until fresh EDHOC/open. Changing a policy's epoch cannot
  reactivate an old session, even if the short node address is reused.
- Join: authenticated identity -> Host grant -> Authority PENDING commit ->
  endpoint ACTIVE commit -> Authority ACTIVE commit -> application permission.
  Duplicate acceptance is read-only. A restored membership is not a live session.
- Revoke closes policy immediately, including ambiguous persistence errors.
  Rejoining a revoked identity requires a newer membership epoch. Stable identity,
  Authority, address, binding epoch, service grants, session, and route epoch
  remain separate fields.
- The Host credential callbacks must validate trust and validity periods and
  return the exact expected stable identity. No credential issuer, inventory,
  factory keys, account database, or product approval UI is embedded in Ninlil.
  The profile uses explicitly assigned/provisionally agreed short addresses;
  it does not implement an unauthenticated automatic address-claim service.

EDHOC processing uses a serialized 16 KiB scratch allocator with a nonblocking
try-lock, 1,024-byte message bound, 30-second lifetime and 8 cached retries.
No unsupported external authorization-data extensions are accepted. NF v1
fragments carry at most 224 bytes each, five fragments per message, a random
exchange token, and a five-second reassembly lifetime. Reassembly memory is
caller-owned and admission-bounded; pre-authentication fragments do not write
Flash. The crypto/control owner must erase closed sessions and expire provisional
transactions; successful EDHOC alone never grants a service permission.

## Plans, failure recovery, and safe removal

The Coordinator accepts authenticated observations only from the reporting
edge's source node. A zero-delivery sample is retained as an unusable edge,
not rejected or divided by zero. Periodic `ninlil_coordinator_tick` automatically
stages a proposal when observations require a different route/cost or the active
lease needs renewal. An unchanged stable route is a no-op; a pending transaction
blocks another proposal. Staging never substitutes for real participant ACKs. It uses delivery ratio, airtime and bounded queue cost,
ignores stale observations, limits routes to four hops without loops, and uses
a two-second hold and twenty-percent improvement threshold when the active path
remains usable. Only powered, authorized relay-capable intermediate nodes qualify.

Each route plan has a monotonically increasing epoch, authorized membership
snapshot, permitted fixed PHY profile, bounded retry interval, and a restart-safe
lease of at most 60 seconds. All participants prepare before activation and
report application before the route becomes effective. Missing acknowledgments
leave it incomplete. A restored EFFECTIVE record still needs fresh participant
reconciliation; journal history is not proof of current application.

An unreachable participant cannot report that it released its old plan. The
owner must wait for lease expiry before activating a conflicting replacement.
The same typed boundary accepts an explicit all-participant release fact from
an authenticated controller; callers must not synthesize that fact from a
request having been sent. Retirement is bound to the expected active epoch, persisted and removed from
the live flow table; a delayed request cannot retire a replacement route. Disabling optimization keeps the effective plan and owned messages.

Recovery never restarts a logical message under a new ID. When a destination
session is replaced, a relay may retain now-obsolete ciphertext. After recovering
its authoritative Core store and establishing a fresh context, the authenticated
source can explicitly reclaim those envelopes through `ninlil_relay_return_to_source`.
Each call commits at most one hop retirement. This is return to the original
logical owner, not remote-stored evidence, cancellation, or a retry-limit discard.
Without that source confirmation the relay keeps custody and removal stays blocked.

## Bounds and scheduling

| Resource | Bound |
|---|---:|
| Authority / secure peer table | Caller-owned, up to 512 |
| Provisional Join entries | Up to 64, subject to caller memory/admission budget |
| Service grants per member | 8 |
| Coordinator nodes / directed observations / active flows | 512 / 2,048 / 32 |
| Pending route transactions / path hops | 1 / 4 |
| Relay owned envelopes | Caller-selected, up to 64 |
| Radio staging | 32 frames; at most 8 per peer |
| Critical/control reserves | 4/4 global, 2/2 per peer |
| Airtime scheduler | One active frame; bounded one-second credit; no boot credit |
| Control store | At most 1 MiB configured; ESP partition 128 KiB |
| ESP counter storage | 32 separate 8 KiB slots, e.g. 16 peers with E2E + hop contexts |

The maxima are validation ceilings, not a promise that the largest combination
fits an ESP32-S3. Applications select smaller caller-owned arrays and provisional
counts for their RAM budget. The source Core's existing ownership and deduplication
limits still apply. The control journal is bounded and append-only in this
profile: full storage returns backpressure and retains data; there is no automatic
compaction or infinite membership/plan churn claim. Retirement does not erase
journal history. A deployment needs an explicit retention/storage lifecycle.

`ninlil_routed_apply_rto` converts an effective plan's RTO to the existing Core
step interval. The owner must drive that runtime at its declared step duration;
this is a runtime-wide interval, not an independent timer for each peer. The
separate EWMA helper ignores retransmitted samples and is not a full RFC 6298
implementation. Automatic changes are confined to routes, retries and airtime
allocation within the explicitly permitted profile. Frequency, power, regional
settings and modulation are not chosen autonomously.

## Platform integration and examples

`ninlil_esp_network_pump` joins the portable modules to the existing SX1262 driver:
at most four receive attempts, one bounded Core step, one Relay opportunity and
one physical TX per pump call. Queue admission is not TX_DONE. Ambiguous TX keeps
the staged packet; the driver still enforces its configured carrier-sense,
pause and airtime constraints. Each staged data frame is rechecked against live
membership and keys immediately before TX; obsolete staging can be dropped while
Core/Relay ownership remains. Control callbacks similarly validate current
handshake/Join/plan transactions before transmission and dispatch received
fragments/channel-1 envelopes to their typed state-machine handlers.

The physical MTU is raised from 92 to 240 bytes for this profile. Non-DMA SPI
transfers are split into at most 64-byte transactions under one asserted NSS.
The historical 92-byte simulator remains an explicit comparison baseline.
The new physical packet sizes have software/target-build evidence, not RF HIL.

The `ninlil_secure_network star` and `ninlil_secure_network compare` examples use
the actual Core, EDHOC, Join, secure envelopes, Relay, Coordinator and scheduler.
They generate four distinct lab credentials, write actual POSIX Core/control
journals, and use a deterministic in-process radio model plus a NOR callback
model for fresh-session counters. Those counter fixtures are not persistent
production POSIX security storage. `compare` includes a lost response,
unavailable relay, lease fencing, fresh session, route withdrawal and revoke.
These ephemeral lab identities and model radio are not production providers.

Build (Linux host):

```sh
bash scripts/fetch_edhoc.sh
cmake -S . -B build -G Ninja
cmake --build build
./build/ninlil_secure_network star
./build/ninlil_secure_network compare
```

For ESP-IDF also fetch the pinned SX126x driver. The `ninlil_network` and
`ninlil_edhoc_vendor` components compile/link the profile against IDF's PSA
backend. Existing boot defaults remain TX-disabled. Adding a component is not
provisioning device credentials or granting deployment/physical acceptance.

## Dependency adaptations

The original libedhoc/submodule checkout remains unchanged. Three classes of committed,
hash-checked adaptations are in `third_party/adapted`:

1. Suite-2 helper: use Mbed TLS's validated point-decompression primitive rather
   than the helper's local arithmetic/removed PK API; handle PSA IDs without
   unaligned casts; return exactly 32-byte ECDH coordinates.
2. zcbor encoder: do not call `memmove` with a null source for an empty string.
3. Generated CBOR bindings: 62 exactly typed callback adapters in 32 files,
   replacing incompatible function-pointer casts without changing the wire
   representation or disabling function-type sanitization.

The adapted code is used by both host and target. The host reference snapshot
reports Mbed TLS 3.5.1 and is a reproducible upstream test dependency, not a
recommended production host crypto deployment. ESP-IDF v6.0.2 uses its pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 backend. Recheck the private ECP compatibility boundary on
any SDK migration. `scripts/check_edhoc.py` verifies commits, submodules, source
hashes and the exact patch ledger. The original vendor clone and two larger compatibility copies are counted
separately; the committed CBOR bindings are conservatively included in the
unchanged 50,000-line first-party ceiling.

## Size accounting

The historical M1 checker scanned all new files under `src`/`include`, including
M2-M5 modules. Its 7,000-line limit is retained for the direct-radio slice; the
explicit `scripts/secure_network_files.txt` ledger measures the new profile
separately under a 12,000-line ceiling. The project-wide 50,000-line limit is
unchanged and now also counts Python, CMake modules and JSON/YAML metadata.
This is scope correction and broader accounting, not a numerical budget increase.
