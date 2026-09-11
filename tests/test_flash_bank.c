#include "ninlil_flash_bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
#define BANK_BYTES 4096u
typedef struct memory {
    uint8_t bytes[4u * BANK_BYTES];
    size_t lengths[32];
    unsigned int calls, fail;
    size_t prefix;
} memory;
typedef struct region {
    memory *flash;
    size_t base;
} region;
static memory flash, baseline;
static int read_bytes(void *ctx, size_t offset, uint8_t *out, size_t length)
{
    region *r = ctx;
    if (r->base + offset > sizeof(flash.bytes) ||
        length > sizeof(flash.bytes) - r->base - offset)
        return -1;
    memcpy(out, r->flash->bytes + r->base + offset, length);
    return 0;
}
static int mutate(region *r, size_t offset, const uint8_t *data, size_t length,
                  int erase)
{
    memory *m = r->flash;
    size_t amount = length;
    if (m->calls >= 32u || r->base + offset > sizeof(m->bytes) ||
        length > sizeof(m->bytes) - r->base - offset)
        return -1;
    m->lengths[m->calls++] = length;
    if (m->calls == m->fail && m->prefix < amount)
        amount = m->prefix;
    for (size_t i = 0u; i < amount; i++) {
        uint8_t *p = &m->bytes[r->base + offset + i];
        if (!erase && ((*p & data[i]) != data[i]))
            return -1;
        *p = erase ? UINT8_MAX : data[i];
    }
    return m->calls == m->fail ? -1 : 0;
}
static int write_bytes(void *ctx, size_t offset, const uint8_t *data,
                       size_t length)
{
    return mutate(ctx, offset, data, length, 0);
}
static int erase_bytes(void *ctx, size_t offset, size_t length)
{
    return mutate(ctx, offset, NULL, length, 1);
}
static int capture(void *ctx, uint8_t type, const uint8_t *data,
                   uint16_t length, size_t offset)
{
    unsigned int *bits = ctx;
    (void)offset;
    if (length != 4u || type != 1u || data[1] != 42u || data[2] != 43u ||
        data[3] != 44u)
        return NINLIL_ERR_CORRUPT;
    if (data[0] == 10u || data[0] == 11u)
        *bits |= 1u << (data[0] - 10u);
    return NINLIL_OK;
}
static int snapshot(void *ctx, ninlil_flash_store *next)
{
    (void)ctx;
    for (uint8_t id = 10u; id <= 11u; id++) {
        uint8_t data[4] = {id, 42u, 43u, 44u};
        int rc = ninlil_flash_store_append(next, 1u, data, sizeof(data));
        if (rc != NINLIL_OK)
            return rc;
    }
    return NINLIL_OK;
}
int main(void)
{
    region primary = {&flash, 0u}, spare = {&flash, BANK_BYTES};
    ninlil_flash_io a = {read_bytes, write_bytes, erase_bytes, &primary,
                         BANK_BYTES};
    ninlil_flash_io b = {read_bytes, write_bytes, erase_bytes, &spare,
                         3u * BANK_BYTES};
    ninlil_flash_bank bank;
    unsigned int bits = 0u, trials = 0u;
    memset(flash.bytes, 255, sizeof(flash.bytes));
    CHECK(ninlil_flash_bank_open(&bank, &a, &b, capture, &bits) == NINLIL_OK);
    CHECK(snapshot(NULL, &bank.store) == NINLIL_OK);
    for (unsigned int generation = 0u; generation < 3u; generation++) {
        unsigned int operations;
        size_t lengths[32];
        flash.calls = flash.fail = 0u;
        baseline = flash;
        CHECK(ninlil_flash_bank_rewrite(&bank, snapshot, NULL) == NINLIL_OK);
        operations = flash.calls;
        memcpy(lengths, flash.lengths, sizeof(lengths));
        for (unsigned int cut = 1u; cut <= operations; cut++) {
            size_t prefixes[5] = {0u, 1u, lengths[cut - 1u] / 2u,
                                  lengths[cut - 1u] - 1u, lengths[cut - 1u]};
            for (unsigned int p = 0u; p < 5u; p++) {
                int rc;
                flash = baseline;
                bits = 0u;
                CHECK(ninlil_flash_bank_open(&bank, &a, &b, capture, &bits) ==
                          NINLIL_OK &&
                      bits == 3u);
                flash.fail = cut;
                flash.prefix = prefixes[p];
                CHECK(ninlil_flash_bank_rewrite(&bank, snapshot, NULL) !=
                      NINLIL_OK);
                flash.fail = 0u;
                bits = 0u;
                rc = ninlil_flash_bank_open(&bank, &a, &b, capture, &bits);
                if (rc == NINLIL_OK) {
                    CHECK(bits == 3u);
                    CHECK(bank.generation == generation ||
                          bank.generation == generation + 1u);
                } else {
                    /* Only ambiguous selector erase/commit may fail closed.
                     * Unpublished payload writes must reopen the old state. */
                    CHECK(rc == NINLIL_ERR_CORRUPT &&
                          (cut == operations || cut == operations - 2u));
                }
                trials++;
            }
        }
        flash = baseline;
        bits = 0u;
        CHECK(ninlil_flash_bank_open(&bank, &a, &b, capture, &bits) ==
              NINLIL_OK);
        CHECK(ninlil_flash_bank_rewrite(&bank, snapshot, NULL) == NINLIL_OK);
    }
    /* A damaged selected bank never rolls back to a complete older bank. */
    flash.bytes[(bank.active ? BANK_BYTES : 0u) + 33u] ^= 1u;
    bits = 0u;
    CHECK(ninlil_flash_bank_open(&bank, &a, &b, capture, &bits) ==
          NINLIL_ERR_CORRUPT);
    printf("NOR collection: %u interrupted mutation boundaries preserve "
           "ownership or fail closed PASS\n",
           trials);
    return 0;
}
