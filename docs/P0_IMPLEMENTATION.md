# P0 contract implementation

Status: implementation candidate. Host and target gates must be recorded in the draft PR before this status can be promoted.

## Delivery API and state

The P0 public API is version 2 (`NINLIL_API_VERSION`). A submission explicitly carries durable ownership, required evidence, traffic class, and an optional absolute deadline. P0 admits durable ownership only; it never falls back to volatile ownership.

The supported end-to-end requirements are:

- `REMOTE_STORED` (set by `ninlil_submission_defaults()`);
- `APPLICATION_ACCEPTED` (optional stronger handoff evidence).

`ninlil_submit()` returning `NINLIL_OK` means the complete body and contract committed to the local journal. It does not mean link transmission or remote success. A durable attempt marker commits before the first call to the Link send operation.

Inbound DATA is completely decoded and authorized before journal mutation. After the authoritative inbox commit, the receiver schedules a `REMOTE_STORED` receipt without waiting for Application handling. `ninlil_receive()` offers a stored message at most once in one boot. A restart offers it again until `ninlil_application_accept()` commits explicit adoption. Application results remain independent opaque messages.

A missing receipt leaves the message active. Local expiry is committed only if restart-safe time proves the deadline passed before any transmission attempt. After an attempt, lack of evidence is not enough to infer expiry or failure. `UNKNOWN` requires an explicit integration decision after an attempt; cancellation is allowed only before an attempt. An explicit peer permanent rejection produces `FAILED`.

## Versioned formats

The unreleased baseline formats are deliberately rejected rather than migrated silently:

- DATA and receipt wire version: 2;
- POSIX journal envelope: `NJL2`, version 2;
- delivery journal record version: 2;
- raw-Flash journal envelope and commit marker: version 3.

DATA carries no generic correlation field. Application correlation remains in the opaque payload.

Payload bodies stay in the authoritative journal. Runtime arrays contain bounded metadata and journal references; they do not retain one fixed payload buffer per live message. Journal growth is capped by the role's Flash ceiling and reports capacity rather than growing without bound. Completed identities remain in the role's bounded dedupe cache and are eventually replaced according to that declared bound.

## Operational modules

- `ninlil_role_profile`: exact standard Leaf, Endpoint, Relay-candidate, and Gateway bounds. Relay-candidate Runtime open remains disabled.
- core admission: protected CRITICAL and CONTROL reserve plus `8:4:3:1` scheduling, with no more than eight consecutive CRITICAL selections while another eligible class waits.
- `ninlil_custody`: caller-backed Host/Gateway spool metadata, ordered log replay, and saturating 250/500 ms to 30 s reconnect delays. Its durable callback commits before state changes. Gateway custody does not release Host payload ownership; only final `REMOTE_STORED` does.
- `ninlil_leaf`: LAB RX1/RX2 opportunities, three-second listen bound, 30-second maximum delay, and one staged downlink per wake.
- `ninlil_topology`: eight-Gateway bound, diagnostic reception observations, a volatile pre-inbox dedupe cache, one active downlink lease, two backups, one-hour maximum leases, and monotonic route epochs.
- `ninlil_group`: Host-side explicit target snapshots with ordered restart replay, four retained operations, 512 targets per operation, and 32 group-derived in-flight deliveries. Completed operations remain exact-idempotent until explicit durable forget.
- service authorization: known capability mask, role-trait validation, fresh-session membership epoch, and bounded direction/payload/class/live-message grants. Application services start at `0x0100`. Authorization is default-deny.

Custody, route, and group durable callbacks return success only for a proven authoritative commit. An ambiguous callback error poisons the in-memory view and requires replay. USB writes, serial acknowledgements, OS buffering, radio TX completion, and sleeping Leaf state are not custody or failure evidence.

## Non-claims

P0 does not implement secure-link authentication, EDHOC, Join exchange, Relay forwarding, production MAC/CAD/LBT, fragmentation, OTA, on-air reliable broadcast, automatic Gateway scoring, or physical RF/power-cut acceptance. Permanent rejection receipts rely on the future authenticated-link boundary for production security.
