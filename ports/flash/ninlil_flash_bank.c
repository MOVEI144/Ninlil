#include "ninlil_flash_bank.h"
#include <string.h>

#define SELECT_COMMIT UINT32_C(0x53454c31)
static uint64_t get(const uint8_t *p, size_t length)
{
    uint64_t n = 0u;
    for (size_t i = 0u; i < length; i++)
        n = (n << 8) | p[i];
    return n;
}
static void put(uint8_t *p, uint64_t n, size_t length)
{
    while (length) {
        p[--length] = (uint8_t)n;
        n >>= 8;
    }
}
static uint32_t crc(const uint8_t *p, size_t length)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0u; i < length; i++) {
        c ^= p[i];
        for (unsigned int b = 0u; b < 8u; b++)
            c = (c >> 1) ^ ((c & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return ~c;
}
static int selector(ninlil_flash_bank *b, unsigned int slot,
                    uint64_t *generation, uint64_t *end, uint32_t *sequence)
{
    uint8_t data[64];
    uint32_t checksum;
    size_t offset = b->data[0].size + slot * NINLIL_FLASH_SECTOR_SIZE;
    if (b->spare.read(b->spare.ctx, offset, data, sizeof(data)) != 0)
        return NINLIL_ERR_IO;
    if (get(data + 48, 8u) == UINT64_MAX)
        return NINLIL_ERR_EMPTY;
    checksum = crc(data, 28u);
    if (get(data + 48, 4u) != SELECT_COMMIT ||
        get(data + 52, 4u) != (uint32_t)~SELECT_COMMIT ||
        memcmp(data, "NBG\001\000\000\000\000", 8u) != 0 ||
        get(data + 28, 4u) != checksum ||
        get(data + 32, 4u) != (uint32_t)~checksum)
        return NINLIL_ERR_CORRUPT;
    for (unsigned int i = 36u; i < 64u; i++)
        if ((i < 48u || i >= 56u) && data[i] != 255u)
            return NINLIL_ERR_CORRUPT;
    *generation = get(data + 8, 8u);
    *end = get(data + 16, 8u);
    *sequence = (uint32_t)get(data + 24, 4u);
    return !*generation || *generation > UINT32_MAX ||
                   (*generation & 1u) != slot || *end > b->data[0].size ||
                   !*sequence
               ? NINLIL_ERR_CORRUPT
               : NINLIL_OK;
}
int ninlil_flash_bank_open(ninlil_flash_bank *b, const ninlil_flash_io *primary,
                           const ninlil_flash_io *spare,
                           ninlil_flash_on_record replay, void *ctx)
{
    uint64_t end = 0u;
    uint32_t sequence = 1u;
    int rc;
    if (!b || !primary || !replay)
        return NINLIL_ERR_INVALID;
    memset(b, 0, sizeof(*b));
    b->data[0] = *primary;
    if (spare) {
        if (!spare->read || !spare->write || !spare->erase ||
            primary->size > SIZE_MAX - NINLIL_FLASH_SELECT_BYTES ||
            spare->size != primary->size + NINLIL_FLASH_SELECT_BYTES)
            return NINLIL_ERR_INVALID;
        b->spare = b->data[1] = *spare;
        b->data[1].size = primary->size;
        b->enabled = 1u;
        for (unsigned int i = 0u; i < 2u; i++) {
            uint64_t gen, candidate_end;
            uint32_t candidate_sequence;
            rc = selector(b, i, &gen, &candidate_end, &candidate_sequence);
            if (rc != NINLIL_OK && rc != NINLIL_ERR_EMPTY)
                return rc;
            if (rc == NINLIL_OK && gen > b->generation) {
                b->generation = gen;
                b->active = (uint8_t)i;
                end = candidate_end;
                sequence = candidate_sequence;
            }
        }
    }
    rc = ninlil_flash_store_open(&b->store, &b->data[b->active], replay, ctx);
    if (rc == NINLIL_OK &&
        (b->store.append_offset < end || b->store.next_sequence < sequence))
        rc = NINLIL_ERR_CORRUPT;
    return rc;
}
static int checked(void *ctx, uint8_t type, const uint8_t *data,
                   uint16_t length, size_t offset)
{
    (void)ctx;
    (void)type;
    (void)data;
    (void)length;
    (void)offset;
    return NINLIL_OK;
}
int ninlil_flash_bank_rewrite(ninlil_flash_bank *b,
                              ninlil_flash_snapshot snapshot, void *ctx)
{
    ninlil_flash_store next, verified;
    uint8_t bytes[64], readback[64];
    uint64_t generation;
    size_t offset;
    unsigned int target;
    int rc;
    if (!b || !snapshot || b->poisoned || b->store.poisoned)
        return NINLIL_ERR_STATE;
    if (!b->enabled)
        return NINLIL_ERR_NOT_FOUND;
    if (b->generation == UINT32_MAX)
        return NINLIL_ERR_CAPACITY;
    generation = b->generation + 1u;
    target = (unsigned int)(generation & 1u);
    offset = b->data[0].size + target * NINLIL_FLASH_SECTOR_SIZE;
    rc = ninlil_flash_store_format(&b->data[target]);
    if (rc == NINLIL_OK)
        rc = ninlil_flash_store_open(&next, &b->data[target], checked, NULL);
    if (rc == NINLIL_OK)
        rc = snapshot(ctx, &next);
    if (rc == NINLIL_OK)
        rc =
            ninlil_flash_store_open(&verified, &b->data[target], checked, NULL);
    if (rc != NINLIL_OK)
        return rc;
    if (verified.append_offset != next.append_offset ||
        verified.next_sequence != next.next_sequence)
        return NINLIL_ERR_CORRUPT;
    memset(bytes, 255, sizeof(bytes));
    memcpy(bytes, "NBG\001\000\000\000\000", 8u);
    put(bytes + 8, generation, 8u);
    put(bytes + 16, next.append_offset, 8u);
    put(bytes + 24, next.next_sequence, 4u);
    put(bytes + 28, crc(bytes, 28u), 4u);
    put(bytes + 32, (uint32_t)~crc(bytes, 28u), 4u);
    if (b->spare.erase(b->spare.ctx, offset, NINLIL_FLASH_SECTOR_SIZE) != 0 ||
        b->spare.write(b->spare.ctx, offset, bytes, sizeof(bytes)) != 0 ||
        b->spare.read(b->spare.ctx, offset, readback, sizeof(readback)) != 0 ||
        memcmp(bytes, readback, sizeof(bytes)) != 0)
        goto uncertain;
    put(bytes + 48, SELECT_COMMIT, 4u);
    put(bytes + 52, (uint32_t)~SELECT_COMMIT, 4u);
    if (b->spare.write(b->spare.ctx, offset + 48u, bytes + 48, 16u) != 0 ||
        b->spare.read(b->spare.ctx, offset, readback, sizeof(readback)) != 0 ||
        memcmp(bytes, readback, sizeof(bytes)) != 0)
        goto uncertain;
    b->store = next;
    b->generation = generation;
    b->active = (uint8_t)target;
    return NINLIL_OK;
uncertain:
    b->poisoned = 1u;
    return NINLIL_ERR_IO;
}
