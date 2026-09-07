#ifndef SECURITY_TEST_IO_H
#define SECURITY_TEST_IO_H
#include "ninlil_security_state.h"
#include <string.h>
typedef struct flash {
    uint8_t bytes[NINLIL_SECURITY_PARTITION_SIZE];
    int fail;
} flash;

static inline int read_flash(void *ctx, size_t offset, uint8_t *data,
                             size_t size)
{
    flash *f = ctx;
    if (offset > sizeof(f->bytes) || size > sizeof(f->bytes) - offset)
        return NINLIL_ERR_IO;
    memcpy(data, f->bytes + offset, size);
    return NINLIL_OK;
}

static inline int write_flash(void *ctx, size_t offset, const uint8_t *data,
                              size_t size)
{
    flash *f = ctx;
    size_t i;
    if (f->fail || offset > sizeof(f->bytes) ||
        size > sizeof(f->bytes) - offset)
        return NINLIL_ERR_IO;
    for (i = 0; i < size; i++) {
        if ((f->bytes[offset + i] & data[i]) != data[i])
            return NINLIL_ERR_IO;
        f->bytes[offset + i] &= data[i];
    }
    return NINLIL_OK;
}

static inline int erase_flash(void *ctx, size_t offset, size_t size)
{
    flash *f = ctx;
    if (f->fail || offset > sizeof(f->bytes) ||
        size > sizeof(f->bytes) - offset)
        return NINLIL_ERR_IO;
    memset(f->bytes + offset, 255, size);
    return NINLIL_OK;
}

#endif
