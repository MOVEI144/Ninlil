#ifndef NINLIL_CONTROL_LOG_H
#define NINLIL_CONTROL_LOG_H

#include "ninlil_join.h"
#include "ninlil_relay.h"

#define NINLIL_CONTROL_LOG_MAX (UINT64_C(1024) * 1024u)
typedef struct ninlil_control_log ninlil_control_log;

typedef struct ninlil_control_replay {
    int (*join)(void *ctx, const ninlil_join_record *record);
    int (*plan)(void *ctx, const ninlil_network_plan *record);
    int (*relay)(void *ctx, const ninlil_relay_record *record);
    void *ctx;
} ninlil_control_replay;

/* Separate from the delivery journal. Uses the existing POSIX or raw-Flash
 * journal port selected at link time. Bounded append-only storage backpressures
 * when full; it never evicts owned records or implicitly resets authorization.
 */
int ninlil_control_log_open(ninlil_control_log **log, const char *location,
                            uint64_t maximum_bytes,
                            ninlil_control_replay replay);
void ninlil_control_log_close(ninlil_control_log *log);
int ninlil_control_log_bind(ninlil_control_log *log, const uint8_t identity[32],
                            int initialize);
int ninlil_control_log_join(void *ctx, const ninlil_join_record *record);
int ninlil_control_log_plan(void *ctx, const ninlil_network_plan *record);
int ninlil_control_log_relay(void *ctx, const ninlil_relay_record *record);
/* Revalidate the committed envelope/checksum before exposing an owned packet.
 */
int ninlil_control_log_verify_relay(void *ctx,
                                    const ninlil_relay_record *record);

#endif
