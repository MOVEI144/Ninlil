#ifndef NODE_BATTERY_H
#define NODE_BATTERY_H
#include "ninlil_network_pump.h"
int node_battery_sleep(ninlil_esp_network_pump *pump, uint32_t duration,
                       uint32_t *elapsed);
int node_battery_step(ninlil_esp_network_pump *pump);
#endif
