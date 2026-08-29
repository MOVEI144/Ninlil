# Ninlil foundational contract

Status: design authority for future milestones. This document distinguishes stable project assumptions from milestone-specific algorithms.

## 1. Mission

Ninlil is a small, bounded, product-agnostic communication runtime for intermittent and low-bandwidth edge networks.

It accepts an opaque application message, takes explicit local ownership, transports it to a remote Ninlil runtime, records delivery evidence, and exposes failure or uncertainty without inventing success.

Ninlil is not a product controller, device-management database, cloud API, dashboard, sensor framework, or physical-actuation verifier.

## 2. Responsibility boundary

Ninlil owns:

- local admission and bounded ownership;
- durable outbox and inbox when requested;
- link transmission, retry, deduplication, and receipt transport;
- authenticated peer/session mechanics in security milestones;
- bounded routing and relay mechanics in later milestones;
- transport health and evidence.

The application or product adapter owns:

- the meaning of commands, events, telemetry, and results;
- safety decisions and business rules;
- physical-device control and verification;
- product asset, installation, site, and user authorization models;
- generation or desired-state semantics specific to the product.

A pump-open command and a pump-open-failed result are two independent application messages. Ninlil transports both but does not interpret either.

## 3. Delivery evidence

Evidence stages are independent facts:

```text
LOCAL_ACCEPTED
LINK_SENT
REMOTE_RECEIVED
REMOTE_STORED
APPLICATION_ACCEPTED   optional
APPLICATION_REPORTED   optional evidence only
PHYSICALLY_VERIFIED    product evidence only
```

No lower stage implies a higher stage.

The default durable communication contract is satisfied at `REMOTE_STORED`: the remote runtime has authenticated, deduplicated, and durably accepted responsibility for the logical message.

`APPLICATION_REPORTED` and `PHYSICALLY_VERIFIED` are never default transport-success requirements. A product may request optional evidence, but a business outcome is normally represented by a new correlated application message.

The current M0 `APPLIED`-only satisfaction behavior is a provisional baseline and must be replaced by a per-message required-evidence contract before secure-link API freeze.

## 4. Reliability semantics

- Packet delivery is retryable and may be at-least-once.
- Logical messages are deduplicated by stable message identity.
- `submit` success means Ninlil accepted the declared ownership contract; it never means remote success.
- Ninlil does not claim exactly-once physical effects.
- An application must make physical or external effects idempotent using the message ID, or commit its effect and completion evidence atomically in its own storage.
- Once Ninlil has taken durable ownership, capacity pressure never silently drops that message.
- Ambiguous results become `UNKNOWN`.

## 5. Application message envelope

The portable contract should contain only communication-relevant metadata:

- stable logical message ID;
- caller idempotency key;
- source and target peer identity or resolved short address;
- service identifier;
- opaque payload;
- traffic class;
- delivery profile and required evidence;
- deadline or expiry policy;
- optional correlation ID.

Product vocabulary is not part of the portable wire or core API.

## 6. Traffic classes and one-radio behavior

Ninlil needs bounded traffic classes such as `CRITICAL`, `CONTROL`, `NORMAL`, and `BULK`.

Priority orders messages already owned by the local runtime. It does not let a Parent know about an event that a remote node has not transmitted, does not create a permission-to-send protocol, and does not preempt a frame already on air.

A single SX1262 is half-duplex and is owned by one radio execution context. TX and RX are serialized by the Link/radio adapter. Radio access, collision avoidance, CAD/LBT, scheduled windows, and airtime policy are bearer-level policies selected from traffic modelling and HIL evidence; they are not assumed by the portable delivery core.

Critical traffic requires admission reserve and starvation protection. Marking every message critical must not bypass capacity or regulatory limits.

## 7. Roles and topology

Roles are capabilities, not product identities:

- Endpoint: runs an application and exchanges messages.
- Battery leaf: Endpoint with Relay permanently forbidden.
- Powered Endpoint: may later be authorized as Relay-capable.
- Relay: explicitly authorized powered store-and-forward/custody role.
- Site Gateway / Parent: radio boundary to a Host adapter.
- Host adapter: translates product IPC and authority decisions into Ninlil operations.

Direct one-hop operation is the first accepted topology. Relay is introduced only after direct RF and secure-session acceptance.

Relay is never enabled merely because a node is powered or has good signal. Initial routing uses one active route with bounded backup candidates. Flooding, unrestricted mesh, simultaneous multi-parent delivery, and deep relay chains are not baseline assumptions.

For multi-building sites, multiple Site Gateways connected through a Host or LAN backhaul are preferred over solving every topology with additional LoRa hops.

## 8. Identity, membership, binding, session, and route

These are separate:

- peer identity: stable cryptographic device identity;
- short node address: replaceable routing identifier;
- membership: authorization to participate in a Ninlil domain;
- membership epoch: monotonic authorization generation;
- binding/attachment epoch: host-authorized fence against stale placement or role attachment;
- session: ephemeral authenticated cryptographic context;
- route: replaceable transport path;
- message identity: stable across retries, sessions, and route changes.

Ninlil may store and enforce a generic attachment epoch, but it does not understand a product's site, floor, toilet, pump, or asset model.

## 9. Join and lifecycle

Radio discovery is not membership.

```text
UNSEEN
→ PROVISIONAL
→ AUTHENTICATED
→ HOST_AUTHORIZED
→ MEMBERSHIP_COMMITTED
→ ACTIVE
→ REVOKED
```

EDHOC or another reviewed authenticated-session mechanism proves peer identity. A Host callback decides authorization and binding. QR, dashboard approval, factory assignment, and inventory are product mechanisms that converge on that Host authorization boundary.

A reboot may restore membership, but never silently restores an old traffic-key session. Fresh authenticated session material is required after restart or ambiguous counter state.

Removal and transfer are explicit revocation/recommission operations; radio silence is not removal.

## 10. Security assumptions

- Production messages are authenticated and encrypted after the secure-link milestone.
- No custom handshake is invented when a reviewed standard implementation is available.
- Nonce/counter reuse is fail-closed.
- A Relay does not receive plaintext application payload merely to forward it.
- Secrets are not stored in public QR codes or logs.
- Peer authentication, membership authorization, and product authorization are separate checks.

## 11. Storage and boundedness

All queues, peer tables, route tables, replay windows, provisional joins, and journals have explicit limits.

Persistent formats are protocols and require versioning, corruption tests, torn-write tests, and restart tests.

Endpoint flash stores bounded current communication state, not unbounded operational history. A many-peer Gateway needs either fresh sessions after restart or a later dedicated multi-peer authority store; the single-session counter partition is not a Gateway session database.

## 12. Traffic and payload assumptions

The primary workload is sparse IoT traffic:

- alarms and safety-relevant events;
- commands and correlated application results;
- periodic health and small observations;
- configuration facts.

Continuous media, arbitrary file transfer, firmware images, and streaming are not normal Ninlil traffic.

The current direct-radio profile has a 92-byte radio MTU. Larger logical messages require an explicit fragmentation profile; fragmentation is never silently enabled by the core. Bulk transfer is a separate bounded traffic class and must not starve control traffic.

## 13. Host and product integration

The Host language is not fixed. KGuard keeps its Go `kguard-core` as business authority and connects through a narrow `kguard-lora` Host adapter. Other products may use Rust, C++, or another language.

Ninlil does not require a universal cloud controller or replace a product's durable business database.

## 14. Design envelope versus accepted scope

Long-term design targets may include multiple gateways, powered relays, several hops, simultaneous power restoration, and fragmentation. These are design envelopes, not claims of implemented support.

Each milestone must state separately:

- specified;
- host/model tested;
- target built;
- HIL accepted;
- field accepted.

## 15. Non-goals

Ninlil does not provide:

- product safety or control policy;
- sensor drivers or physical-device semantics;
- asset inventory, installation UI, QR workflow, or tenant authorization;
- exactly-once physical effects;
- unbounded mesh or automatic Relay promotion;
- LoRaWAN compatibility by default;
- large-file or media transport in the base profile;
- regulatory certification by itself.
