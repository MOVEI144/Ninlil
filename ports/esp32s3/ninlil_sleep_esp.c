#include "ninlil_sleep_esp.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "ninlil_sleep.h"

int ninlil_esp_node_sleep(ninlil_esp_network_pump *p, uint32_t duration,
                          uint32_t *elapsed)
{
    int64_t began = esp_timer_get_time(), ended;
    int rc, recovered, resumed;
    if (!p || !p->node || !p->radio || !elapsed || !duration ||
        duration > 86400000u || began < 0)
        return NINLIL_ERR_INVALID;
    *elapsed = 0u;
    /* The exclusive owner is outside TX. A staged scheduler frame remains in
     * RAM and is revalidated against the resumed node before transmission. */
    rc = ninlil_node_suspend(p->node, (uint64_t)began / 1000u);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_sx1262_radio_sleep(p->radio);
    if (rc == NINLIL_OK) {
        if (esp_sleep_enable_timer_wakeup((uint64_t)duration * 1000u) != ESP_OK)
            rc = NINLIL_ERR_IO;
        else {
            rc =
                esp_light_sleep_start() == ESP_OK ? NINLIL_OK : NINLIL_ERR_BUSY;
            if (esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER) !=
                ESP_OK)
                rc = NINLIL_ERR_IO;
        }
    }
    ended = esp_timer_get_time();
    recovered = ninlil_sx1262_radio_recover(p->radio);
    if (ended < began || (uint64_t)(ended - began) / 1000u > UINT32_MAX)
        return NINLIL_ERR_STATE; /* Owner remains suspended on clock failure. */
    *elapsed = (uint32_t)((ended - began) / 1000);
    resumed = ninlil_node_resume(p->node, (uint64_t)ended / 1000u);
    return recovered != NINLIL_OK ? recovered
           : resumed != NINLIL_OK ? resumed
                                  : rc;
}
