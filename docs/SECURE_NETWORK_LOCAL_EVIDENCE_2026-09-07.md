# Steps 4-6 local implementation evidence

Verified: **2026-09-07T23:47:32.393954+09:00**.
The requested software implementation is complete for the
[documented profile](SECURE_NETWORK_PROFILE_2026-09-07.md): secure many-peer
membership, powered Relay recovery/removal, and adaptive communication.
Conversation step 6 is not roadmap M6 bulk/OTA. Physical acceptance is separate.

## Verified results

| Gate | Result |
|---|---|
| GCC 13.3 and Clang 18.1.3 | 27/27 CTest entries each |
| GCC and Clang with ASan + UBSan | 27/27 each; dependencies instrumented too |
| Full local matrix | **108/108 PASS** |
| Pinned upstream suite with final CBOR/helper adaptations | **711 tests, 0 failures, 0 ignored** |
| Manifest parser fuzz | 10,000 seeded executions, ASan/UBSan PASS |
| Join/plan/Relay/fragment parser fuzz | 10,000 seeded executions, ASan/UBSan PASS |
| Static checks | GCC/Clang analysis, crypto-bridge analysis from compile database, ESP strict syntax, format and shell syntax PASS |
| Reproducibility/provenance | Simulation repeatability, pinned revisions and exact patch ledger PASS |
| ESP32-S3 | ESP-IDF v6.0.2 configure/build/link PASS; binary checksum and validation hash valid |
| New physical tests / hosted CI | **Not run** |

The integrated examples use four distinct generated lab credentials, real
EDHOC, encrypted Join, epoch-bound E2E/hop sessions, actual POSIX Core/control
journals, Relay and airtime scheduling. Radio and counter NOR storage are
explicit models, not production POSIX security storage or physical RF.

The same 18-message workload takes **412 ticks / 120 attempts** with a fixed
path and **60 ticks / 97 attempts** with adaptive routing. A tick is 10 ms of
model time, not measured hardware latency. Additional checks cover relay
absence, waiting out an unreleased lease, fresh-session delivery under the
original message ID, drain, epoch-bound route retirement, revoke and disabling
optimization while retaining the effective route.

The periodic optimizer handles zero-delivery observations without division by
zero. A proposed route remains inactive until participant acknowledgments;
restored EFFECTIVE history needs fresh reconciliation. Old keys cannot acquire
new membership epochs, and stale staged data is blocked before physical TX.

## Source, configuration and firmware

[Structured results](SECURE_NETWORK_RESULTS_2026-09-07.json) and the
[239-file source manifest](SECURE_NETWORK_SOURCE_2026-09-07.json) bind these
results to the implementation. The base commit is `355e4ab`; the commit
containing these files captures the completed source. Source-set SHA-256
(UTF-8 text, CRLF normalized to LF):

`ab38891d6c121b634effce7f7a3b1fd606a64c4b5c8efb3a7b0f69c4a4fd771f`

Image: `.verify-m1-evidence/456-final-firmware.bin`, **390,288 bytes**,
SHA-256:

`642eb91014406c90fde15954aa2b3a6b0fe61c9022ef63d760a2203b48498a99`

Application version is `ninlil-456-20260907`, node 1 / peer 2, diagnostic boot,
**TX disabled**, RF region empty and frequency **0 Hz**. EDHOC, secure ingress,
radio pump and periodic Coordinator entry points are explicitly linked and
checked in the ELF. This is component/pump build evidence, not provisioning
credentials or booting a deployed secure network. No connected board was changed.

Wiring is the unchanged Seeed XIAO ESP32-S3 + Wio-SX1262 B2B profile in
`ports/esp32s3/ninlil_board_seeed_b2b.h`. Physical MTU changes from 92 to 240;
non-DMA SPI is split into at most 64-byte chunks under one NSS. HAL tests cover
boundaries, read/write continuity and failure cleanup. New packet sizes have
not been accepted in RF HIL.

Target: ESP-IDF **v6.0.2**, commit `7101770dc6db2667b3c477cc31365dd1acd6db4e`;
Xtensa GCC **15.2.0** (`esp-15.2.0_20251204`), CMake **4.0.3**, Python **3.12.3**,
Mbed TLS **4.1.0** / TF-PSA-Crypto **1.1.0**. Local cached image:
`sha256:c03b6700557906d221aca77af5dd82addbf7102025ee41627a005b1bb1c04f93`,
based on ESP-IDF image
`sha256:e3d941cb983e028aad1e2f5ecb2837254e467f2b71f3e0af67e7337bd27ae177`.

Host: Ubuntu 24.04 on WSL2 x86_64; GCC **13.3.0**, Clang/clang-format **18.1.3**,
CMake **3.28.3**, Ninja **1.11.1**, Python **3.12.3**, Git **2.43.0**.
First-party strict warnings and all sanitizer categories remain enabled.
External headers retain dependency warning scope; they are still instrumented.

## Reproduce and inspect

Fresh local verification uses ordinary repository sources:

```sh
bash scripts/fetch_edhoc.sh
bash scripts/fetch_sx126x_driver.sh  # only when the driver directory is absent
NINLIL_BUILD_ROOT=/tmp/ninlil-verify NINLIL_JOBS=6 bash scripts/ci.sh
```

The final run rebuilt and reran every CTest entry in
`/tmp/ninlil-456-final/{gcc,clang,gcc-sanitize,clang-sanitize}`, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`, then ran the normal remaining gates.
The additional adapted upstream suite used
`bash scripts/verify_vendor.sh /tmp/ninlil-456-adapted-vendor`.

Target command, **without executing a flash command**:

```sh
idf.py -C /work/embedded/esp32s3 -B /tmp/ninlil-456-build \
  -D SDKCONFIG=/tmp/ninlil-456-sdkconfig \
  -D PROJECT_VER=ninlil-456-20260907 build
```

Final logs under `.verify-m1-evidence/` are `456-release-local-verification.log`,
`456-adapted-vendor-final.log`, `456-release-target.log` and
`456-final-image-info.log`. The image, ELF, sdkconfig, generated EDHOC config and
compile commands are retained. The **89-file** local artifact manifest is
`456-evidence-manifest.json`, SHA-256:

`27a62e21a16fc12c76c8d43df6ea7f2a4a3e16f83fdfed44bdde98ccb9de2b10`

Raw artifacts remain local and ignored by Git, not uploaded to GitHub.
Implementation, tests, build logic, profile and structured/source evidence are
normal repository files, not generated archives or hosted-CI-only source staging.

Earlier failed logs are preserved. Verification fixed empty-message allocation,
zero-length `memmove`, removed Mbed TLS PK APIs, unaligned PSA IDs, incompatible
CBOR callback casts, and the helper's small-buffer error-code compatibility.
A test fixture's 16-entry capacity for an 8-entry array was also corrected.
Review tightened Join idempotency/replay, immutable session epochs, stale queued
frames, fractional airtime credit and expected-epoch route retirement.

M1's historical glob accidentally counted all new M2-M5 modules. M1 remains
capped at 7,000 lines, a separate explicit ledger caps the new profile at 12,000,
and the project stays at 50,000. Project accounting now also includes
Python/CMake/JSON/YAML. Numerical existing ceilings were not increased.

## Unrun and bounded behavior

The user's decision ends new hardware tests. Secure/multi-hop RF, 240-byte
physical packets, controlled Flash-write power cuts, field deployment,
production-security acceptance and hardware secure boot remain unaccepted.
No hosted CI, push, issue/project write or external message occurred. Earlier
plaintext two-board tests remain their own evidence.

Control storage is bounded and append-only: capacity backpressures rather than
evicting ownership. Infinite churn/retention, automatic PHY retuning, bulk/OTA,
and a product-specific credential issuer/UI are outside this profile. The Host
supplies authoritative trust/grants, restart-safe time, and genuine participant
application/source-recovery facts through typed boundaries.

Vendor patch context and two upstream-generated trailing blank lines are preserved
with narrowly scoped Git whitespace attributes. First-party formatting, strict
compiler diagnostics, all sanitizers and exact vendor hash/patch checks remain
active; the whitespace attributes do not change compiled source.
