/* Bench-only link wrappers. Never erase NVS or touch Ninlil stores/keys.
 * The last committed checkpoint survives USB power removal. Flash latency
 * perturbs timing: this diagnoses progress, not battery life or release gates.
 */
#include "bootloader_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "ninlil_sx1262_radio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stdio.h>

esp_err_t __real_esp_light_sleep_start(void);
esp_err_t __wrap_esp_light_sleep_start(void);
int __real_ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio);
int __wrap_ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio);
void __real_bootloader_random_enable(void);
void __wrap_bootloader_random_enable(void);

static bool active;
static struct {
    int64_t time_us;
    uint32_t stage;
    int32_t result;
} record;
static void checkpoint(uint32_t stage, int rc)
{
    nvs_handle_t handle;
    record.stage = stage;
    record.time_us = esp_timer_get_time();
    if (rc != 0 && record.result == 0)
        record.result = rc;
    ESP_ERROR_CHECK(nvs_open("ninlil_sleep", NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(
        nvs_set_blob(handle, "checkpoint", &record, sizeof(record)));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

esp_err_t __wrap_esp_light_sleep_start(void)
{
    esp_err_t rc;
    active = true;
    record.result = 0;
    checkpoint(1u, 0);
    rc = __real_esp_light_sleep_start();
    checkpoint(2u, rc);
    return rc;
}

int __wrap_ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio)
{
    int rc;
    if (active)
        checkpoint(3u, 0);
    rc = __real_ninlil_sx1262_radio_recover(radio);
    if (active)
        checkpoint(4u, rc);
    return rc;
}

void __wrap_bootloader_random_enable(void)
{
    nvs_handle_t handle;
    size_t size = sizeof(record);
    if (active)
        checkpoint(5u, 0);
    __real_bootloader_random_enable();
    if (active) {
        checkpoint(6u, 0);
        active = false;
        return;
    }
    ESP_ERROR_CHECK(nvs_flash_init()); /* No erase/reinitialization fallback. */
    esp_err_t opened = nvs_open("ninlil_sleep", NVS_READONLY, &handle);
    if (opened == ESP_OK) {
        esp_err_t loaded = nvs_get_blob(handle, "checkpoint", &record, &size);
        if (loaded != ESP_ERR_NVS_NOT_FOUND) {
            ESP_ERROR_CHECK(loaded);
            ESP_ERROR_CHECK(size == sizeof(record) && record.time_us >= 0 &&
                                    record.stage >= 1u && record.stage <= 6u
                                ? ESP_OK
                                : ESP_FAIL);
        }
        nvs_close(handle);
    } else if (opened != ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK(opened);
    }
    printf("SLEEP_PROBE previous_stage=%" PRIu32 " result=%" PRId32
           " time_us=%" PRId64 "\n",
           record.stage, record.result, record.time_us);
}
