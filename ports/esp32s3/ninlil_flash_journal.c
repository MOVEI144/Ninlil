#ifdef ESP_PLATFORM

#include "ninlil_flash_store.h"
#include "ninlil_journal.h"

#include "esp_partition.h"

#include <stdlib.h>
#include <string.h>

#define NINLIL_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x40)

struct esp_flash_context {
    const esp_partition_t *partition;
};

struct ninlil_journal {
    struct esp_flash_context flash;
    ninlil_flash_store store;
};

static int partition_read(void *ctx,
                          size_t offset,
                          uint8_t *buffer,
                          size_t length)
{
    struct esp_flash_context *flash = ctx;
    return esp_partition_read(flash->partition, offset, buffer, length) == ESP_OK
               ? 0
               : -1;
}

static int partition_write(void *ctx,
                           size_t offset,
                           const uint8_t *buffer,
                           size_t length)
{
    struct esp_flash_context *flash = ctx;
    return esp_partition_write(flash->partition, offset, buffer, length) ==
                   ESP_OK
               ? 0
               : -1;
}

static int partition_erase(void *ctx, size_t offset, size_t length)
{
    struct esp_flash_context *flash = ctx;
    return esp_partition_erase_range(flash->partition, offset, length) == ESP_OK
               ? 0
               : -1;
}

int ninlil_journal_open(ninlil_journal **out,
                        const char *location,
                        ninlil_journal_on_record on_record,
                        void *ctx)
{
    const esp_partition_t *partition;
    ninlil_journal *journal;
    ninlil_flash_io io;
    int rc;

    if (!out || !location || location[0] == '\0' || !on_record)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         NINLIL_PARTITION_SUBTYPE, location);
    if (!partition || partition->size < NINLIL_FLASH_SECTOR_SIZE ||
        partition->size % NINLIL_FLASH_SECTOR_SIZE != 0u)
        return NINLIL_ERR_NOT_FOUND;

    journal = calloc(1u, sizeof(*journal));
    if (!journal)
        return NINLIL_ERR_IO;
    journal->flash.partition = partition;
    memset(&io, 0, sizeof(io));
    io.read = partition_read;
    io.write = partition_write;
    io.erase = partition_erase;
    io.ctx = &journal->flash;
    io.size = partition->size;
    rc = ninlil_flash_store_open(&journal->store, &io, on_record, ctx);
    if (rc != NINLIL_OK) {
        free(journal);
        return rc;
    }
    *out = journal;
    return NINLIL_OK;
}

int ninlil_journal_append(ninlil_journal *journal,
                          uint8_t type,
                          const uint8_t *payload,
                          uint16_t length)
{
    if (!journal)
        return NINLIL_ERR_INVALID;
    return ninlil_flash_store_append(&journal->store, type, payload, length);
}

void ninlil_journal_close(ninlil_journal *journal)
{
    free(journal);
}

#endif
