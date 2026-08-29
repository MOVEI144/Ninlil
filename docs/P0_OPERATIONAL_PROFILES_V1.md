# P0 Operational Profiles and Topology Contract v1

Status: design decision batch 2. This document fixes six bounded operational contracts that refine `FOUNDATIONS.md` and `P0_DELIVERY_CONTRACT_V2.md`. It does not claim that Relay, multi-Gateway routing, group radio optimization, or Battery Leaf HIL is implemented.

## Decision 1 — Role profiles, queue limits, RAM, and Flash

A queue slot means one live logical message, not one radio retry packet. Durable payload bodies live in authoritative Flash or Host storage. MCU RAM holds bounded metadata, indexes, and active packet buffers; correctness must not depend on PSRAM.

The first standard profiles are:

| Profile | Active peers or members | Outbound owned | Inbound stored | Relay custody | Dedupe IDs | Service grants | Ninlil DRAM ceiling | Dedicated communication Flash ceiling |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Battery Leaf | 1 active parent | 8 | 2 | 0 | 32 | 8 | 16 KiB | 256 KiB |
| Powered Endpoint | 1 active parent + 2 route candidates | 32 | 32 | 0 | 128 | 16 | 32 KiB | 512 KiB |
| Powered Relay candidate | 64 authorized peers | 16 local | 16 local | 64 | 256 | 16 local | 64 KiB | 1 MiB |
| Site Gateway / Parent | 512 ACTIVE members, 64 PROVISIONAL | 128 radio-custody transactions total | included in custody total | future explicit Relay only | 1024 | 64 policies | 128 KiB | 1 MiB local spool plus bounded Host spool |

The Gateway may run at most 8 authenticated handshakes concurrently. The Relay profile is a design envelope only and remains disabled until the Relay milestone.

The current ESP32 partition map uses a 128 KiB delivery journal and two 8 KiB security partitions. That fits inside the Leaf ceiling, but production compaction, wear, and hard-power acceptance remain separate gates.

### Admission reserves

The standard local outbound reserves are:

| Profile | CRITICAL reserve | CONTROL reserve | Shared slots | BULK maximum |
|---|---:|---:|---:|---:|
| Battery Leaf | 2 | 1 | 5 | 0 |
| Powered Endpoint | 4 | 8 | 20 | 4 |
| Powered Relay local traffic | 2 | 4 | 10 | 0 |
| Site Gateway radio custody | 16 | 32 | 80 | 8 |

Reserved slots are minimum protected capacity, not class-specific hard partitions. CRITICAL may use CRITICAL reserve plus free shared capacity but may not consume the CONTROL reserve. CONTROL may use its reserve plus shared capacity but may not consume the CRITICAL reserve. NORMAL and BULK may never consume protected CRITICAL or CONTROL reserve.

The default local scheduling weight is `CRITICAL:CONTROL:NORMAL:BULK = 8:4:3:1`, with a maximum of 8 consecutive CRITICAL selections when another class is pending. Empty, clock-blocked, and proven-expired attempted entries are skipped, so one deadline-bearing item cannot head-of-line block eligible traffic. A packet already handed to a bearer is never preempted. Every traffic-class input is validated across the complete `CRITICAL..BULK` range before any mask, shift, schedule lookup, or class-array index.

A standard Host Adapter profile is bounded to 4,096 live transport messages, 64 MiB total spool bytes, and 8 pending downlinks per peer. A zero-byte custody item is valid when it has a nonzero durable payload token: it consumes one live-message and per-peer slot but zero bytes. Products may choose lower limits. Higher limits require an explicit memory, disk, recovery-time, and abuse analysis.

## Decision 2 — Host, Adapter, Gateway, and Endpoint custody

Product state and transport custody are separate. A Product Core retains its operation record until the product reaches its own terminal outcome. Ninlil transport uses stable message identity across every local boundary.

### Host to Endpoint

```text
Product Core durable operation
→ Host Adapter durable spool
→ Site Gateway durable custody
→ Endpoint authoritative inbox
```

The evidence meanings are:

- `HOST_ADAPTER_STORED`: the Host Adapter durably owns retry toward a Gateway;
- `GATEWAY_CUSTODY`: a Site Gateway durably owns radio forwarding;
- `REMOTE_STORED`: the final target Runtime committed the message to its authoritative inbox.

`HOST_ADAPTER_STORED` and `GATEWAY_CUSTODY` are hop or integration evidence. They do not satisfy an end-to-end message whose target is the Endpoint.

A USB write, serial acknowledgement, OS buffer flush, or radio TX completion never transfers durable custody by itself.

The Host Adapter keeps the payload until end-to-end `REMOTE_STORED`, even after `GATEWAY_CUSTODY`. This deliberate duplicate custody allows Gateway replacement and replay without reconstructing a product command from transport diagnostics.

### Endpoint to Host

```text
Endpoint durable outbox
→ Site Gateway durable custody
→ Host Adapter authoritative transport inbox
→ Product Core application handoff
```

For a message addressed to the Host Runtime, `REMOTE_STORED` means the Host Adapter committed it. Gateway storage alone is intermediate custody.

The Gateway retains inbound data until the Host Adapter returns a durable acknowledgement. The Host Adapter retains it until the Product Core explicitly accepts application handoff under the P0 at-least-once rule.

On USB reconnect, the Host Adapter reoffers every non-terminal outbound message, and the Gateway replays every unacknowledged inbound message. Both sides deduplicate by stable logical message ID. A complex pending-inventory negotiation is not required for the first implementation.

## Decision 3 — Battery Leaf uplink, poll, and downlink

A Battery Leaf is uplink-capable whenever its own Application wakes it. It does not require Parent permission to report an event.

The standard behavior is:

```text
event or periodic wake
→ submit and send pending uplink
→ treat that uplink as a poll opportunity
→ open bounded receive window(s)
→ receive at most one staged downlink logical message
→ send required receipt when possible
→ sleep
```

Rules:

- event traffic such as a leak is admitted and attempted immediately after local filtering;
- every successful uplink or explicit poll advertises a downlink opportunity;
- the Host Adapter holds at most 8 pending downlinks per Leaf;
- the Gateway stages at most 1 downlink logical message for the current wake cycle;
- additional downlinks wait for later polls;
- sleep is not offline, removal, or delivery failure;
- the next wake estimate is advisory and never authoritative;
- a product requiring immediate unsolicited downlink must use a Powered Endpoint, not a Battery Leaf.

The portable core does not hard-code milliseconds. The first lab bearer profile uses:

```text
RX1: start 200 ms after uplink TX completion, listen 800 ms
RX2: optional recovery window, start 2200 ms after TX completion, listen 1200 ms
```

These are LAB defaults only. A production radio profile must provide measured timing, clock-drift margin, energy cost, legal airtime rules, and HIL evidence. Total receive-window time per ordinary wake is bounded to 3 seconds unless a reviewed product profile says otherwise.

A durable uplink remains pending across sleep cycles until its required evidence arrives or a terminal outcome is proven.

## Decision 4 — Multiple Site Gateways

A Ninlil domain may expose at most 8 Site Gateways in the initial design envelope. They remain subordinate radio heads or custody nodes under one Host authority; they are not independent competing controllers.

Uplink rules:

- any authorized Gateway may receive an uplink;
- multiple Gateways may report the same message ID;
- the Host Adapter deduplicates the logical message once;
- per-reception RSSI, SNR, timestamp, Gateway UID, and radio profile remain diagnostic observations and are not merged into the Application payload.

Downlink rules:

- one peer has exactly one active downlink Gateway;
- at most 2 backup Gateway candidates are retained;
- simultaneous multi-Gateway downlink transmission is not a baseline feature;
- the Host issues a bounded route lease containing active Gateway identity and a monotonic `route_epoch`;
- failover increments `route_epoch` and preserves logical message identity;
- a previous Gateway must release or lose its lease before a replacement transmits ordinary downlink traffic;
- authoritative replay rejects a different-Gateway lease whose start overlaps an unreleased prior lease, while allowing same-Gateway renewal, explicit release then replacement, and replacement whose start is at or after prior expiry.

Gateway selection uses recent bidirectional delivery evidence, retry rate, queue health, radio faults, and signal history. RSSI alone is insufficient.

A LAN or Host partition must not create two active controller writers. If authority is ambiguous, new downlink admission fails closed while already owned messages remain recoverable.

For multi-building sites, multiple Gateways over Host or LAN backhaul are preferred over adding deeper LoRa hops merely to cover geography.

## Decision 5 — Broadcast and reliable group delivery

Raw radio broadcast is not a durable delivery primitive in P0.

It may be used only for bounded best-effort control-plane functions such as discovery beacons or time hints. It never produces `REMOTE_STORED` for an unknown set of receivers.

A reliable group operation is represented at the Host as:

```text
one product operation
→ explicit target snapshot
→ one logical Ninlil delivery per target
→ one outcome per target
```

The initial implementation uses unicast deliveries. Ninlil exposes per-target outcomes; ALL, quorum, partial-success, or product safety aggregation belongs to the Product Adapter.

Limits:

- maximum target snapshot: 512 peers;
- maximum concurrent group operations: 4;
- maximum group-derived deliveries admitted to one Gateway at once: 32;
- the remainder stays in the bounded Host spool and is interleaved with non-group traffic.

Receivers never send immediate uncoordinated acknowledgements to a reliable radio broadcast. Any future on-air group optimization requires authenticated group membership, a group epoch, bounded response scheduling or selective query, missing-target repair, and proof that it does not starve CRITICAL or CONTROL traffic.

Battery Leaves are not assumed to be simultaneously awake, so an urgent reliable group downlink cannot include them unless the product accepts deferred per-Leaf delivery.

## Decision 6 — Service and capability authorization

Membership grants communication authority; receiving a valid encrypted packet does not by itself authorize every service or direction.

Authorization is default-deny and contains:

```text
role and role traits
validated capability bitset
bounded service grants
```

The initial generic capability set is:

- `APP_SEND`;
- `APP_RECEIVE`;
- `POLL_DOWNLINK`;
- `GROUP_RECEIVE`;
- `RELAY_CUSTODY`;
- `GATEWAY_RADIO_HEAD`.

Unknown capability bits are rejected during Join and current policy validation. The persistent membership store is a generic bitset container and preserves unknown bits in structurally valid records so a newer writer does not make an older reader misclassify durable storage as corrupt. A Battery Leaf with `RELAY_CUSTODY` is invalid even if a Host attempts to grant it.

Each service grant contains:

```text
service_id
allowed direction: send, receive, or both
maximum payload bytes
allowed traffic-class mask
maximum live messages for that service
```

Service IDs `0x0000..0x00ff` are reserved for Ninlil control traffic. Application services use `0x0100..0xffff`; Ninlil assigns no product meaning to those IDs.

The per-role service-grant limits are the values in Decision 1. A capability or service-grant change requires a higher membership epoch and a fresh authenticated session before use.

An authenticated but unauthorized message is rejected before authoritative inbox mutation. Before returning a generic permanent-rejection receipt, the receiver commits an exact-contract tombstone in a `max_inbound`-bounded table shared with durable receiver-expiry terminals. Matching duplicates re-arm that rejection across restart and policy changes; conflicting identity reuse fails closed. Tombstones are not evicted, so full capacity or storage failure backpressures without emitting a terminal receipt that cannot be upheld. The four-step interval is consumed by every Link attempt, including `BUSY` and `CAPACITY`, so a high-work step cannot spin on a failing bearer. The receipt must not reveal secret policy details or create an amplification path.

A Relay forwards protected payload under Relay authority without receiving Application service permission and without learning plaintext merely to forward it.

## Required implementation consequences

- introduce explicit role-profile configuration and validate every limit at open;
- separate payload persistence from RAM indexes before enabling Gateway-scale profiles;
- implement Host/Gateway custody receipts distinct from end-to-end evidence;
- add idempotent USB replay in both directions;
- add a Battery Leaf poll/wake contract to the radio adapter, not the portable core scheduler;
- add Host-side multi-Gateway dedupe and active-parent lease state;
- keep reliable group expansion Host-side for the first implementation;
- enforce capability and service grants before inbox commit;
- add tests for every capacity boundary, reconnect, duplicate Gateway reception, stale route epoch, sleeping Leaf, unauthorized service, and group admission wave.

## Explicitly deferred

- production RF timing values for Battery Leaf windows;
- automatic Gateway scoring constants and failover timeouts;
- on-air group optimization;
- Relay implementation and route discovery;
- group keys;
- CAD/LBT, channel calendar, and airtime scheduler details;
- fragmentation and firmware transport.
