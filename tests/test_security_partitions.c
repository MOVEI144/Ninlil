#include "ninlil_security_partitions.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct fake_partition {
    esp_partition_t descriptor;
    uint8_t bytes[NINLIL_SECURITY_PARTITION_SIZE];
    esp_partition_subtype_t subtype;
    const char *label;
} fake_partition;

static fake_partition counter_partition;
static fake_partition membership_partition;
static uint8_t hide_counter;
static uint8_t hide_membership;

static fake_partition *from_descriptor(const esp_partition_t *partition)
{
    if (partition == &counter_partition.descriptor)
        return &counter_partition;
    if (partition == &membership_partition.descriptor)
        return &membership_partition;
    return NULL;
}

const esp_partition_t *esp_partition_find_first(int type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    if (type != ESP_PARTITION_TYPE_DATA || !label)
        return NULL;
    if (!hide_counter && subtype == counter_partition.subtype &&
        strcmp(label, counter_partition.label) == 0)
        return &counter_partition.descriptor;
    if (!hide_membership && subtype == membership_partition.subtype &&
        strcmp(label, membership_partition.label) == 0)
        return &membership_partition.descriptor;
    return NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset,
                             void *output, size_t length)
{
    fake_partition *fake = from_descriptor(partition);

    if (!fake || !output || offset > sizeof(fake->bytes) ||
        length > sizeof(fake->bytes) - offset)
        return -1;
    memcpy(output, fake->bytes + offset, length);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *data, size_t length)
{
    fake_partition *fake = from_descriptor(partition);
    const uint8_t *input = data;
    size_t index;

    if (!fake || !data || offset > sizeof(fake->bytes) ||
        length > sizeof(fake->bytes) - offset)
        return -1;
    for (index = 0u; index < length; index++) {
        if ((fake->bytes[offset + index] & input[index]) != input[index])
            return -1;
    }
    for (index = 0u; index < length; index++)
        fake->bytes[offset + index] &= input[index];
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
                                    size_t offset, size_t length)
{
    fake_partition *fake = from_descriptor(partition);

    if (!fake || offset % NINLIL_SECURITY_SECTOR_SIZE != 0u ||
        length % NINLIL_SECURITY_SECTOR_SIZE != 0u ||
        offset > sizeof(fake->bytes) || length > sizeof(fake->bytes) - offset)
        return -1;
    memset(fake->bytes + offset, UINT8_C(0xFF), length);
    return ESP_OK;
}

static void initialize_partitions(void)
{
    memset(&counter_partition, 0, sizeof(counter_partition));
    memset(&membership_partition, 0, sizeof(membership_partition));
    memset(counter_partition.bytes, UINT8_C(0xFF),
           sizeof(counter_partition.bytes));
    memset(membership_partition.bytes, UINT8_C(0xFF),
           sizeof(membership_partition.bytes));
    counter_partition.descriptor.size = sizeof(counter_partition.bytes);
    counter_partition.subtype = NINLIL_COUNTER_PARTITION_SUBTYPE;
    counter_partition.label = NINLIL_COUNTER_PARTITION_LABEL;
    membership_partition.descriptor.size = sizeof(membership_partition.bytes);
    membership_partition.subtype = NINLIL_MEMBERSHIP_PARTITION_SUBTYPE;
    membership_partition.label = NINLIL_MEMBERSHIP_PARTITION_LABEL;
    hide_counter = 0u;
    hide_membership = 0u;
}

int main(void)
{
    ninlil_esp_security_partition counter_context;
    ninlil_esp_security_partition membership_context;
    ninlil_security_io counter_io;
    ninlil_security_io membership_io;
    uint8_t value = UINT8_C(0x7F);
    uint8_t output = 0u;

    initialize_partitions();
    CHECK(ninlil_esp_counter_io(NULL, &counter_io) == NINLIL_ERR_INVALID);
    CHECK(ninlil_esp_counter_io(&counter_context, NULL) == NINLIL_ERR_INVALID);
    CHECK(ninlil_esp_counter_io(&counter_context, &counter_io) == NINLIL_OK);
    CHECK(counter_io.size == NINLIL_SECURITY_PARTITION_SIZE);
    CHECK(counter_io.write(counter_io.ctx, 0u, &value, 1u) == 0);
    CHECK(counter_io.read(counter_io.ctx, 0u, &output, 1u) == 0);
    CHECK(output == value);
    CHECK(counter_io.erase(counter_io.ctx, 0u, NINLIL_SECURITY_SECTOR_SIZE) ==
          0);
    CHECK(counter_io.read(counter_io.ctx, 0u, &output, 1u) == 0);
    CHECK(output == UINT8_C(0xFF));

    CHECK(ninlil_esp_membership_io(&membership_context, &membership_io) ==
          NINLIL_OK);
    CHECK(membership_io.ctx == &membership_context);
    hide_counter = 1u;
    CHECK(ninlil_esp_counter_io(&counter_context, &counter_io) ==
          NINLIL_ERR_NOT_FOUND);
    CHECK(counter_context.partition == NULL && counter_io.read == NULL &&
          counter_io.write == NULL && counter_io.erase == NULL);
    hide_counter = 0u;
    counter_partition.descriptor.size -= NINLIL_SECURITY_SECTOR_SIZE;
    CHECK(ninlil_esp_counter_io(&counter_context, &counter_io) ==
          NINLIL_ERR_NOT_FOUND);
    membership_partition.descriptor.size += NINLIL_SECURITY_SECTOR_SIZE;
    CHECK(ninlil_esp_membership_io(&membership_context, &membership_io) ==
          NINLIL_ERR_NOT_FOUND);
    puts("security_partitions PASS");
    return 0;
}
