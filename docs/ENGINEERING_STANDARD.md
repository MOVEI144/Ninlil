# Engineering standard

Priority order:

1. Preserve durable state and cryptographic safety.
2. Never turn an unknown result into success.
3. Recover correctly after restart or power loss.
4. Keep behavior deterministic and explainable.
5. Keep resource use finite.
6. Keep the code maintainable and small.
7. Optimize performance only after the above.

## System rules

- Commit before effect.
- Use durable outbox/custody patterns for recoverable external effects.
- Reject malformed or unsupported input before mutating runtime state.
- A failed decode must not partially update state.
- A queue-full condition is an explicit result, not a silent drop.
- A timeout always has a finite bound and a documented recovery path.
- A restart is a normal tested transition.
- Unknown reserved protocol bits are rejected unless the version contract says otherwise.

## Size budget

First-party implementation, tests, documentation, and build logic have a project hard budget of 50,000 nonblank lines. Third-party code and generated artifacts are counted separately. Approaching the limit triggers deletion and simplification, not automatic expansion of the budget.
