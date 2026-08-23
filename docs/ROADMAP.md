# Roadmap

## B0 — Canonical baseline import

Import the reviewed compact C11 baseline into `MOVEI144/Ninlil`, add governance and remote CI, and verify that the imported tree is reproducible.

## M1 — Direct radio physical acceptance

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
- identity, membership epoch, and binding epoch separation;
- durable ACTIVE/REVOKED state;
- restart and transfer tests.

## M4 — Relay

- powered Relay only;
- one active parent plus bounded backup candidates;
- durable custody and route replacement;
- Relay drain and removal;
- two-hop and three-hop HIL.

## M5 — Scheduled access and join storm

- airtime budget and bounded radio calendar;
- simultaneous power restoration and Join admission;
- priority and fairness;
- measured operational limits.

## M6 — Fragmentation and bulk transfer

- a separate bounded traffic class;
- resumable fragments and missing bitmap;
- explicit total-size limit;
- no starvation of control traffic.
