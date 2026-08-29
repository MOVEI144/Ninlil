# P0 batch 1 implementation notes

The authoritative design decisions are in [`P0_DELIVERY_CONTRACT_V2.md`](P0_DELIVERY_CONTRACT_V2.md). This note maps the six decisions to implementation work without changing milestone scope.

## Implementation order

1. Version the submission, journal, DATA, and receipt contracts.
2. Add ownership, required evidence, and traffic class to submission state.
3. Commit inbound DATA before generating `REMOTE_STORED` receipts.
4. Replace APPLIED-only sender satisfaction with evidence comparison.
5. Implement explicit Application acceptance while preserving at-least-once handoff.
6. Add clock/time-quality interfaces and terminal outcome tests.
7. Add bounded class queues and starvation-bounded local scheduling.

## Compatibility policy

The project has not declared a production release. P0 v2 may reject the old wire and journal format explicitly instead of carrying a permanent compatibility layer. The migration decision must be visible in version fields and documentation; silent reinterpretation is forbidden.

## Review focus

- no success inference from link-send return codes;
- no durable handoff marker before explicit Application acceptance;
- no message loss on queue pressure after ownership;
- no accidental global ordering promise;
- no `FAILED` result derived only from missing receipts;
- no traffic class bypass of capacity or radio policy;
- no KGuard-specific vocabulary in the portable API or wire format.
