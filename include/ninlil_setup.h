#ifndef NINLIL_SETUP_H
#define NINLIL_SETUP_H
#include "ninlil_enrollment.h"
typedef struct ninlil_setup ninlil_setup;
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
/* Borrowed snapshot. Configures local/root/members/resources/dynamic_enrollment
 * only. Caller supplies storage, clocks, counters and I/O separately. */
int ninlil_setup_config(ninlil_setup *setup, ninlil_node_config *config,
                        uint64_t *revision, int *autorun);
int ninlil_setup_advertise(ninlil_setup *setup, ninlil_node *node);
int ninlil_setup_autorun(ninlil_setup *setup, int autorun);
#endif
