#ifdef ESP_PLATFORM

#include "ninlil_flash_bank.h"
#include "ninlil_journal.h"

#include "esp_partition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NINLIL_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x40)

struct esp_flash_context {
    const esp_partition_t *partition;
};

struct ninlil_journal {
    struct esp_flash_context flash;
    struct esp_flash_context spare_flash;
    ninlil_flash_bank bank;
    ninlil_journal_on_record on_record;
    void *record_ctx;
};

static int replay_record(void *ctx, uint8_t type, const uint8_t *payload,
                         uint16_t length, size_t payload_offset)
{
    ninlil_journal *journal = ctx;
    ninlil_journal_ref reference;

    reference.offset = payload_offset;
    reference.generation = (uint32_t)journal->bank.generation;
    reference.length = length;
    reference.type = type;
    return journal->on_record(journal->record_ctx, type, payload, length,
                              &reference);
}

static int partition_read(void *ctx, size_t offset, uint8_t *buffer,
                          size_t length)
{
    struct esp_flash_context *flash = ctx;
    return esp_partition_read(flash->partition, offset, buffer, length) ==
                   ESP_OK
               ? 0
               : -1;
}

static int partition_write(void *ctx, size_t offset, const uint8_t *buffer,
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

int ninlil_journal_open(ninlil_journal **out, const char *location,
                        uint64_t maximum_bytes,
                        ninlil_journal_on_record on_record, void *ctx)
{
    const esp_partition_t *partition;
    ninlil_journal *journal;
    ninlil_flash_io io, spare;
    const esp_partition_t *spare_partition = NULL;
    char spare_label[17];
    int rc;

    if (!out || !location || location[0] == '\0' || !on_record ||
        maximum_bytes < NINLIL_FLASH_SECTOR_SIZE)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         NINLIL_PARTITION_SUBTYPE, location);
    if (!partition || partition->size < NINLIL_FLASH_SECTOR_SIZE ||
        partition->size % NINLIL_FLASH_SECTOR_SIZE != 0u)
        return NINLIL_ERR_NOT_FOUND;
    if (partition->size > maximum_bytes)
        return NINLIL_ERR_CAPACITY;

    journal = calloc(1u, sizeof(*journal));
    if (!journal)
        return NINLIL_ERR_IO;
    journal->flash.partition = partition;
    journal->on_record = on_record;
    journal->record_ctx = ctx;
    memset(&io, 0, sizeof(io));
    io.read = partition_read;
    io.write = partition_write;
    io.erase = partition_erase;
    io.ctx = &journal->flash;
    io.size = partition->size;
    if (strlen(location) <= 13u) {
        (void)snprintf(spare_label, sizeof(spare_label), "%s_gc", location);
        spare_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, NINLIL_PARTITION_SUBTYPE, spare_label);
    }
    spare = io;
    if (spare_partition) {
        journal->spare_flash.partition = spare_partition;
        spare.ctx = &journal->spare_flash;
        spare.size = spare_partition->size;
    }
    rc = ninlil_flash_bank_open(&journal->bank, &io,
                                spare_partition ? &spare : NULL, replay_record,
                                journal);
    if (rc != NINLIL_OK) {
        free(journal);
        return rc;
    }
    *out = journal;
    return NINLIL_OK;
}

int ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                          const uint8_t *payload, uint16_t length,
                          ninlil_journal_ref *reference)
{
    size_t payload_offset;
    int rc;

    if (!journal || journal->bank.poisoned)
        return NINLIL_ERR_INVALID;
    rc = ninlil_flash_store_append_ref(&journal->bank.store, type, payload,
                                       length, &payload_offset);
    if (rc == NINLIL_OK && reference) {
        reference->offset = payload_offset;
        reference->generation = (uint32_t)journal->bank.generation;
        reference->length = length;
        reference->type = type;
    }
    return rc;
}

int ninlil_journal_read(ninlil_journal *journal,
                        const ninlil_journal_ref *reference,
                        uint16_t relative_offset, uint8_t *buffer,
                        uint16_t length)
{
    if (!journal || !reference || journal->bank.poisoned ||
        reference->offset > SIZE_MAX)
        return NINLIL_ERR_INVALID;
    if (reference->generation != journal->bank.generation)
        return NINLIL_ERR_STATE;
    return ninlil_flash_store_read(
        &journal->bank.store, (size_t)reference->offset, reference->length,
        reference->type, relative_offset, buffer, length);
}

int ninlil_journal_usage(ninlil_journal *j, uint64_t *used, uint64_t *capacity)
{
    if (!j || !used || !capacity || j->bank.poisoned || j->bank.store.poisoned)
        return NINLIL_ERR_IO;
    *used = j->bank.store.append_offset;
    *capacity = j->bank.store.io.size;
    return NINLIL_OK;
}
int ninlil_journal_visit(ninlil_journal *j, ninlil_journal_on_record visit,
                         void *ctx)
{
    ninlil_journal scan = {0};
    uint64_t used, capacity;
    if (!visit || ninlil_journal_usage(j, &used, &capacity) != NINLIL_OK)
        return NINLIL_ERR_IO;
    scan.on_record = visit;
    scan.bank.generation = j->bank.generation;
    scan.record_ctx = ctx;
    return ninlil_flash_store_open(&scan.bank.store, &j->bank.store.io,
                                   replay_record, &scan);
}
typedef struct snapshot_context {
    ninlil_journal_snapshot snapshot;
    void *ctx;
    uint64_t generation;
} snapshot_context;
static int snapshot_bridge(void *ctx, ninlil_flash_store *store)
{
    snapshot_context *c = ctx;
    ninlil_journal next = {0};
    int rc;
    next.bank.store = *store;
    next.bank.generation = c->generation;
    rc = c->snapshot(c->ctx, &next);
    *store = next.bank.store;
    return rc;
}
int ninlil_journal_rewrite(ninlil_journal *j, ninlil_journal_snapshot snapshot,
                           void *ctx)
{
    snapshot_context c = {snapshot, ctx, j ? j->bank.generation + 1u : 0u};
    return j && snapshot
               ? ninlil_flash_bank_rewrite(&j->bank, snapshot_bridge, &c)
               : NINLIL_ERR_INVALID;
}

void ninlil_journal_close(ninlil_journal *journal)
{
    free(journal);
}

#endif
