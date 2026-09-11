#include "ninlil_flash_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
typedef struct fixture {
    uint8_t bytes[8192];
    size_t records;
} fixture;
static int read_bytes(void *ctx, size_t at, uint8_t *out, size_t length)
{
    fixture *f = ctx;
    if (at > sizeof(f->bytes) || length > sizeof(f->bytes) - at)
        return -1;
    memcpy(out, f->bytes + at, length);
    return 0;
}
static int write_bytes(void *ctx, size_t at, const uint8_t *in, size_t length)
{
    fixture *f = ctx;
    if (at > sizeof(f->bytes) || length > sizeof(f->bytes) - at)
        return -1;
    for (size_t i = 0u; i < length; i++) {
        if ((f->bytes[at + i] & in[i]) != in[i])
            return -1;
        f->bytes[at + i] &= in[i];
    }
    return 0;
}
static int erase_bytes(void *ctx, size_t at, size_t length)
{
    fixture *f = ctx;
    if (at > sizeof(f->bytes) || length > sizeof(f->bytes) - at)
        return -1;
    memset(f->bytes + at, 255, length);
    return 0;
}
static int record(void *ctx, uint8_t type, const uint8_t *data, uint16_t length,
                  size_t at)
{
    fixture *f = ctx;
    (void)type;
    (void)data;
    (void)length;
    (void)at;
    f->records++;
    return NINLIL_OK;
}
int main(void)
{
    fixture f;
    ninlil_flash_store store;
    ninlil_flash_io io = {.read = read_bytes,
                          .write = write_bytes,
                          .erase = erase_bytes,
                          .ctx = &f,
                          .size = sizeof(f.bytes)};
    unsigned int silent = 0u;
    /* Every one of the 32 zero bits across marker+complement is tested. */
    for (size_t byte = 24u; byte < 32u; byte++) {
        for (unsigned int bit = 0u; bit < 8u; bit++) {
            uint8_t mask = (uint8_t)(1u << bit);
            memset(&f, 0, sizeof(f));
            memset(f.bytes, 255, sizeof(f.bytes));
            REQUIRE(ninlil_flash_store_open(&store, &io, record, &f) ==
                    NINLIL_OK);
            REQUIRE(ninlil_flash_store_append(
                        &store, 1u, (const uint8_t *)"owned", 5u) == NINLIL_OK);
            if ((f.bytes[byte] & mask) != 0u)
                continue;
            f.bytes[byte] |=
                mask; /* Corruption of an already successful commit. */
            REQUIRE(ninlil_flash_store_open(&store, &io, record, &f) ==
                    NINLIL_OK);
            REQUIRE(f.records == 0u);
            REQUIRE(ninlil_flash_store_append(
                        &store, 1u, (const uint8_t *)"later", 5u) == NINLIL_OK);
            REQUIRE(ninlil_flash_store_open(&store, &io, record, &f) ==
                    NINLIL_OK);
            REQUIRE(f.records ==
                    1u); /* Only the later record remains visible. */
            silent++;
        }
    }
    REQUIRE(silent == 32u);
    printf("REPRODUCED Flash: %u single-bit 0->1 marker corruptions silently "
           "omit committed ownership; reopening and later append return OK\n",
           silent);
    return 0;
}
