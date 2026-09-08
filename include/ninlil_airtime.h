#ifndef NINLIL_AIRTIME_H
#define NINLIL_AIRTIME_H

#include "ninlil.h"

#define NINLIL_AIRTIME_QUEUE_MAX 32u
#define NINLIL_AIRTIME_FRAME_MAX 240u

typedef struct ninlil_airtime_job {
    uint64_t token;
    uint16_t peer;
    uint16_t length;
    uint32_t airtime_us;
    ninlil_traffic_class traffic;
    uint8_t frame[NINLIL_AIRTIME_FRAME_MAX];
    uint8_t used;
} ninlil_airtime_job;

typedef struct ninlil_airtime_scheduler {
    ninlil_airtime_job jobs[NINLIL_AIRTIME_QUEUE_MAX];
    uint64_t last_refill_us;
    uint64_t not_before_us;
    uint32_t credit_us;
    uint32_t credit_remainder;
    uint32_t budget_us;
    uint32_t pause_us;
    uint16_t cursor[4];
    uint8_t phase;
    uint8_t active;
    uint8_t busy;
    uint8_t waiting;
} ninlil_airtime_scheduler;

/* Explicit one-radio queue; all jobs are derived from owners above this layer.
 * Not a replacement for regional CCA/LBT enforcement in the physical driver.
 * Starts without credit after boot. Fixed one-second accounting window, no
 * packet preemption, 4/4 global and 2/2 per-peer CRITICAL/CONTROL reserves.
 *
 * A selected job retains its turn while credit accumulates (at most one
 *
 * second of refill). Smaller later jobs cannot consume that reservation. */
int ninlil_airtime_open(ninlil_airtime_scheduler *s, uint64_t now_us,
                        uint32_t budget_us_per_second, uint32_t pause_us);
int ninlil_airtime_enqueue(ninlil_airtime_scheduler *s, uint64_t token,
                           uint16_t peer, ninlil_traffic_class traffic,
                           uint32_t airtime_us, const uint8_t *frame,
                           size_t length);
int ninlil_airtime_next(ninlil_airtime_scheduler *s, uint64_t now_us,
                        const ninlil_airtime_job **job);
/* OK means real TX completion. BUSY/TIMEOUT/IO keep the job; no discard limit.
 * Ambiguous transmissions consume their reserved airtime conservatively. */
int ninlil_airtime_complete(ninlil_airtime_scheduler *s, int physical_result);

/* Only for a staged envelope whose session/authorization became obsolete.
 * The originating Core/Relay retains logical ownership. Not a retry limit. */
int ninlil_airtime_discard_stale(ninlil_airtime_scheduler *s);
#endif
