# Canonical baseline import provenance

This document records the provenance of the first reviewed source import into `MOVEI144/Ninlil`.

## Canonical repository

- Repository: `MOVEI144/Ninlil`
- Review branch: `feature/import-m3-security-state`
- Import date: 2026-08-23

## Source identity

The imported source archive was reconstructed and accepted only after its complete byte-level digest matched:

```text
SHA-256: a9b5a8c5647c33e553b22f5aed4742ea07836ec22e023b66f53eb851d57a496b
Size:    62,416 bytes
Members: 107
```

The source candidate was derived from the reviewed temporary Git tree identified by:

```text
M1 parent commit: a67dcb4a0778cd85e557f5168429d464b4b6e2f8
M3.3 candidate:   b10c92dbb267e416c388889eff72aa4adc2ff30c
M3.3 tree:        fa2003c3bbccfa6c790116d0aa65e9fc6470079a
```

These temporary identifiers document provenance only. The canonical history begins in this repository.

## Imported implementation scope

- portable C11 durable-delivery core and POSIX recovery path;
- bounded direct-radio model;
- ESP32-S3 and SX1262 HAL/physical-radio source with fake-hardware tests;
- raw-flash delivery journal;
- independent fail-closed persistent secure-counter store;
- independent fail-closed persistent membership store;
- strict host tests, sanitizer gates, static-analysis gates, and ESP32 syntax checks.

## Explicit non-claims

The import does not establish that the following exist or are accepted:

- integrated production Secure Link encryption;
- EDHOC handshake;
- Join, resume, or revoke wire protocols;
- Relay or routing;
- fragmentation or bulk transfer;
- a product Controller;
- KGuard-specific policy or behavior;
- two-board RF acceptance;
- hard-power interruption acceptance;
- regional radio certification.

The repository must continue to represent these as future or `NOT RUN` work until evidence exists.