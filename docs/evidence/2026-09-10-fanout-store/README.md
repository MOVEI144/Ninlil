# Durable fanout store evidence — 2026-09-10

Base b38878e; unpushed cumulative integration patch. `results.json` is the scoped
summary; `source_provenance.json` records exact upstream Git blobs. No file copies
or declaration doubles replace the public header or POSIX journal used in these
new storage tests. The downstream Core adapter is a named, separately journaled
fixture, not the production Core or physical recipient.

Final run: `scripts/run_adaptive.py OUTPUT --resume --jobs 4`, attempt-002.
GCC14.2/Clang17, normal + ASan/UBSan: 25/25 CTest each, zero skipped. One entry
contains 17 Python seven-board protocol tests. All 89 source inputs were hashed
before/after and checked again before packaging. The first 23 entries include
inherited node/crypto/USB/radio boundary doubles; none proves seven-board RF.

New tests use real file writes, fsync, readback, locks and open/replay. START is
interrupted after 10 record commits by child `_exit`; target transitions after
3 more. Every one of 1,191 prefixes of the seven-target START file is tested.
A 511-target journal recovers 32 initially unavailable targets. Corruption of
header/target/body/seal/state is rejected before exposure or downstream calls.
Payload uses actual OpenSSL SHA-256, not a dummy digest. Codec mutations use the
recorded deterministic seed. This is not a full protocol fuzzer or electrical
power-loss experiment.

Five actual C translation units (including the unchanged POSIX journal) pass
GCC/Clang static analysis: 10 commands, zero final diagnostics. Earlier logs
preserve a regression which failed before the read-error classification fix
and the initial Clang output-pointer diagnostic. Neither was suppressed.

The delivered bundle's `evidence/` contains original matrix/JUnit, source hashes,
static commands/logs, witnesses and historical failures. The verification scope
and remaining Core/Flash/ESP-IDF/package/full-checkout/RF gates are explicit in
[the implementation record](../../DURABLE_FANOUT_2026-09-10.md).
