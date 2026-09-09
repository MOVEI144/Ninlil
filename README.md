# Ninlil

Ninlil is a small, portable C11 runtime for durable messaging over intermittent, low-bandwidth links. The standard hardware target is XIAO ESP32-S3 with Wio-SX1262 over the B2B connector; other ports remain possible through explicit adapters.

This repository, `MOVEI144/Ninlil`, is the canonical project repository from 2026-08-23 onward. The earlier `Aero123421/Ninlil-Runtime` repository is a legacy design and evidence source, not the implementation authority for this codebase.

## Current verified state

| Area | State |
|---|---|
| P0 delivery evidence, bounded profiles, and Host operational contracts | Local software gates pass; full acceptance remains open |
| POSIX durable delivery core | Local restart and fault-injection tests pass |
| SX1262 direct-radio software | Bench RF verified; field qualification remains open |
| ESP32 raw-flash delivery journal | Bench persistence/reset verified; controlled power cuts remain open |
| Persistent security counter and membership stores | Local corruption/restart tests pass; physical power cuts remain open |
| EDHOC, Join, Relay and autonomous node owner | Implemented; three-board delivery, restart and drain evidence recorded |
| Storage collection, bulk transfer and TX power adaptation | Implemented as a bounded extension; see its verification scope |

No production release has been declared. The [storage/bulk/radio extension](docs/STORAGE_BULK_RADIO_2026-09-08.md) and [earlier small-message integration](docs/OSS_COMPLETION_2026-09-08.md) distinguish locally verified software, exact firmware-specific bench results and unrun gates.

## Design boundary

Ninlil owns communication mechanics:

- durable submit, retry, deduplication, receipts, and restart recovery;
- bounded radio and storage adapters;
- authenticated sessions, committed membership, encrypted relaying and route recovery.

Ninlil does not own product policy, cloud APIs, dashboards, building or equipment models, safety-rule decisions, or tenant authorization. Product integrations such as KG consume Ninlil at an explicit application-level delivery boundary.

The authoritative responsibility boundary and delivery semantics are defined in [`docs/FOUNDATIONS.md`](docs/FOUNDATIONS.md). The implemented P0 API, format versions, and non-claims are summarized in [`docs/P0_IMPLEMENTATION.md`](docs/P0_IMPLEMENTATION.md).

Physical ESP32-S3/SX1262 work follows
[`docs/M1_HIL_ACCEPTANCE.md`](docs/M1_HIL_ACCEPTANCE.md). Until its required
phases have reviewed evidence, firmware builds and bench observations remain
candidate evidence only.

## Build and test

Local verification requires CMake, Ninja, GCC, Clang, Python and clang-format 18.

```sh
bash scripts/fetch_edhoc.sh
bash scripts/ci.sh
```

The ESP32-S3 build requires ESP-IDF v6.0.2 and the exact pinned Semtech driver subset:

```sh
./scripts/fetch_sx126x_driver.sh
. /path/to/esp-idf-v6.0.2/export.sh
./scripts/build_esp32s3.sh
```

Neither command flashes hardware or enables RF transmission. Repository defaults keep TX disabled until an explicit, reviewed RF profile is supplied.

## Documentation

Storage collection and the optional 64 KiB bulk profile are documented in
[`STORAGE_BULK_RADIO_2026-09-08.md`](docs/STORAGE_BULK_RADIO_2026-09-08.md).
Installed consumers can require the `bulk` CMake component and link `Ninlil::bulk`.
The profile retains incomplete objects and distinguishes local complete storage,
remote adoption and the caller's application effects. OTA installation is excluded.

Read in this order:

1. [`docs/STATUS.md`](docs/STATUS.md)
2. [`docs/FOUNDATIONS.md`](docs/FOUNDATIONS.md)
3. [`docs/P0_DELIVERY_CONTRACT_V2.md`](docs/P0_DELIVERY_CONTRACT_V2.md)
4. [`docs/P0_OPERATIONAL_PROFILES_V1.md`](docs/P0_OPERATIONAL_PROFILES_V1.md)
5. [`docs/P0_IMPLEMENTATION.md`](docs/P0_IMPLEMENTATION.md)
6. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
7. [`docs/ENGINEERING_STANDARD.md`](docs/ENGINEERING_STANDARD.md)
8. [`docs/CODING_STYLE.md`](docs/CODING_STYLE.md)
9. [`docs/FAILURE_MODEL.md`](docs/FAILURE_MODEL.md)
10. [`docs/TESTING.md`](docs/TESTING.md)
11. [`docs/M1_HIL_ACCEPTANCE.md`](docs/M1_HIL_ACCEPTANCE.md)
12. [`docs/ROADMAP.md`](docs/ROADMAP.md)
13. [`docs/ADAPTIVE_NETWORK_CONTRACT.md`](docs/ADAPTIVE_NETWORK_CONTRACT.md)
14. [`docs/SIMULATION.md`](docs/SIMULATION.md)

## License

Apache License 2.0. See [`LICENSE`](LICENSE).

## Secure many-peer and adaptive Relay profile

Conversation steps 4-6 are implemented in the [autonomous profile](docs/OSS_COMPLETION_2026-09-08.md).
The new `ninlil_node` owner runs authentication, participation, observation,
route application, recovery and removal from explicit receive/step calls.
See the [integration checkpoint](docs/OSS_COMPLETION_2026-09-08.md) for the
current verification boundary. The older USB-owned examples and their HIL
results remain dated evidence; their recoverable sources are listed in the
[historical index](docs/HISTORICAL_CONTROLLERS.md).

For a Core-only installation without vendor crypto, configure with
`-DNINLIL_BUILD_SECURE=OFF`. `-DNINLIL_BUILD_TESTS=OFF` omits validation
executables. `cmake --install build --prefix /chosen/prefix` installs a static
package; consumers use `find_package(Ninlil 0.1 CONFIG REQUIRED COMPONENTS core)`
and `Ninlil::posix`, or request `secure` and link `Ninlil::node`.
The installed package defaults to the POSIX journal; set
`Ninlil_JOURNAL_BACKEND=flash_runtime` before finding it to select the NOR file model.
The host crypto snapshot is a pinned reference fixture; the ESP port uses SDK PSA.
