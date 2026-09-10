#ifndef NINLIL_LINK_METRICS_H
#define NINLIL_LINK_METRICS_H
#include "ninlil.h"
#define NINLIL_METRIC_WINDOW 8u
#define NINLIL_METRIC_RESPONSE_MS 3000u
#define NINLIL_METRIC_MAX_AGE_MS 120000u

/* Public fingerprints only. A different session, setting generation, profile,
 * or actual TX power starts a new measurement series, not a new delivery. */
typedef struct ninlil_link_context {
    uint64_t generation;
    uint32_t profile;
    int8_t power_dbm;
    uint8_t session[16];
} ninlil_link_context;

typedef struct ninlil_link_window {
    ninlil_link_context context;
    uint64_t first_sequence, last_sequence, last_token;
    uint64_t first_tx_ms, closed_ms;
    uint32_t airtime_sum_us, queue_max_us;
    uint8_t attempts, delivered, success_bits;
} ninlil_link_window;

typedef struct ninlil_link_metrics {
    ninlil_link_context context;
    ninlil_link_window window;
    uint64_t now_ms, sequence, token, tx_ms, due_ms, last_closed_ms;
    uint64_t first_tx_ms, first_sequence, previous_token;
    uint32_t pending_airtime_us, pending_queue_us;
    uint32_t airtime_sum_us, queue_max_us;
    uint8_t active, pending, replied, attempts, delivered, consecutive_losses;
    uint8_t success_bits;
} ninlil_link_metrics;

/* Bounded, boot-local, no allocation or persistence. One pending probe/peer.
 * All APIs use one explicit execution owner. TX is actual TX_DONE; enqueue
 * rejection, sensing deferral, sleep and radio errors never create a trial.
 * Replies enter only after the caller authenticates the current neighbor. */
void ninlil_link_metrics_open(ninlil_link_metrics *metrics);
/* Sleep/receiver-plan changes invalidate provisional measurements without
 * manufacturing an RF loss. Logical observation sequence is never rewound. */
int ninlil_link_metrics_invalidate(ninlil_link_metrics *metrics,
                                   uint64_t now_ms);
int ninlil_link_metrics_tx(ninlil_link_metrics *metrics,
                           const ninlil_link_context *context, uint64_t token,
                           uint64_t tx_done_ms, uint32_t airtime_us,
                           uint32_t queue_us);
/* Reply interval is [TX_DONE, TX_DONE+3000ms); tick finalizes at its endpoint.
 * A duplicate reply is idempotent; a wrong/late reply is not counted. */
int ninlil_link_metrics_reply(ninlil_link_metrics *metrics, uint64_t token,
                              uint64_t now_ms);
int ninlil_link_metrics_tick(ninlil_link_metrics *metrics, uint64_t now_ms);
/* Only complete, non-overlapping eight-trial windows, with every sample fresh.
 * EMPTY does not mean zero loss or zero cost. Output unchanged on error. */
int ninlil_link_metrics_read(const ninlil_link_metrics *metrics,
                             uint64_t now_ms, ninlil_link_window *window);
#endif
