# AGENTS.md

This file applies to every coding agent and contributor in the repository.

## Before changing code

Read:

1. `docs/STATUS.md`
2. `docs/ARCHITECTURE.md`
3. `docs/ENGINEERING_STANDARD.md`
4. `docs/CODING_STYLE.md`
5. `docs/FAILURE_MODEL.md`
6. `docs/TESTING.md`
7. the milestone specification relevant to the change

## Non-negotiable rules

- Do not represent an uncertain result as success.
- Do not perform an external effect before the durable state required to recover it has committed.
- Keep every queue, table, packet, retry loop, timeout, and allocation bounded.
- Preserve the distinction between accepted, sent, received, stored, applied, verified, failed, and unknown.
- Do not introduce product-specific concepts into the reusable runtime.
- Do not add speculative factories, managers, providers, plugin systems, or generic frameworks.
- Do not invent cryptography or a proprietary key-exchange protocol.
- Do not weaken compiler warnings, corruption checks, or failure tests to make a change pass.
- Do not claim ESP-IDF, RF, flash power-cut, or HIL success unless that exact gate was run and its evidence is recorded.

## Required completion sequence

1. Implement only the scoped change.
2. Run focused tests.
3. Run the full applicable CI matrix.
4. Review the entire diff for ownership, bounds, durability, duplicate handling, parser safety, restart behavior, and secret exposure.
5. Fix review findings.
6. Re-run the affected tests.
7. Report both completed and unrun gates explicitly.

A test passing is not permission to broaden scope.
