#ifdef ESP_PLATFORM

#include "ninlil_security_partitions.h"

#include <string.h>

static int partition_read(void *ctx,
                          size_t offset,
                          uint8_t *buffer,
                          size_t length)
{
    ninlil_esp_security_partition *context = ctx;

    return esp_partition_read(context->partition, offset, buffer, length) ==
                   ESP_OK
               ? 0
               : -1;
}

static int partition_write(void *ctx,
                           size_t offset,
                           const uint8_t *buffer,
                           size_t length)
{
    ninlil_esp_security_partition *context = ctx;

    return esp_partition_write(context->partition, offset, buffer, length) ==
                   ESP_OK
               ? 0
               : -1;
}

static int partition_erase(void *ctx, size_t offset, size_t length)
{
    ninlil_esp_security_partition *context = ctx;

    return esp_partition_erase_range(context->partition, offset, length) ==
                   ESP_OK
               ? 0
               : -1;
}

static int open_partition(ninlil_esp_security_partition *context,
                          ninlil_security_io *io,
                          const char *label,
                          esp_partition_subtype_t subtype)
{
    const esp_partition_t *partition;

    if (!context || !io || !label)
        return NINLIL_ERR_INVALID;
    memset(context, 0, sizeof(*context));
    memset(io, 0, sizeof(*io));
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, subtype,
                                         label);
    if (!partition || partition->size != NINLIL_SECURITY_PARTITION_SIZE)
        return NINLIL_ERR_NOT_FOUND;
    context->partition = partition;
    io->read = partition_read;
    io->write = partition_write;
    io->erase = partition_erase;
    io->ctx = context;
    io->size = partition->size;
    return NINLIL_OK;
}

int ninlil_esp_counter_io(ninlil_esp_security_partition *context,
                          ninlil_security_io *io)
{
    return open_partition(context, io, NINLIL_COUNTER_PARTITION_LABEL,
                          NINLIL_COUNTER_PARTITION_SUBTYPE);
}

int ninlil_esp_membership_io(ninlil_esp_security_partition *context,
                             ninlil_security_io *io)
{
    return open_partition(context, io, NINLIL_MEMBERSHIP_PARTITION_LABEL,
                          NINLIL_MEMBERSHIP_PARTITION_SUBTYPE);
}

#endif
