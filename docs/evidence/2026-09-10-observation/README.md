# Observation integration evidence — 2026-09-10 JST

Base: `4357286cf3cff58e9b3413eef45412dc28c4771e` (PR #18).
This is an actual node/pump **boundary integration** checkpoint, not full SDK,
ESP-IDF, cryptographic, or physical RF qualification.

## Executed

`python scripts/run_adaptive.py /mnt/data/ninlil-observation-validation --resume --jobs 4`
created `attempt-005/results.json`. GCC 14.2.0 and Clang 17.0.0, each normal
and ASan/UBSan, pass **14/14 CTest entries**. One entry executes eleven Python
3.13.5 seven-board-runner tests. Leak detection and halt-on-sanitizer-error
were enabled. All 52 recorded source inputs match before/after the matrix
and were checked again before packaging. Earlier attempts remain separate.

The production scheduler, metrics and new monitor are compiled directly.
`ninlil_node_radio.c`, `ninlil_node_links.c`, `ninlil_node_sleep.c` and the
ESP32 network pump are copied byte-for-byte into the build for explicit
fixture headers. Crypto, driver, Core, node stepping and Coordinator boundaries
are doubles. The test verifies the real node observation encoder/receiver path
up to a Coordinator input sink; it does **not** execute the actual Coordinator
route selector, a real EDHOC session, or complete SDK ABI.

The deterministic integration witness reports:

```text
production pump -> TX_DONE -> authenticated-reply boundary -> node report -> Coordinator input PASS; TX=16 queue_us=500000
```

Here TX=16 counts fake driver calls: eight BUSY returns and eight successful
completions, not sixteen over-the-air transmissions. Duplicate enqueue retains
its first timestamp. Six of eight completed trials receive matching replies;
the report stays unavailable before the final response window closes. The
queue field is 500,000 us and the sampled output is the driver's applied -6 dBm,
not its requested -3 dBm. These are synthetic fixture values, not RF measurements.
The same sources pass with explicit monitor attachment and the opt-in compiled
workspace. Tests also cover delayed ACK versus next probe, overlapping closed
routing windows, power/session/sleep invalidation, bounds and artifact/config
mismatch. Independent power-policy windows remain separate and legacy power
control has not been replaced.

GCC `-fanalyzer -c` and Clang `--analyze` were run on seven C translation units
(three direct modules and four exact production units with fixture dependency
headers): fourteen commands, exit zero and no emitted diagnostics. This is a
scoped analyzer run, not the repository-wide gate. `git diff --check` passes
against the exact available original-source subset. No warning or test was
disabled to obtain these results.

## Evidence and non-claims

`results.json` is the compact report; `source_hashes.json` identifies all 52
matrix inputs. The delivered change/evidence bundle retains the original
command logs, JUnit files and analyzer command report. Absolute paths name this
container, not the user's device PC.

Full checkout/SDK regression, ESP-IDF, installed-package, clang-format, full LOC,
full protocol fuzz, seven-board RF/HIL, hard power cuts, current/lifetime,
range/soak/field gates are NOT_RUN. The unchanged 50,000-line project ceiling
has not been checked against a complete checkout. No device was flashed,
erased, enrolled or started. Commits retain `[skip ci]`; hosted CI is not run.
The new route-search engine, coordinated PHY transactions and durable bound
fanout/Core adapter remain independent unfinished integration work.

See [implementation](../../OBSERVATION_INTEGRATION_2026-09-10.md) and
[seven-board procedure](../../SEVEN_BOARD_TEST.md).
