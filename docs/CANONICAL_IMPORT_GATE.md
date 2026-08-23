# Canonical import acceptance gate

The first source import into `MOVEI144/Ninlil` is accepted into `main` only when all of the following hold for the exact pull-request head:

- the imported source archive provenance is recorded;
- temporary bootstrap payloads and one-shot import workflows are absent from the review tree;
- `git diff --check` succeeds;
- GCC strict tests succeed;
- Clang strict tests succeed;
- GCC AddressSanitizer and UndefinedBehaviorSanitizer tests succeed;
- Clang AddressSanitizer and UndefinedBehaviorSanitizer tests succeed;
- static-analysis gates succeed;
- canonical `clang-format` verification succeeds;
- the real ESP-IDF v6.0.2 / ESP32-S3 configure-and-link job succeeds;
- the pull request still points to the exact commit tested by CI.

The merge gate must not reinterpret a failed, missing, cancelled, skipped, or stale job as success.

This software gate does not replace physical acceptance. Two-board RF, regional RF configuration, GPIO38 polarity, hard-power interruption, and long-duration HIL remain `NOT RUN` until separate evidence is recorded.