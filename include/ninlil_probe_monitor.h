#ifndef NINLIL_PROBE_MONITOR_H
#define NINLIL_PROBE_MONITOR_H

#include "ninlil_link_metrics.h"

#define NINLIL_PROBE_MONITOR_PEERS 16u

typedef struct ninlil_probe_sample {
    uint64_t tx_ms, token, sequence;
    uint32_t airtime_us, queue_us;
    uint8_t delivered;
} ninlil_probe_sample;

typedef struct ninlil_probe_monitor_peer {
    ninlil_link_metrics metrics;
    uint64_t generation;
    uint64_t acknowledged_sequence;
    uint16_t address;
    ninlil_probe_sample samples[NINLIL_METRIC_WINDOW];
    ninlil_link_window report;
    uint8_t sample_count, cursor;
} ninlil_probe_monitor_peer;

typedef struct ninlil_probe_monitor {
    ninlil_probe_monitor_peer peers[NINLIL_PROBE_MONITOR_PEERS];
    uint32_t profile;
    uint32_t skipped_measurements;
} ninlil_probe_monitor;

/* Borrowed by the node until close. One explicit execution owner, no allocation,
 * persistence, timers or authority. Each entry is telemetry, not custody.
 * A profile change requires pause/reopen before more RF; this first integration
 * keeps the receive PHY fixed and buckets actual changes in legacy TX power.
 * Open does not start a radio. Never reopen a live borrowed monitor. */
int ninlil_probe_monitor_open(ninlil_probe_monitor *monitor, uint32_t profile);
/* Actual completed, current-session neighbor probe only. queue_us is residence
 * from first queue admission to the final driver call, including earlier BUSY
 * deferrals. Unknown or >30-second residence is excluded, never clamped to zero.
 * The caller counts incomplete driver results separately, not as RF trials. */
int ninlil_probe_monitor_tx(ninlil_probe_monitor *monitor, uint16_t peer,
                             const uint8_t session[16], int8_t applied_power_dbm,
                             uint64_t token, uint64_t tx_done_ms,
                             uint32_t airtime_us, uint64_t queue_us);
/* Call only after authenticating a current neighbor reply. */
int ninlil_probe_monitor_reply(ninlil_probe_monitor *monitor, uint16_t peer,
                                const uint8_t session[16], uint64_t token,
                                uint64_t now_ms);
/* Finalizes expired response windows, then returns the latest eight CLOSED trials.
 * Routing windows overlap; each trial enters once, but a new finalized trial
 * refreshes routing without waiting another eight-probe interval. This is NOT
 * an independent power-policy window; those remain in link_metrics.window.
 * pending_only excludes a window already acknowledged by the authority.
 * Error leaves output unchanged. No success is inferred from missing samples. */
int ninlil_probe_monitor_read(ninlil_probe_monitor *monitor, uint16_t peer,
                               const uint8_t session[16], uint64_t now_ms,
                               int pending_only, ninlil_link_window *window);
int ninlil_probe_monitor_ack(ninlil_probe_monitor *monitor, uint16_t peer,
                              uint64_t token, uint8_t success_bits);
/* Sleep invalidates provisional and closed observations, not logical deliveries.
 * Sequence and generation high-water marks survive this boot-local pause. */
int ninlil_probe_monitor_pause(ninlil_probe_monitor *monitor, uint64_t now_ms);
#endif
