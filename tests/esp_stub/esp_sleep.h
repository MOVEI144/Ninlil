#ifndef ESP_SLEEP_H
#define ESP_SLEEP_H
#include "esp_err.h"
#include <stdint.h>
typedef enum { ESP_SLEEP_WAKEUP_TIMER = 4 } esp_sleep_source_t;
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t duration);
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source);
esp_err_t esp_light_sleep_start(void);
#endif
