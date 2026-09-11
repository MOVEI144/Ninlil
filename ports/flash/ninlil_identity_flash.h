#ifndef NINLIL_IDENTITY_FLASH_H
#define NINLIL_IDENTITY_FLASH_H
#include "ninlil_flash_store.h"
#include "ninlil_identity.h"

typedef struct ninlil_identity_flash {
    ninlil_flash_io flash;
} ninlil_identity_flash;

/* Dedicated bounded 8 KiB region, erased only by explicit provisioning outside
 * this API. Identity updates append to the v5 checked Flash journal. Full means
 * CAPACITY; no automatic erase, GC, rollback, or substitute identity. Hardware
 * encryption/physical protection must be configured by the deployment. */
int ninlil_identity_flash_open(ninlil_identity_flash *store,
                               const ninlil_flash_io *flash,
                               ninlil_identity_io *io);
#endif
