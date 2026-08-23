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
- Committed corruption is a hard error.
- Resource exhaustion returns an explicit capacity error and preserves already-owned durable data.
