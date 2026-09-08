# Source budgets for the autonomous-owner addition

The September 8 request adds the previously absent autonomous MCU execution
owner, persistent identity integration and an application/HIL entry point.
The project-wide hard ceiling remains **50,000 nonblank first-party lines**,
including tests, documentation, tooling and historical evidence in the checkout.

The old secure-profile ledger initially counted both its protocol libraries
and the new execution owner against 12,000 lines. That combined gate failed:
15,927 lines before retirement, 12,993 afterward. These failures remain in the
local evidence; they are not relabeled as passes.

Twenty-five superseded controller/fragment files were removed only after
verifying their recoverable Git sources and hashes. See
[the historical index](HISTORICAL_CONTROLLERS.md). The general secure multiplexer,
current regression tests and immutable review witnesses remain.

The scoped accounting now distinguishes the two actual build responsibilities:

| Execution responsibility | Ceiling | Ledger |
|---|---:|---|
| Secure protocol/storage libraries and their tests | 12,000 | Secure union minus autonomous ledger |
| Autonomous owner, lease clock, reference MCU application and their tests/tools | 5,500 | `scripts/autonomous_node_files.txt` |

This is an additional scoped allocation for the new owner, **not a claim that
the combined source fits the former 12,000-line scope**. It does not increase
the project hard ceiling or the existing P0, M1 or security-state ceilings.
`scripts/secure_network_files.txt` remains the complete union used by the M1
scope checker. The size gate rejects unknown owner entries and duplicate paths;
all listed files are charged once to one of these two responsibilities.

The September 7 specification remains dated historical context and is counted
in historical evidence and the project total. Current behavior and release
limitations belong to [the integration record](OSS_COMPLETION_2026-09-08.md).
