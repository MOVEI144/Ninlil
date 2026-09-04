# Roadmap

## B0 — Canonical baseline import

Import the reviewed compact C11 baseline into `MOVEI144/Ninlil`, add governance and remote CI, and verify that the imported tree is reproducible.

## P0 — Portable delivery and operational contracts

- replace APPLIED-only satisfaction with per-message required evidence;
- make `REMOTE_STORED` the default durable communication boundary;
- define Application at-least-once handoff and terminal outcome semantics;
- add bounded traffic classes, role profiles, and admission reserves;
- separate Host Adapter, Gateway custody, and end-to-end evidence;
- define Battery Leaf poll/downlink behavior, multi-Gateway authority, reliable group expansion, and service authorization;
- version wire and persistent formats before secure-link API freeze.

The design authority is:

- [`P0_DELIVERY_CONTRACT_V2.md`](P0_DELIVERY_CONTRACT_V2.md);
- [`P0_OPERATIONAL_PROFILES_V1.md`](P0_OPERATIONAL_PROFILES_V1.md).

## M1 — Direct radio physical acceptance

Canonical procedure: [`M1_HIL_ACCEPTANCE.md`](M1_HIL_ACCEPTANCE.md).

- exact ESP-IDF v6.0.2 build;
- two XIAO ESP32-S3 + Wio-SX1262 boards;
- RF-path polarity and radio profile confirmation;
- bidirectional durable delivery;
- packet-loss and reboot tests;
- hard-power journal campaign.

## M2 — Secure direct session

- authenticated session establishment using a reviewed EDHOC implementation;
- AEAD envelope, replay window, and durable transmit counter;
- fresh-session requirement after ambiguous security state;
- two-board secure RF HIL.

## M3 — Join, membership, revoke, and resume

- provisional discovery;
- host-authorized activation;
- identity, membership epoch, binding epoch, and service authorization separation;
- durable ACTIVE/REVOKED state;
- restart and transfer tests.

## M4 — Powered Relay prototype

- powered Relay only;
- one active parent plus bounded backup candidates;
- durable hop custody distinct from end-to-end evidence;
- Relay drain and removal;
- two-hop and three-hop HIL.

M4 may produce a prototype, but field acceptance is blocked until M5 proves direct and relayed airtime, priority, collision, and join-storm limits.

## M5 — Measured channel access and operational limits

- traffic model for 1, 10, 50, 100, and 512 nodes;
- airtime budget and bounded radio calendar;
- CAD/LBT, backoff, periodic offsets, or scheduled windows selected from simulation and HIL evidence;
- simultaneous power restoration and Join admission;
- priority, fairness, broadcast-response control, and Battery Leaf timing;
- multi-Gateway failover and measured operational limits;
- Relay field-acceptance capacity gate.

## M6 — Group optimization, fragmentation, and bulk transfer

- authenticated group membership and bounded response strategy if on-air group optimization is justified;
- a separate bounded BULK traffic class;
- resumable fragments and missing bitmap;
- explicit total-size limit;
- no starvation of CRITICAL or CONTROL traffic;
- no firmware, media, or arbitrary file-transfer claim without a separate profile and acceptance gate.
