#include "ninlil_identity_flash.h"
#include <string.h>

typedef struct identity_scan {
    uint8_t record[NINLIL_IDENTITY_RECORD_SIZE];
    uint64_t generation;
} identity_scan;

static int capture(void *ctx, uint8_t type, const uint8_t *data,
                   uint16_t length, size_t offset)
{
    identity_scan *scan = ctx;
    uint64_t generation = 0u;
    size_t i;
    (void)offset;
    if (type != 1u || length != NINLIL_IDENTITY_RECORD_SIZE ||
        (memcmp(data, "NI\002\000", 4u) != 0 &&
         (memcmp(data, "NI\003", 3u) != 0 || data[3] > 1u) &&
         (memcmp(data, "NI\004", 3u) != 0 || data[3] != 3u) &&
         (memcmp(data, "NI\005", 3u) != 0 || (data[3] != 5u && data[3] != 7u))))
        return NINLIL_ERR_CORRUPT;
    for (i = 4u; i < 12u; i++)
        generation = (generation << 8) | data[i];
    if (!generation || scan->generation == UINT64_MAX ||
        generation != scan->generation + 1u)
        return NINLIL_ERR_CORRUPT;
    memcpy(scan->record, data, NINLIL_IDENTITY_RECORD_SIZE);
    scan->generation = generation;
    return NINLIL_OK;
}

static int read_record(void *ctx, uint8_t record[NINLIL_IDENTITY_RECORD_SIZE])
{
    ninlil_identity_flash *s = ctx;
    ninlil_flash_store journal;
    identity_scan scan = {0};
    int rc = ninlil_flash_store_open(&journal, &s->flash, capture, &scan);
    if (rc == NINLIL_OK) {
        if (scan.generation)
            memcpy(record, scan.record, NINLIL_IDENTITY_RECORD_SIZE);
        else
            rc = NINLIL_ERR_EMPTY;
    }
    ninlil_secret_clear(&scan, sizeof(scan));
    return rc;
}

static int commit_record(void *ctx,
                         const uint8_t record[NINLIL_IDENTITY_RECORD_SIZE])
{
    ninlil_identity_flash *s = ctx;
    ninlil_flash_store journal;
    identity_scan scan = {0};
    int rc = ninlil_flash_store_open(&journal, &s->flash, capture, &scan);
    if (rc == NINLIL_OK)
        rc = capture(&scan, 1u, record, NINLIL_IDENTITY_RECORD_SIZE, 0u);
    if (rc == NINLIL_OK)
        rc = ninlil_flash_store_append(&journal, 1u, record,
                                       NINLIL_IDENTITY_RECORD_SIZE);
    ninlil_secret_clear(&scan, sizeof(scan));
    return rc;
}

int ninlil_identity_flash_open(ninlil_identity_flash *s,
                               const ninlil_flash_io *flash,
                               ninlil_identity_io *io)
{
    if (!s || !io || !flash || !flash->read || !flash->write || !flash->erase ||
        flash->size != 2u * NINLIL_FLASH_SECTOR_SIZE)
        return NINLIL_ERR_INVALID;
    s->flash = *flash;
    *io = (ninlil_identity_io){read_record, commit_record, s};
    return NINLIL_OK;
}
