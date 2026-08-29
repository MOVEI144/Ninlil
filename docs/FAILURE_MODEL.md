# Failure model

Ninlil assumes the following can happen at any boundary:

- process crash or watchdog reset;
- power removal during erase or program;
- partial, duplicated, delayed, reordered, or lost packets;
- link success followed by remote failure;
- stale sessions, bindings, or routes;
- storage full, I/O failure, or committed-state corruption;
- queue saturation and task cancellation;
- untrusted and malformed input.

## Required reactions

- Ambiguous durable state is never guessed.
- A completed operation whose result was not observed becomes `UNKNOWN`, not `FAILED` or `SUCCEEDED` by assumption.
- Completed-but-reported-failed storage writes are resolved by replaying authoritative state.
- Cryptographic counter rollback or possible nonce reuse requires a fresh session.
- Committed corruption is a hard error. Every journal-reference dereference revalidates the complete committed POSIX or raw-Flash record before exposing payload bytes; a failed validation prevents Link send or Application handoff, and committed corruption also fails reopen.
- Resource exhaustion returns an explicit capacity error and preserves already-owned durable data.
- A missing receipt leaves the operation active; after a possible remote effect it may become `UNKNOWN` only through an explicit recovery decision. Deadline expiry cannot turn an attempted operation into inferred failure, and unavailable restart-safe time cannot authorize deadline-bearing first admission or DATA transmission. A matching duplicate of an already durable inbox record may still re-arm one bounded stored receipt without time evidence, because it creates no new ownership decision.
- An unauthorized message may produce only a generic bounded rejection and never mutates the authoritative inbox. A permanent rejection is sent only after its exact message contract is durably tombstoned; tombstone capacity or storage failure backpressures, and conflicting identity reuse fails closed.
- An ambiguous Host custody or route commit poisons the in-memory view; recovery replays authoritative storage rather than guessing.
