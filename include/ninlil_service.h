#ifndef NINLIL_SERVICE_H
#define NINLIL_SERVICE_H
#include "ninlil.h"
#define NINLIL_SPOOL_OWNED_MAX 1024u
#define NINLIL_SERVICE_SLOTS_MAX 32u
#define NINLIL_SERVICE_SCAN_MAX 64u

typedef enum ninlil_service_state {
    NINLIL_SERVICE_ELIGIBLE = 0,
    NINLIL_SERVICE_WAIT_LINK = 1,
    NINLIL_SERVICE_BACKOFF = 2,
    NINLIL_SERVICE_WAIT_AUTH = 3,
    NINLIL_SERVICE_WAIT_ROUTE = 4,
    NINLIL_SERVICE_PAUSED = 5
} ninlil_service_state;
typedef struct ninlil_service_info {
    ninlil_service_state state;
    uint64_t next_step;
    int last_result;
    uint16_t owned_outbound, owned_capacity, servicing, service_capacity;
} ninlil_service_info;
/* Same Core execution owner. Service state is deliberately boot-local; it is
 * not a delivery outcome or a persistent user pause. Restart rebuilds the
 * service cache without changing IDs, attempted state, evidence or payload.
 * Query is only for an active message. Outputs unchanged on error. */
int ninlil_service_query(ninlil_runtime *runtime, const ninlil_id *message,
                         ninlil_service_info *info);
/* Pause/resume only scheduling, never storage or receipt processing. A caller
 * requiring a persistent operator hold must save/reapply it before driving
 * Core. Already staged DATA is blocked by transmit_check while paused;
 * receipts continue. Cold discovery scans at most 64 indexes per selection.
 * Resume does not bypass current binding, service, route or deadline
 * checks. */
int ninlil_service_pause(ninlil_runtime *runtime, const ninlil_id *message);
int ninlil_service_resume(ninlil_runtime *runtime, const ninlil_id *message);
#endif
