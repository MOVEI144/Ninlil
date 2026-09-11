# Dependency notices

Ninlil's host secure package includes the following pinned upstream code.
The installation contains each upstream license under `share/ninlil/licenses`.
The original checkouts and the adaptation hash ledger are retained in source.

- libedhoc: Copyright (c) 2024, Kamil Kiełbasa and others. MIT License.
- zcbor: Apache License 2.0; see its upstream license and source notices.
- Mbed TLS: Copyright The Mbed TLS Contributors. Offered under
  Apache-2.0 OR GPL-2.0-or-later; this distribution uses Apache-2.0.
- Mbed TLS's Everest implementation: Copyright (c) INRIA and Microsoft
  Corporation. All rights reserved. Licensed under the Apache 2.0 License.
- Mbed TLS's p256-m implementation: Copyright The Mbed TLS Contributors.
  Author: Manuel Pégourié-Gonnard. SPDX-License-Identifier:
  Apache-2.0 OR GPL-2.0-or-later; this distribution uses Apache-2.0.

The host package does not include the Unity test runner. Firmware builds also
use the separately pinned Semtech SX126x driver and ESP-IDF dependencies;
their source distributions retain their own license and notice files.
Compatibility changes are recorded in `third_party/adapted/PROVENANCE.json`.
