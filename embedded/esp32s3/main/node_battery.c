#include "node_battery.h"
#include "bootloader_random.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "ninlil_sleep_esp.h"
#include "node_example.h"
#include "sdkconfig.h"
static uint64_t awake_until;
void node_battery_reset(void)
{
    awake_until = 0u;
}
int node_battery_sleep(ninlil_esp_network_pump *p, uint32_t duration,
                       uint32_t *elapsed)
{
    /* This example exclusively owns the SAR entropy source. Applications with
     * ADC drivers provide their own platform sleep preparation instead. */
    bootloader_random_disable();
    int rc = ninlil_esp_node_sleep(p, duration, elapsed);
    bootloader_random_enable();
    awake_until = (uint64_t)esp_timer_get_time() / 1000u +
                  (uint64_t)CONFIG_NINLIL_BATTERY_AWAKE_SECONDS * 1000u;
    return rc;
}
int node_battery_step(ninlil_esp_network_pump *p)
{
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000u;
    uint32_t elapsed;
    if (node_config.resources.role != NINLIL_ROLE_BATTERY_LEAF)
        return NINLIL_OK;
    if (usb_serial_jtag_is_connected()) {
        node_battery_reset(); /* Preserve configuration access on USB hosts. */
        return NINLIL_OK;
    }
    if (!awake_until)
        awake_until =
            now + (uint64_t)CONFIG_NINLIL_BATTERY_AWAKE_SECONDS * 1000u;
    if (now < awake_until)
        return NINLIL_OK;
    return node_battery_sleep(
        p, (uint32_t)CONFIG_NINLIL_BATTERY_SLEEP_SECONDS * 1000u, &elapsed);
}
