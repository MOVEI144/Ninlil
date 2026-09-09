#ifndef NINLIL_SETUP_H
#define NINLIL_SETUP_H
#include "ninlil_enrollment.h"
typedef struct ninlil_setup ninlil_setup;
#define NINLIL_DEPLOYMENT_PACKET_MAX (76u + 2u * NINLIL_ADMISSION_MAX)
/* Packet: expected revision BE64, autorun byte, CA public key (65), Root
 * credential length BE16, Root credential, optional local credential. */
int ninlil_setup_packet(ninlil_setup *setup, const uint8_t *packet,
                        size_t length);
/* Begin commits an RF-inhibiting intent. Caller first checks all application
 * ownership, then opens the old configuration offline and retires its stores.
 * Finish is legal only after successful retirement; interrupted work resumes
 * from the same intent. These are trusted local management operations. */
int ninlil_setup_transfer_begin(ninlil_setup *setup, const uint8_t *packet,
                                size_t length);
int ninlil_setup_transfer_pending(const ninlil_setup *setup);
int ninlil_setup_transfer_finish(ninlil_setup *setup);
/* Local deployment data only; no private keys. Uses the chosen journal port.
 * Empty is unconfigured, malformed/partial committed state is corruption.
 * The identity and exclusive owner outlive the setup handle. */
int ninlil_setup_open(ninlil_setup **out, const char *location,
                      ninlil_identity *identity);
void ninlil_setup_close(ninlil_setup *setup);
/* Physical/trusted management boundary, never raw RF input. Root key is an
 * explicit trust assignment; a child also proves its identity using the signed
 * credential. Atomic replacement with a revision precondition; the exact last
 * successful request is an idempotent no-op after a lost response. Caller
 * must separately drain/archive old network custody before changing networks.
 */
int ninlil_setup_update(ninlil_setup *setup, uint64_t expected_revision,
                        const ninlil_node_member *root,
                        const uint8_t *credential, size_t length, int autorun);
/* Initial trusted CA assignment or matching revision update. Root and local
 * credentials use the same independent issuer. Private CA key stays outside. */
int ninlil_setup_update_authority(ninlil_setup *setup,
                                  uint64_t expected_revision,
                                  const uint8_t authority_key[65],
                                  const uint8_t *root_credential,
                                  size_t root_length, const uint8_t *credential,
                                  size_t length, int autorun);
/* Borrowed snapshot. Configures local/root/members/resources/dynamic_enrollment
 * only. Caller supplies storage, clocks, counters and I/O separately. */
int ninlil_setup_config(ninlil_setup *setup, ninlil_node_config *config,
                        uint64_t *revision, int *autorun);
int ninlil_setup_advertise(ninlil_setup *setup, ninlil_node *node);
int ninlil_setup_autorun(ninlil_setup *setup, int autorun);
/* With saved transfer intent already committed and RF disabled, retire only
 * completed transport state. Pending Core or opaque Relay ownership is BUSY.
 * Close the offline node afterward. Interrupted retirement may be repeated. */
int ninlil_node_retire_deployment(ninlil_node *node);
int ninlil_node_deployment_idle(ninlil_node *node);
#endif
