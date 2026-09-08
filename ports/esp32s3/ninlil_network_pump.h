#ifndef NINLIL_NETWORK_PUMP_H
#define NINLIL_NETWORK_PUMP_H
#include "ninlil_airtime.h"
#include "ninlil_radio_adapt.h"
#include "ninlil_routed.h"
#include "ninlil_sx1262_radio.h"
typedef struct ninlil_node ninlil_node;
typedef struct ninlil_esp_network_pump {
    ninlil_sx1262_radio *radio;
    ninlil_routed *routed;
    ninlil_node *node;
    ninlil_airtime_scheduler scheduler;
    uint64_t token;
    uint64_t tx_at_us;
    uint32_t transmitted;
    uint32_t received;
    uint32_t discarded;
    int last_receive_result;
    ninlil_radio_adapt power[16];
    uint16_t power_peer[16];
    uint8_t adaptive_power;
    int (*control_receive)(void *ctx, const uint8_t *frame, size_t length,
                           uint64_t now_ms);
    /* Revalidate a queued fragment or channel-1 frame against the current
     * provisional/authenticated control transaction before physical TX. */
    int (*control_current)(void *ctx, const uint8_t *frame, size_t length,
                           uint64_t now_ms);
    void *control_ctx;
    /* Optional bounded RX failure-injection boundary. Returning 0 simulates

     * frame loss before the protocol sees it; never reports delivery. */
    int (*accept_rx)(void *ctx, const uint8_t *frame, size_t length);
    void *rx_ctx;
} ninlil_esp_network_pump;
/* One existing radio owner task. Open only initializes the queue; it does not
 * initialize the radio, choose an RF profile, write firmware, or transmit. */
int ninlil_esp_network_open(ninlil_esp_network_pump *p,
                            ninlil_sx1262_radio *radio, ninlil_routed *routed,
                            uint32_t budget_us);
int ninlil_esp_node_open(ninlil_esp_network_pump *p, ninlil_sx1262_radio *radio,
                         ninlil_node *node, uint32_t budget_us);
int ninlil_esp_network_emit(void *ctx, uint16_t next,
                            ninlil_traffic_class traffic, const uint8_t *frame,
                            size_t length);
/* <=4 receives, one bounded Core step, one Relay opportunity, <=1 physical TX.
 * The existing SX1262 driver retains CCA/pause/profile enforcement. */
int ninlil_esp_network_step(ninlil_esp_network_pump *p);
int ninlil_esp_node_adaptive_power(ninlil_esp_network_pump *p,
                                   int8_t minimum_dbm);
#endif
