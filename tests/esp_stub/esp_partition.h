#ifndef ESP_PARTITION_H
#define ESP_PARTITION_H
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef uint8_t esp_partition_subtype_t;
typedef struct esp_partition_t {
    size_t size;
} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
const esp_partition_t *esp_partition_find_first(int type,
                                                esp_partition_subtype_t subtype,
                                                const char *label);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset,
                             void *output, size_t length);
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *data, size_t length);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
                                    size_t offset, size_t length);
#endif
