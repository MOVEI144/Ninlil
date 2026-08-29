# P0 Delivery Contract v2

Status: design decision batch 1. This document refines `FOUNDATIONS.md` into six implementation decisions for the portable delivery core. It does not specify EDHOC, Join, Relay, or radio MAC algorithms.

## Decision 1 — Default ownership and success boundary

A normal durable submission uses:

```text
ownership = DURABLE
required_evidence = REMOTE_STORED
```

`submit` success means the local runtime accepted the declared ownership contract. It never means that a link transmitted the packet or that the remote side succeeded.

A durable submission becomes `SATISFIED` when the target runtime has authenticated the logical message when security is enabled, deduplicated it, committed it to its authoritative inbox, and returned a durable `REMOTE_STORED` receipt.

`LINK_SENT` and `REMOTE_RECEIVED` are diagnostics. They are not default success conditions.

Volatile submissions may be supported for expendable diagnostics, but they are never an implicit downgrade path when durable admission fails.

## Decision 2 — Application handoff is at-least-once

The runtime-to-Application boundary is at-least-once.

```text
REMOTE_STORED
→ offer message to Application
→ Application durably adopts or idempotently commits its work
→ Application explicitly acknowledges acceptance
```

The runtime must not persist `handoff complete` merely because it called or returned data to the Application. Until explicit Application acceptance is committed, a restart may offer the same logical message again.

A per-boot `offered` marker may suppress tight-loop redelivery during one process lifetime, but it is not authoritative persistent state.

Applications that cause external or physical effects must use the stable message ID as an idempotency key, or atomically commit their own operation record and acceptance evidence.

Ninlil does not claim exactly-once physical effects.

## Decision 3 — Business results are new messages

A command result, alarm result, actuator failure, or measured outcome is a new Application message.

Example:

```text
message A: pump_open request
message B: pump_open_failed result
```

Ninlil transports both without interpreting their payloads.

For P0, correlation remains Application-owned and is encoded inside the opaque payload. A generic correlation field is not added to the base wire header yet because the direct-radio MTU is small and cross-product demand has not been demonstrated.

Optional `APPLICATION_ACCEPTED` evidence may report that the remote Application durably adopted the message. It must not be interpreted as successful business execution.

## Decision 4 — No cross-message ordering guarantee

Ninlil guarantees stable identity and deduplication for each logical message. It does not guarantee global, peer-wide, or service-wide ordering between different logical messages.

The runtime may prefer local admission order within the same traffic class, but retry, route change, restart, Relay, and multi-Gateway reception may reorder distinct messages.

Applications that publish replaceable state must carry their own monotonic generation, revision, or domain sequence in the opaque payload and reject stale updates.

A duplicate of one logical message and an older distinct logical message are different cases and must not be conflated.

## Decision 5 — Deadline and terminal outcome semantics

Deadline support requires an explicit platform time source and time quality. The P0 enforceable profile permits a nonzero absolute deadline only with required evidence `REMOTE_STORED`. Combining a deadline with `APPLICATION_ACCEPTED` is invalid at submission, DATA decode, and durable replay because P0 has not defined whether post-storage Application delay means expiry, cancellation, or an Application-owned result. That semantic is explicitly deferred.

A first inbound admission and each outbound attempt or retry of deadline-bearing DATA require a successful `RESTART_SAFE` reading. Missing, failed, unavailable, or runtime-only time cannot prove validity or expiry, so that deadline-bearing work is skipped without mutating the inbox or transmitting DATA; unrelated eligible work continues. Duplicate DATA that exactly matches a contract already durably accepted is not a new admission: it may trigger one bounded retransmission of the stored receipt without a clock and after the deadline. This exception preserves recovery from receipt loss and never creates a second inbox record. The runtime never guesses elapsed wall time after a power loss.

A locally proven-expired message becomes terminal `EXPIRED` only if no transmission attempt was durably recorded. If an attempt may have crossed the remote boundary, expiry suppresses further DATA retries but the message remains `ACTIVE` and ambiguous until authoritative evidence or an explicit recovery decision arrives. A peer `EXPIRED` receipt is committed only for an attempted message with a nonzero deadline, unsatisfied required evidence, and local restart-safe time proving `now >= deadline`. Live receipts that fail those tests are ignored; durable histories containing zero-deadline expiry or expiry after satisfying evidence are corruption.

Terminal meanings are:

- `EXPIRED`: the runtime can prove that the declared deadline passed before any local attempt, or can validate a peer expiry after an attempt under the conservative rules above.
- `FAILED`: an authenticated peer or local policy returned an explicit permanent rejection, or the operation was proven impossible without ambiguity.
- `UNKNOWN`: transmission or remote action may have occurred, but the required evidence cannot be determined safely.
- `CANCELLED`: cancellation was durably committed before the operation could have reached the remote responsibility boundary, or a later cancellation protocol explicitly confirmed it.

`ACK missing` is not proof of failure. A deadline-bearing inbound message admitted before expiry has already reached the P0 `REMOTE_STORED` boundary; P0 does not later discard it before Application handoff.

`SUPERSEDED` is not a portable core outcome in P0. Products represent replacement through their own generation/revision semantics and may cancel an older message when cancellation is still safe.

## Decision 6 — Traffic classes, admission reserve, and local scheduling

The portable contract defines four bounded traffic classes:

```text
CRITICAL
CONTROL
NORMAL
BULK
```

They order messages already owned by one local runtime. They do not create a Parent permission-to-send protocol and do not let a Parent know about an event that a remote node has not transmitted.

Scheduling is non-preemptive at the packet boundary: a frame already handed to the radio is never interrupted for a higher class.

Each role profile must declare finite capacities and reserved admission for CRITICAL and CONTROL traffic. CRITICAL admission preserves the configured CONTROL reserve, just as CONTROL and lower classes preserve capacity reserved for higher classes. Once durable ownership is accepted, a lower class message is never silently evicted.

The default scheduler is starvation-bounded weighted service, with CRITICAL selected first when pending but with configured service opportunities for CONTROL and NORMAL. BULK receives service only within an explicit bounded share and must not delay control traffic.

Traffic class never bypasses:

- queue capacity;
- peer or service quotas;
- radio state;
- airtime and regulatory policy;
- authentication and authorization;
- deadline checks.

Exact queue counts and weights are role-profile decisions and will be fixed in the next decision batch after RAM, Flash, and workload budgets are calculated.

## Required P0 implementation consequences

- Replace the current APPLIED-only sender satisfaction rule with per-message required evidence.
- Generate `REMOTE_STORED` receipt immediately after authoritative inbox commit.
- Rename or separate current `APPLIED` APIs so they cannot be read as transport success.
- Persist ownership, traffic class, and required evidence in versioned journal records.
- Version the DATA and receipt wire contracts.
- Require restart-safe clock evidence at each deadline-bearing first admission and DATA attempt, while allowing stored duplicate receipt recovery without that clock.
- Keep Application handoff acknowledgement separate from Application business results.
- Add tests for duplicate receipt, restart redelivery, stale-state ordering, capacity reserve, starvation bounds, and ambiguous timeout outcomes.

## Explicitly deferred

- exact per-role queue sizes;
- Host-to-Gateway custody stages;
- Battery Leaf downlink windows;
- multi-Gateway selection;
- group or broadcast delivery;
- CAD/LBT, channel access, slotting, and airtime scheduler algorithms;
- EDHOC, Join, Relay, and fragmentation.
