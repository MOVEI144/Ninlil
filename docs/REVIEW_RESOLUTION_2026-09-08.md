# Review resolution and regression map

The immutable review in `reviews/2026-09-08` describes defects reproduced on
`f5b5b7eb84f7ad830cb2bbba2a9a7820820d53d4`. Its witnesses remain unchanged.
The implementation now uses the following normal repository regressions.
Final local and physical results are recorded in
[the integration record](OSS_COMPLETION_2026-09-08.md).

| Finding | Corrected behavior | Regression source |
|---|---|---|
| R1: ambiguous committed Flash marker | Fail closed instead of skipping owned data; both directions of marker-bit corruption are checked | `tests/test_flash.c` |
| R2: temporary service quota interpreted as permanent denial | Capacity remains retryable across consumption/restart | `tests/test_quota_retry.c` |
| R3: abort erased an already committed plan fence | Abort only staged plans; committed withdrawal requires release or expiry | `tests/test_network_restart.c` |
| R4: replay selected another flow's lease for activation | Activation fences the actual replaced flow | `tests/test_network_restart.c` |
| R5: replay reused stale participant proofs | Replay/disconnect clears volatile preparation, application and reconciliation evidence | `tests/test_network_restart.c`, `tests/test_node.c` |
| R6: missing DATA route blocked control progress | Continue receiving/scheduling control while Core is waiting for a route | `tests/test_network_pump.c` |
| R7: queued DATA escaped later validation | Recheck current keys, route, lease, deadline and durable ownership before radio send | `tests/test_network_pump.c` |
| R8: small packets starved larger packets | Preserve the selected class's credit turn until its bounded packet fits | `tests/test_airtime.c` |
| R9: divergent grant validators | Join and Core use the same grant validator | `tests/test_join.c` |

Additional integration defects were reproduced and corrected:

- Cached final hop acknowledgements still authenticate against the current
  session and reread Core integrity; the cache is never durable custody evidence.
- Reference identities are generated from the platform entropy source. Stable
  identity survives key rotation; established missing stores fail closed.
- Join ACTIVE is acknowledged before membership broadcasts. Lost Join ACKs,
  finite queues and delayed TX_DONE do not permanently prevent participation.
- SX1262 restores the receive MTU after short transmissions. Queued TX preserves
  an in-progress RX until completion or a bounded false-preamble timeout.
- Carrier sensing restores the LoRa modem and sync word after the GFSK RSSI
  measurement. Switching/restoration failures are propagated.
- Authenticated lease synchronization includes bounded full exchange delay.
  Late replies shorten usable leases; stale or backwards time cannot extend one.
- Link observations are acknowledged and retried with bounded jitter. Actual
  TX_DONE starts the probe's response window; queue admission is not RF evidence.
- Pending EFFECTIVE notifications do not trigger premature E2E rekey. A delayed
  duplicate APPLY cannot undo an already confirmed effective route. A replacement
  epoch clears the old readiness and bindings before awaiting its own EFFECTIVE.
- A forced reconciliation invalidates only its matching flow and restarts that
  flow's notification state. It preserves an unrelated proof round in progress;
  the native regression reproduces the prior global proof reset.
- Same-message retries reuse the E2E ciphertext within a bounded 30-second
  window while using fresh hop counters. Context changes and expiry renew it.
- Draining with outstanding custody does not wait forever for a replacement
  route through the draining node. Only an authenticated original source, after
  ending the old context and verifying retained Core records, confirms return.
- Drain-state and removal-ready notices are acknowledged. Unchanged acknowledged
  state stops repeating; changed custody, role state or a replaced authority
  session restarts reporting. Native loss tests drop each first acknowledgement
  and check that both retries and eventual quiet occur.
- The application ledger validates persisted source/service/length fields as
  well as journal integrity, and commits before Core application acceptance.
- Installed consumers select one journal backend; repeated package discovery
  preserves that choice and rejects conflicting/unsupported choices.

Review covered ownership, bounds, persisted/wire parsing, stale contexts,
duplicate handling, restart, error propagation, public/private key separation,
package dependencies and operational evidence. It is a source review and local
test result, not an independent cryptographic certification or a physical
power-interruption qualification.
