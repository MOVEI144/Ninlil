#ifndef NINLIL_NETWORK_PUMP_H
#define NINLIL_NETWORK_PUMP_H
#include "ninlil_airtime.h"
#include "ninlil_routed.h"
#include "ninlil_sx1262_radio.h"
typedef struct ninlil_esp_network_pump {
    ninlil_sx1262_radio *radio;
    ninlil_routed *routed;
    ninlil_airtime_scheduler scheduler;
    uint64_t token;
    int (*control_receive)(void *ctx, const uint8_t *frame, size_t length,
                           uint64_t now_ms);
    /* Revalidate a queued fragment or channel-1 frame against the current
     * provisional/authenticated control transaction before physical TX. */
    int (*control_current)(void *ctx, const uint8_t *frame, size_t length,
                           uint64_t now_ms);
    void *control_ctx;
} ninlil_esp_network_pump;
/* One existing radio owner task. Open only initializes the queue; it does not
 * initialize the radio, choose an RF profile, write firmware, or transmit. */
int ninlil_esp_network_open(ninlil_esp_network_pump *p,
                            ninlil_sx1262_radio *radio, ninlil_routed *routed,
                            uint32_t budget_us);
int ninlil_esp_network_emit(void *ctx, uint16_t next,
                            ninlil_traffic_class traffic, const uint8_t *frame,
                            size_t length);
/* <=4 receives, one bounded Core step, one Relay opportunity, <=1 physical TX.
 * The existing SX1262 driver retains CCA/pause/profile enforcement. */
int ninlil_esp_network_step(ninlil_esp_network_pump *p);
#endif
