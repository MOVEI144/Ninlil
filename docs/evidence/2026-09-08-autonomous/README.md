# Historical autonomous captures

The nine raw console captures are retained byte-for-byte in the Git revision
and blob paths listed in INDEX.json. Before removing the duplicate working-tree
copies, every byte and SHA-256 was verified against that immutable destination.
Pass/fail summaries, image identities and hashes remain in
`docs/AUTONOMOUS_HIL_RESULTS_2026-09-08.json`; their `console` paths identify the
historical paths in that revision, rather than current checkout files.

Verify or retrieve a capture with:

```
python tools/node_hil/capture.py 26
python tools/node_hil/capture.py 26 --output /chosen/new/run26.jsonl
```

The output must be new. Keep Git history available when distributing this
evidence; a shallow source archive alone does not contain the raw captures.
Test sources remain ordinary current repository files.
