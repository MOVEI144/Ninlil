# Deployment lifecycle

September 9, 2026. Scope: Root replacement without touching children, battery
Light-sleep, explicit site/role transfer, and reuse of a retired radio address.
Ninlil remains independent of any host application. No OTA is included.
Verification and physical limitations are recorded in [the evidence](evidence/2026-09-09-maintenance/README.md).

## Root replacement

Provision one independent ES256 issuer public key and its signed Root/local
credentials once, using `ninlil_setup_update_authority` or USB upload operation 4.
The issuer private key stays outside all radio Roots; its storage/availability
belongs to the installation. Losing both the issuer and its key cannot be
recovered by inventing new trust. Device private keys are never exported.
Legacy Root-signed installations need this one-time CA setup on each device.
Subsequent Root replacement requires configuring only the new Root.

Remove the old Root. Give the new physical Root a credential for the same
network and Root address, with a strictly higher membership generation and a
nondecreasing binding. It advertises this certificate through existing powered
Relays. Existing nodes validate it, persist it, close old sessions and rejoin.
Their uncompleted messages retain the same identities and retry toward the new
Root. The old physical Root stops if it learns the newer certificate; its
persisted fence survives restart. Two isolated powered Roots cannot be made
mutually exclusive by a disconnected radio network: remove the old unit.

The generation occupies separate lease/plan fields from the physical boot
counter. Generations and physical Root boot eras are limited to 65,535 each;
exhaustion fails closed. NIv5 remembers prior Root use across later role changes.
A lost established era store cannot be recreated. Old firmware rejects NIv5.
Application records stored solely on a destroyed Root are not replicated by this
feature; applications must define their own persistence/reconciliation boundary.
The replacement credential must identify a spare device not already assigned
another address in this network. An enrolled Relay is not an unused spare Root.

## Issuer and USB reference tools

`tools/node_hil/issuer.py` uses an existing standard P-256 PKCS8 PEM key and a
separate SQLite allocation database. Neither Python nor SQLite is a requirement
of the portable runtime. Protect the key and retain the database without rolling
it back. Allocation commits before the public credential is returned. The DB
contains the latest public certificate per address, including one whose output
file could not be written. Do not silently create a fresh DB for an old network.
Install the tested optional host dependencies from `tools/node_hil/requirements.txt`.

Read public device information with `manage.py info --port PORT`; provision a
previously unused device explicitly with `manage.py provision --port PORT`.
Issue with `issuer.py issue --key KEY.pem --database SITE.sqlite --network HEX16
--public HEX97 --address N --role root|relay|endpoint|battery --expected-epoch E
--output GRANT.cose`. E is 0 for an unused address, otherwise the last issued
generation. For a moved device, set `--binding-floor` above its previous binding.
The output includes the public CA key. No private material is printed.

Install with `issuer.py install --port PORT --ca-public HEX65 --root ROOT.cose
[--credential LOCAL.cose] [--autorun]`. Omit the local credential only on Root.
Installation uses a revision precondition, validates the physical public identity
and reports saved configuration separately from delivery. `--transfer` selects
the explicit retirement flow below. Network/role changes never occur over RF.
Repeated exact final requests are idempotent after a lost reply.

## Battery operation

The ordinary ESP32-S3 image delivers a fresh application receipt after timer
sleep, and the restart-window regression passes on hardware. Forced sleep can
disconnect USB; its elapsed-time reply remains unobserved. These are scoped
bench results, with current/lifetime and field qualification still open.

`ninlil_node_suspend/resume` supports RAM-retaining platform sleep for battery
leaves only. It retains session nonce state, invalidates time/leases and requests
authenticated clock renewal on wake. While suspended, step/RX/TX checks are BUSY.
The powered Root retains pending downlink ownership during sleep. Root and
powered Relay roles cannot suspend through this API.

`ninlil_esp_node_sleep` sleeps the SX1262 and ESP32-S3, then recovers the radio.
It owns the timer wake source for the call, permits caller-configured GPIO wake,
preserves regional TX pause accounting, and uses elapsed `esp_timer` time.
The caller must finish its peripheral/storage work first. USB can disconnect.
The reference battery role uses configurable Kconfig awake/sleep windows
(120/60 seconds by default), including when no Root can be reached. Automatic
sleep pauses while a USB host is connected; power-only USB does not count as a
host (`usb_serial_jtag_is_connected`, ESP-IDF 6.0.2). USB command `E` explicitly
forces bench sleep, accepting BE32 milliseconds, 1..86,400,000. The reference owns
SAR entropy and disables/restores it around sleep. Actual applications choose
sensor timing and peripheral power. Deep sleep, measured battery lifetime and
board-level current qualification are outside this Light-sleep implementation.

## Site or role transfer

Stop the device and issue a new credential with a strictly higher local binding.
For role changes in the same network, issue a higher membership generation too;
retain the device's address. A move within radio coverage needs no new settings.
Transfer first checks no uncompleted Core work and no opaque Relay custody.
The reference also refuses any retained bulk object: adoption/retention is an
application decision, and the tool never silently deletes one. The application
ledger, stable identity, signing key and physical counters are preserved.

The setup journal atomically commits a bounded transfer intent before transport
retirement. Pending intent disables RF/autorun. Completed transport deduplication
history is retired by atomic journal rewrites, then the new setup revision is
published. Restart repeats unfinished retirement before allowing RF. A failed or
ambiguous write requires authoritative reopen; it is never reported as delivery.
This allows resource profiles to shrink from Root/powered roles to a battery role
without carrying incompatible completed transport indexes into the new profile.
Old operation keys must not be replayed as new work after transfer; applications
retain their own business deduplication record. No uncompleted operation is
cancelled or converted to UNKNOWN automatically.

## Address reuse

An independent CA can authorize a different physical identity at an existing
non-Root address using strictly higher membership and binding generations.
Peers reject this update while holding uncompleted Core ownership or opaque
Relay custody involving the old address. On acceptance they replace the same bounded registry slot, drop old
sessions/Join state and retain the generation fence across collection/restart.
Old credentials fail the fence. Stable application identity is the device ID,
not its reusable radio number. Root address replacement follows the separate
Root procedure above. Delayed/out-of-range peers adopt the new certificate when
reachable; a radio address change is not permission to deliver old work to a
different device.

## Sleep failure diagnosis

The retired diagnostic probe and its stage-6 readout are retained in the evidence
history (`214d279:tools/node_hil/sleep_probe.c`). `sleep_test.py` measures RF
recovery even after a USB timeout; that alone does not pass timing/ledger gates.
