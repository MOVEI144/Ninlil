# Two-board secure laboratory bench

This default-off ESP32-S3 mode exposes bounded USB commands for testing the
actual crypto, Flash and SX1262 library boundaries. It is not product firmware
and does not implement the autonomous network pump, a third-node Relay, or
production credential enrollment. There is no unsolicited RF transmission.
The loop expires after 30 minutes; each requested TX retries CCA busy for at
most two seconds. The existing Japanese fixed PHY and TXDONE checks apply.

Private P256 keys are generated inside each MCU, held only in RAM, and never
returned. The operator pins the other board's public key over the identity-
checked USB connection. Reset requires new key generation and new EDHOC.
`O` consumes completed fresh EDHOC material exactly once, closes the handshake,
and explicitly formats only session-counter slot 0. Back up all Flash before
flashing this mode. Membership is separate from cryptographic readiness.

Commands are one ASCII letter, one space, lowercase hexadecimal, newline.
One command is outstanding per board; at most 1024 decoded bytes are admitted.
Replies are `BENCH <command> <status> <hex>`. Errors expose no output bytes.

| Commands | Boundary |
|---|---|
| I / P / E / O | Public identity; USB-pinned peer; EDHOC exchange; fresh secure session |
| S / U | Encrypt/decrypt data channel |
| C / D | Encrypt/decrypt control channel |
| T / R | Actual SX1262 transmit/receive, maximum 240 bytes |
| F / G | Clear bounded fragment reassembly / ingest fragment |
| K | Resume the Flash counter store without resetting the live RX window |
| A / Q / V | Authority prepares Join / endpoint commits / authority confirms |
| Z | Close and reopen the physical control journal; return latest record |
| L / W / H | Peer authorization lookup / durable revoke and session close / stack and heap headroom |
| X | Close live secure and EDHOC contexts |

The Python controller explicitly delivers decrypted Join records to the typed
Join API over trusted USB. Therefore this verifies library and physical I/O
boundaries, not an autonomous on-device control dispatcher. Counter reopen is
not a power-cut test. Withheld frames are intentional test-owner loss, not
measured natural RF loss. Raw RF byte equality is independently checked before
submitting received bytes to authentication/reassembly.

Use the pinned esptool 5.3.0 / pyserial 3.5 environment. `hardware.py flash a`
and `flash b` require matching full 8 MiB backups, image hashes, exact board
identities, the Japan profile and a passing local gate. They leave ROM loader
active. `campaign.py secure-20260908-<unique-label>` captures both boots and
executes the campaign, leaving both MCUs in download mode. Use `hardware.py
restore a` and `restore b` after capture; it preserves control/counter readback
and verifies the complete original Flash digest after restoration. Do not
replace existing evidence labels or silently erase a nonempty journal.

`restart.py <new-label> <completed-campaign-label>` performs actual MCU resets,
checks persisted Join/revoke records without restoring authorization, tests
fragment conflict/expiry, and repeats fresh crypto over RF. This is software
reset, not power loss; ephemeral credentials require fresh USB pinning.
`analyze.py <labels...>` independently pairs all successful physical TX/RX
captures and checks monotonic, nonreused session counters in emitted envelopes.
The main campaign requires 504 matched RF frames; the restart campaign adds 52.

The `update` hardware action is a pinned r3-to-r4 app-only correction for the
initial lab counter configuration. It saves the current control/session area
and verifies it against the MCU before writing. It never clears stored records.
The fixed bench reserves 32 counters per block with a 1,000,000-counter limit,
which fits the store's 32-bit generation bound. Production checks are unchanged.
