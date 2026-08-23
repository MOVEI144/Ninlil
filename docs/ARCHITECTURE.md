# Architecture

## Layers

```text
Application / product adapter
        |
Portable delivery core
        |
Security / Join / Relay layers (milestone-specific)
        |
Bounded Link adapter
        |
Radio or stream transport
        |
Platform persistence and clock ports
```

The portable core does not create tasks, threads, sockets, or hidden global state. Platform owners provide explicit storage, random, clock, and link operations.

## Evidence stages

Ninlil keeps these facts distinct:

```text
accepted locally
sent to a link
received remotely
stored remotely
applied by the remote application
verified by physical evidence
```

No lower stage implies a higher stage.

## Standard deployment roles

- **Endpoint**: application device; may be battery powered and may forbid Relay.
- **Relay**: powered forwarding/custody node introduced only after direct-radio acceptance.
- **Site Gateway / Parent**: ESP32-S3/SX1262 boundary to a host over USB or another explicit host adapter.
- **Host adapter**: product-specific process translating Ninlil facts into a product IPC or domain model.

The host language is not part of the wire contract. A product may use Go, Rust, C++, or another language without moving its business rules into Ninlil.

## Persistence

Persistent formats are protocols. Changes require versioning, corruption tests, torn-write tests, restart tests, and an explicit compatibility decision.
