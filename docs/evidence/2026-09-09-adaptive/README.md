# Adaptation component evidence — 2026-09-09

Base: `8c37df522b8cc4ccbd58010fb11f909d0049f9ed` (PR #17).

This is a **partial integration** checkpoint. Six actual C implementation units
are compiled with the verified original `include/ninlil.h`, not a rewritten
protocol model. Only the optional DRR scheduler is connected to the existing
firmware open path; target compilation and physical operation remain unrun.
The route/PHY/fanout components do not yet replace the autonomous runtime.

## Executed

`python scripts/run_adaptive.py /mnt/data/ninlil-validation-2 --resume --jobs 4`
produced `attempt-004/results.json`: GCC 14.2.0 / Clang 17.0.0, normal and
ASan/UBSan configurations, each 10/10 CTest entries. Leak detection and halt on
sanitizer error were enabled. One entry contains 11 Python 3.13.5 USB protocol
fixture tests. The exact source hashes were checked before and after the matrix.
`results.json` is the condensed report; `source_hashes.json` identifies its inputs.
The delivered change bundle includes the original JSON, command logs and JUnit
reports in `evidence/full-component-run/`, plus scoped analyzer records in
`evidence/static-analysis/`. Their absolute build paths identify this workspace,
not the user's device PC. Earlier interrupted/mid-development runs are not
relabeled as final successes.

GCC `-fanalyzer -c` and Clang `--analyze` were subsequently run on the six C
translation units: 12 commands, all exit 0, no emitted diagnostics. This is not
the full repository static-analysis gate or an independent security audit.

Observed native witnesses: airtime shares 64/32/24/8 million microseconds; two
peers each receive 4 million microseconds; bounded route search on 7/512-node
graphs; 511-target typed adapter fixture with the first 32 blocked **before Core
admission**; interrupted intent/admission/terminal persistence; 20,000 seeded
codec mutations; fixed reassembly deadlines and conflicting recovery slots.
The fake digest callback validates the fixture, not cryptographic strength.
The fanout fixture is not the real identity-bound Core integration.

## Not run / not claimed

Full SDK regression, full source/format/LOC/package gates, production protocol
fuzz, ESP-IDF configure/compile/link, physical seven-board HIL, power-cut, current,
range, long-term and field qualification remain NOT_RUN. No board was started,
flashed, erased or re-enrolled by this task. GitHub Actions are intentionally
skipped; this is not CI-green. No 512-node autonomous RF capability is claimed.

See [implementation](../../ADAPTIVE_IMPLEMENTATION_2026-09-09.md) and
[seven-board procedure](../../SEVEN_BOARD_TEST.md).
