#ifndef NINLIL_NETWORK_PUMP_H
#define NINLIL_NETWORK_PUMP_H
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "ninlil_airtime.h"
#include "ninlil_probe_monitor.h"
#include "ninlil_radio_adapt.h"
#include "ninlil_radio_feedback.h"
#include "ninlil_route_optimizer.h"
#include "ninlil_routed.h"
#include "ninlil_sx1262_radio.h"
typedef struct ninlil_node ninlil_node;
typedef struct ninlil_esp_network_pump {
    ninlil_sx1262_radio *radio;
    ninlil_routed *routed;
    ninlil_node *node;
    ninlil_airtime_scheduler scheduler;
    ninlil_probe_monitor *monitor;
#ifdef CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL
    ninlil_probe_monitor probe_workspace;
#endif
    uint8_t closed_probes;
    int last_measurement_result;
#ifdef CONFIG_NINLIL_ROUTE_CANDIDATES_EXPERIMENTAL
    ninlil_route_optimizer route_workspace;
    ninlil_search_node route_nodes[16];
    ninlil_search_edge route_edges[64];
#endif
    uint64_t token;
    uint64_t tx_at_us;
    uint32_t transmitted;
    uint32_t received;
    uint32_t discarded;
    int last_receive_result;
    ninlil_radio_adapt power[16];
    uint16_t power_peer[16];
    uint8_t adaptive_power; /* 0 fixed, 1 legacy, 2 confirmed feedback */
    ninlil_radio_feedback *feedback; /* borrowed; no default RAM allocation */
#ifdef CONFIG_NINLIL_RADIO_FEEDBACK_EXPERIMENTAL
    ninlil_radio_feedback feedback_workspace;
#endif
    uint64_t feedback_probe_token;
    uint32_t feedback_queue_us;
    int feedback_reply_result;
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
/* Before the first node step. Connect completed measurements to the existing
 * observation/route path. This call does not select a power policy.
 * Workspace remains caller-owned and borrowed until node_close. The default
 * pump has only a pointer; Kconfig allocates embedded workspace only on opt-in.
 */
int ninlil_esp_node_closed_probes(ninlil_esp_network_pump *p,
                                  ninlil_probe_monitor *workspace);
int ninlil_esp_network_emit(void *ctx, uint16_t next,
                            ninlil_traffic_class traffic, const uint8_t *frame,
                            size_t length);
/* <=4 receives, one bounded Core step, one Relay opportunity, <=1 physical TX.
 * The existing SX1262 driver retains CCA/pause/profile enforcement. */
int ninlil_esp_network_step(ninlil_esp_network_pump *p);
int ninlil_esp_node_adaptive_power(ninlil_esp_network_pump *p,
                                   int8_t minimum_dbm);
/* Attach an explicit, caller-owned workspace before the first staged frame.
 * Workspace must outlive node and pump. No switching from an already enabled
 * power owner. All users must rebuild the experimental config/pump ABI. */
int ninlil_esp_node_feedback_open(ninlil_esp_network_pump *pump,
                                  int8_t minimum_dbm,
                                  ninlil_radio_feedback *workspace);
#endif
