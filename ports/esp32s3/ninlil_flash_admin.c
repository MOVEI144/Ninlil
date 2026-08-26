#ifdef ESP_PLATFORM

#include "ninlil_flash_admin.h"
#include "ninlil_flash_store.h"

#include "esp_partition.h"

#define NINLIL_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x40)
#define NINLIL_PARTITION_LABEL "ninlil_journal"

int ninlil_flash_admin_erase_journal(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, NINLIL_PARTITION_SUBTYPE,
        NINLIL_PARTITION_LABEL);

    if (!partition || partition->size % NINLIL_FLASH_SECTOR_SIZE != 0u)
        return -1;
    return esp_partition_erase_range(partition, 0u, partition->size) == ESP_OK
               ? 0
               : -1;
}

#endif
