#ifndef NINLIL_FLASH_BANK_H
#define NINLIL_FLASH_BANK_H
#include "ninlil_flash_store.h"

#define NINLIL_FLASH_SELECT_BYTES (2u * NINLIL_FLASH_SECTOR_SIZE)
typedef struct ninlil_flash_bank {
    ninlil_flash_store store;
    ninlil_flash_io data[2], spare;
    uint64_t generation;
    uint8_t active, enabled, poisoned;
} ninlil_flash_bank;
typedef int (*ninlil_flash_snapshot)(void *ctx,
                                     ninlil_flash_store *replacement);
/* Spare contains one equally sized data bank followed by two selector sectors.
 * NULL spare opens a legacy append-only store. No open operation erases data.
 * Incomplete selection keeps the previous bank; ambiguous committed metadata
 * fails closed. A selected corrupt bank never falls back to older data. */
int ninlil_flash_bank_open(ninlil_flash_bank *bank,
                           const ninlil_flash_io *primary,
                           const ninlil_flash_io *spare,
                           ninlil_flash_on_record replay, void *ctx);
int ninlil_flash_bank_rewrite(ninlil_flash_bank *bank,
                              ninlil_flash_snapshot snapshot, void *ctx);
#endif
