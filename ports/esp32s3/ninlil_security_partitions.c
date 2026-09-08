#ifdef ESP_PLATFORM

#include "ninlil_security_partitions.h"

#include <string.h>

static int partition_read(void *ctx, size_t offset, uint8_t *buffer,
                          size_t length)
{
    ninlil_esp_security_partition *context = ctx;

    if (offset > context->size || length > context->size - offset)
        return -1;
    return esp_partition_read(context->partition, context->offset + offset,
                              buffer, length) == ESP_OK
               ? 0
               : -1;
}

static int partition_write(void *ctx, size_t offset, const uint8_t *buffer,
                           size_t length)
{
    ninlil_esp_security_partition *context = ctx;

    if (offset > context->size || length > context->size - offset)
        return -1;
    return esp_partition_write(context->partition, context->offset + offset,
                               buffer, length) == ESP_OK
               ? 0
               : -1;
}

static int partition_erase(void *ctx, size_t offset, size_t length)
{
    ninlil_esp_security_partition *context = ctx;

    if (offset > context->size || length > context->size - offset)
        return -1;
    return esp_partition_erase_range(context->partition,
                                     context->offset + offset, length) == ESP_OK
               ? 0
               : -1;
}

int ninlil_esp_security_region(ninlil_esp_security_partition *context,
                               ninlil_security_io *io, const char *label,
                               size_t offset, size_t size)
{
    const esp_partition_t *partition;
    if (!context || !io || !label || !size ||
        offset % NINLIL_SECURITY_SECTOR_SIZE ||
        size % NINLIL_SECURITY_SECTOR_SIZE)
        return NINLIL_ERR_INVALID;
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         ESP_PARTITION_SUBTYPE_ANY, label);
    if (!partition || offset > partition->size ||
        size > partition->size - offset)
        return NINLIL_ERR_NOT_FOUND;
    context->partition = partition;
    context->offset = offset;
    context->size = size;
    *io = (ninlil_security_io){partition_read, partition_write, partition_erase,
                               context, size};
    return NINLIL_OK;
}

static int open_partition(ninlil_esp_security_partition *context,
                          ninlil_security_io *io, const char *label,
                          esp_partition_subtype_t subtype)
{
    const esp_partition_t *partition;

    if (!context || !io || !label)
        return NINLIL_ERR_INVALID;
    memset(context, 0, sizeof(*context));
    memset(io, 0, sizeof(*io));
    partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, subtype, label);
    if (!partition || partition->size != NINLIL_SECURITY_PARTITION_SIZE)
        return NINLIL_ERR_NOT_FOUND;
    context->partition = partition;
    context->size = partition->size;
    io->read = partition_read;
    io->write = partition_write;
    io->erase = partition_erase;
    io->ctx = context;
    io->size = partition->size;
    return NINLIL_OK;
}

int ninlil_esp_session_counter_io(ninlil_esp_security_partition *context,
                                  ninlil_security_io *io, uint16_t slot)
{
    const esp_partition_t *partition;
    if (!context || !io || slot >= NINLIL_SESSION_COUNTER_SLOTS)
        return NINLIL_ERR_INVALID;
    memset(context, 0, sizeof(*context));
    memset(io, 0, sizeof(*io));
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         NINLIL_SESSION_PARTITION_SUBTYPE,
                                         NINLIL_SESSION_PARTITION_LABEL);
    if (!partition || partition->size != NINLIL_SESSION_COUNTER_SLOTS *
                                             NINLIL_SECURITY_PARTITION_SIZE)
        return NINLIL_ERR_NOT_FOUND;
    context->partition = partition;
    context->offset = (size_t)slot * NINLIL_SECURITY_PARTITION_SIZE;
    context->size = NINLIL_SECURITY_PARTITION_SIZE;
    io->read = partition_read;
    io->write = partition_write;
    io->erase = partition_erase;
    io->ctx = context;
    io->size = context->size;
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
