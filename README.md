# Ninlil

Ninlil is a small, portable C11 runtime for durable messaging over intermittent, low-bandwidth links. The standard hardware target is XIAO ESP32-S3 with Wio-SX1262 over the B2B connector; other ports remain possible through explicit adapters.

This repository, `MOVEI144/Ninlil`, is the canonical project repository from 2026-08-23 onward. The earlier `Aero123421/Ninlil-Runtime` repository is a legacy design and evidence source, not the implementation authority for this codebase.

## Current verified state

| Area | State |
|---|---|
| POSIX durable delivery core | Host-tested baseline |
| SX1262 direct-radio software | Software candidate; physical RF acceptance pending |
| ESP32 raw-flash delivery journal | Software candidate; hard-power acceptance pending |
| Persistent security counter and membership stores | Software candidate; physical power-cut acceptance pending |
| Secure-link encryption, EDHOC, Join, Relay, fragmentation | Not part of the imported official baseline |

No production release has been declared. In particular, passing host simulation is not evidence of RF performance or power-loss safety on physical flash.

## Design boundary

Ninlil owns communication mechanics:

- durable submit, retry, deduplication, receipts, and restart recovery;
- bounded radio and storage adapters;
- authenticated session, Join, Relay, and fragmentation layers as later milestones.

Ninlil does not own product policy, cloud APIs, dashboards, building or equipment models, safety-rule decisions, or tenant authorization. Product integrations such as KG consume Ninlil through a narrow transport adapter.

The authoritative responsibility boundary and delivery semantics are defined in [`docs/FOUNDATIONS.md`](docs/FOUNDATIONS.md).

## Build and test

Host CI requires CMake, Ninja, GCC, Clang, and clang-format.

```sh
./scripts/ci.sh
```

The ESP32-S3 build requires ESP-IDF v6.0.2 and the exact pinned Semtech driver subset:

```sh
./scripts/fetch_sx126x_driver.sh
. /path/to/esp-idf-v6.0.2/export.sh
./scripts/build_esp32s3.sh
```

Neither command flashes hardware or enables RF transmission. Repository defaults keep TX disabled until an explicit, reviewed RF profile is supplied.

## Documentation

Read in this order:

1. [`docs/STATUS.md`](docs/STATUS.md)
2. [`docs/FOUNDATIONS.md`](docs/FOUNDATIONS.md)
3. [`docs/P0_DELIVERY_CONTRACT_V2.md`](docs/P0_DELIVERY_CONTRACT_V2.md)
4. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
5. [`docs/ENGINEERING_STANDARD.md`](docs/ENGINEERING_STANDARD.md)
6. [`docs/CODING_STYLE.md`](docs/CODING_STYLE.md)
7. [`docs/FAILURE_MODEL.md`](docs/FAILURE_MODEL.md)
8. [`docs/TESTING.md`](docs/TESTING.md)
9. [`docs/ROADMAP.md`](docs/ROADMAP.md)

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
