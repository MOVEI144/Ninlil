#ifndef NINLIL_FLASH_STORE_H
#define NINLIL_FLASH_STORE_H

#include "ninlil.h"

#include <stddef.h>
#include <stdint.h>

#define NINLIL_FLASH_SECTOR_SIZE 4096u
#define NINLIL_FLASH_ALIGNMENT 16u
#define NINLIL_FLASH_MAX_PAYLOAD 294u

typedef int (*ninlil_flash_on_record)(void *ctx, uint8_t type,
                                      const uint8_t *payload, uint16_t length);

typedef struct ninlil_flash_io {
    int (*read)(void *ctx, size_t offset, uint8_t *buffer, size_t length);
    int (*write)(void *ctx, size_t offset, const uint8_t *buffer,
                 size_t length);
    int (*erase)(void *ctx, size_t offset, size_t length);
    void *ctx;
    size_t size;
} ninlil_flash_io;

typedef struct ninlil_flash_store {
    ninlil_flash_io io;
    size_t append_offset;
    uint32_t next_sequence;
    uint8_t poisoned;
} ninlil_flash_store;

int ninlil_flash_store_open(ninlil_flash_store *store,
                            const ninlil_flash_io *io,
                            ninlil_flash_on_record on_record, void *record_ctx);
int ninlil_flash_store_append(ninlil_flash_store *store, uint8_t type,
                              const uint8_t *payload, uint16_t length);
int ninlil_flash_store_format(const ninlil_flash_io *io);
size_t ninlil_flash_store_append_offset(const ninlil_flash_store *store);

#endif
