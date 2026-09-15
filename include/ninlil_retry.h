#ifndef NINLIL_RETRY_H
#define NINLIL_RETRY_H
#include "ninlil.h"

/* Boot-local timing only. Never a replacement for restart-safe deadline time.
 * A staging watchdog reoffers the same durable message; it is not a failure
 * outcome and does not release ownership. All values are milliseconds. */
typedef struct ninlil_retry_policy {
    uint32_t initial_rto_ms;
    uint32_t blocked_backoff_ms;
    uint32_t staging_timeout_ms;
} ninlil_retry_policy;

typedef struct ninlil_retry_info {
    uint64_t next_due_ms;
    uint32_t rto_ms;
    uint8_t waiting_for_tx;
} ninlil_retry_info;
/* Active-message scheduling only; outputs unchanged on error. */
int ninlil_retry_query(ninlil_runtime *runtime, const ninlil_id *message,
                       ninlil_retry_info *info);

/* Enable once, before the first Core step (including after journal replay).
 * Existing legacy step clients remain unchanged. Policy values: RTO/backoff
 * 1..60000, staging 1..120000. The caller owns one execution context. */
int ninlil_retry_enable(ninlil_runtime *runtime,
                        const ninlil_retry_policy *policy, uint64_t now_ms);
/* All subsequent Core steps must provide nondecreasing monotonic time.
 * Repeated calls at the same time cannot accelerate retries. Delayed calls
 * do not require replaying ticks. Rejected timestamps leave time unchanged. */
int ninlil_step_at(ninlil_runtime *runtime, uint64_t now_ms);
/* Set only this logical message's RTO from its validated route. The value
 * takes effect at the next TX completion, not for unrelated peers. */
int ninlil_retry_set_message(ninlil_runtime *runtime, const ninlil_id *message,
                             uint32_t rto_ms);
/* Link owner reports CURRENT staged DATA once, after send() returned OK and
 * physical TX_DONE. This is never remote delivery evidence. The synchronous
 * reference pump reports completion before a later offer can occur. Async
 * ports must correlate the current offer before calling this boundary. */
int ninlil_retry_tx_done(ninlil_runtime *runtime, const ninlil_id *message,
                         uint64_t now_ms);
#endif
