# M1 two-board bench procedures

These are deliberately pinned September 7, 2026 bench procedures, not product
commands or a general flashing utility. They require the existing verified
artifacts and original 8 MiB board backups in `.verify-m1-evidence`, esptool
5.3.0, the documented COM identities and the local verification container.
Never substitute an unverified device or binary. Outputs must use new labels.

Order: back up and clean only the known test journal; run local gates; build
and verify the pinned firmware; flash both roles (leaves ROM loader); capture
both fresh boots; read each actual journal and app digest; analyze logs and
journal records together; finally restore and verify TX-disabled images.
`fault-journal.py` saves and verifies the entire journal before `clean` can
remove it. The separate `m1_corrupt_journal_hil.py` restores its saved sector
in `finally`; on any restoration error, retain the backup and stop the bench.

See `docs/M1_FAULT_CAMPAIGN_2026-09-07.md` for classifications and limitations.
The cache build script runs inside a pinned local ESP-IDF container whose
existing `/project` checkout and pinned driver were previously verified.
It fetches a committed source from the read-only `/source` mount and writes
configuration, provenance and hashed binaries under `/evidence`.

Example (after build, backup/clean and flash gates):

```
python tools/m1_bench/capture-fault.py fault-a-init fault-b-resp new-label
python tools/m1_bench/fault-journal.py a new-label read fault-a-init
python tools/m1_bench/fault-journal.py b new-label read fault-b-resp
python tools/m1_bench/analyze-fault.py new-label new
```

Use a lowercase label. Capture stops both MCUs in download mode even on error.
A run summary alone is insufficient; require the independent analyzer PASS.

The pending monitor restart test uses start-pending-monitor.py with B in ROM,
then capture-monitor-restart.py fault-a-init fault-b-resp fault-monitor-restart.
The first process must exit successfully before invoking the second. The
second opens A without reset; it preserves the pre-monitor log verbatim as a
prefix. Any TX-only logs in the gap are unobserved, not assumed absent.
