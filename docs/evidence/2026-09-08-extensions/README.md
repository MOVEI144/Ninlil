# Extension hardware evidence

INDEX.json records byte-exact console captures and independently reconciled
per-run summaries in the `codex/storage-bulk-radio-evidence` Git history.
That history is joined to the implementation with a content-preserving merge,
so captures remain reachable when cloning the implementation's full history.
The active checkout carries the index, firmware input hashes and concise results;
test and implementation sources remain normal files. The 50,000-line active
project ceiling is unchanged. The separate evidence tree contains only captures
and summaries, also below that ceiling; no original local capture was deleted.

Every committed blob was read back and checked against its exact size/SHA-256.
Retrieve to a new file (never overwrites):

```
python tools/node_hil/capture.py 5 --extension --output /chosen/new/run5.jsonl
python tools/node_hil/capture.py 5 --extension --result --output /chosen/new/run5.json
python tools/node_hil/extension_evidence.py /chosen/new/run5.jsonl
```

Run 1 exposed cold-session handling and failed. Runs 2–4 timed out with owned
progress retained; run 2 includes actual receiver restart retaining 80 bytes.
Run 5 completed the same 512-byte object, verified full readback/SHA-256 and
sender completion, then collected both object journals and stopped all nodes.
Power remained at -3 dBm in measured RF conditions; an autonomous downward
power change was not observed. These are not throughput or field qualifications.
