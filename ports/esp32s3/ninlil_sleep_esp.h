#ifndef NINLIL_SLEEP_ESP_H
#define NINLIL_SLEEP_ESP_H
#include "ninlil_network_pump.h"
/* Blocks the exclusive radio owner for 1..86400000 ms, or until a caller-set
 * GPIO wakeup. Owns the timer wake source for this call. RAM and queues stay
 * allocated; regional airtime/pause accounting is not reset. USB may detach.
 * The application finishes peripheral/storage work before calling. */
int ninlil_esp_node_sleep(ninlil_esp_network_pump *pump, uint32_t duration_ms,
                          uint32_t *elapsed_ms);
#endif
