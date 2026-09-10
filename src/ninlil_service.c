#include "ninlil_internal.h"
#include <stdlib.h>
#include <string.h>

int ninlil_spool_limits_valid(const ninlil_config *c)
{
    const ninlil_spool_limits *s = &c->spool;
    if (!s->owned_outbound)
        return !s->service_slots && !s->total_owned && !s->memory_ceiling_bytes;
    return s->owned_outbound >= c->profile.max_outbound &&
           s->owned_outbound <= NINLIL_SPOOL_OWNED_MAX && s->service_slots &&
           s->service_slots <= NINLIL_SERVICE_SLOTS_MAX &&
           s->service_slots <= c->profile.max_outbound &&
           s->total_owned >= s->owned_outbound &&
           (uint32_t)s->total_owned <=
               (uint32_t)s->owned_outbound + c->profile.max_inbound &&
           s->memory_ceiling_bytes >= c->profile.dram_ceiling_bytes &&
           s->memory_ceiling_bytes <= UINT32_C(1048576);
}
size_t ninlil_service_memory(const ninlil_runtime *r)
{
    return r->config.spool.owned_outbound
               ? (size_t)r->outbound_capacity * sizeof(ninlil_service_wait) +
                     (size_t)r->config.spool.service_slots *
                         sizeof(ninlil_service_slot)
               : 0u;
}
int ninlil_service_open(ninlil_runtime *r)
{
    if (!r->config.spool.owned_outbound)
        return NINLIL_OK;
    r->service_slots =
        calloc(r->config.spool.service_slots, sizeof(*r->service_slots));
    r->service_wait = calloc(r->outbound_capacity, sizeof(*r->service_wait));
    return r->service_slots && r->service_wait ? NINLIL_OK
                                               : NINLIL_ERR_CAPACITY;
}
void ninlil_service_clear(ninlil_runtime *r, uint16_t index)
{
    if (!r->service_wait)
        return;
    memset(&r->service_wait[index], 0, sizeof(*r->service_wait));
    for (uint16_t i = 0u; i < r->config.spool.service_slots; i++)
        if (r->service_slots[i].index == index)
            r->service_slots[i].used = 0u;
}
static int ready(const ninlil_runtime *r, uint16_t index)
{
    const ninlil_outbound_entry *e = &r->outbound[index];
    const ninlil_service_wait *w = &r->service_wait[index];
    return e->used && w->state != NINLIL_SERVICE_PAUSED &&
           r->step_count >= w->next_step &&
           (!e->last_sent_step || (r->step_count >= e->last_sent_step &&
                                   r->step_count - e->last_sent_step >=
                                       r->config.retry_interval_steps));
}
static int valid_slot(const ninlil_runtime *r, const ninlil_service_slot *s)
{
    return s->used && s->index < r->outbound_capacity && ready(r, s->index) &&
           ninlil_id_equal(&s->message, &r->outbound[s->index].message_id);
}
void ninlil_service_fill(ninlil_runtime *r)
{
    uint16_t count = r->config.spool.service_slots;
    if (!count)
        return;
    for (uint16_t i = 0u; i < count; i++)
        if (!valid_slot(r, &r->service_slots[i]))
            r->service_slots[i].used = 0u;
    for (unsigned int scan = 0u;
         scan < NINLIL_SERVICE_SCAN_MAX && scan < r->outbound_capacity;
         scan++) {
        uint16_t index = r->service_cursor, empty = count;
        int found = 0;
        r->service_cursor = (uint16_t)((index + 1u) % r->outbound_capacity);
        if (!ready(r, index))
            continue;
        for (uint16_t i = 0u; i < count; i++) {
            const ninlil_service_slot *s = &r->service_slots[i];
            if (!s->used && empty == count)
                empty = i;
            if (s->used && s->index == index)
                found = 1;
        }
        if (found)
            continue;
        if (empty == count) {
            r->service_cursor = index; /* Do not skip the next cold owner. */
            break;
        }
        r->service_slots[empty].used = 1u;
        r->service_slots[empty].index = index;
        r->service_slots[empty].message = r->outbound[index].message_id;
    }
}
ninlil_outbound_entry *ninlil_service_select(ninlil_runtime *r)
{
    static const uint8_t classes[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                        1, 1, 1, 1, 2, 2, 2, 3};
    ninlil_service_fill(r);
    for (unsigned int scan = 0u; scan < 16u; scan++) {
        uint8_t cls = classes[r->service_class];
        r->service_class = (uint8_t)((r->service_class + 1u) % 16u);
        for (uint16_t i = 0u; i < r->config.spool.service_slots; i++) {
            uint16_t at = (uint16_t)((r->outbound_cursor[cls] + i) %
                                     r->config.spool.service_slots);
            ninlil_service_slot *s = &r->service_slots[at];
            if (valid_slot(r, s) &&
                r->outbound[s->index].traffic_class == cls) {
                r->outbound_cursor[cls] =
                    (uint16_t)((at + 1u) % r->config.spool.service_slots);
                s->used = 0u; /* Return service, never return ownership. */
                return &r->outbound[s->index];
            }
        }
    }
    return NULL;
}
void ninlil_service_result(ninlil_runtime *r, ninlil_outbound_entry *e,
                           int result)
{
    ninlil_service_wait *w;
    uint64_t delay;
    if (!r->service_wait)
        return;
    w = &r->service_wait[(size_t)(e - r->outbound)];
    if (w->state == NINLIL_SERVICE_PAUSED)
        return;
    w->result = result;
    w->state = result == NINLIL_OK ? NINLIL_SERVICE_WAIT_LINK
               : result == NINLIL_ERR_UNAUTHORIZED || result == NINLIL_ERR_STATE
                   ? NINLIL_SERVICE_WAIT_AUTH
               : result == NINLIL_ERR_NOT_FOUND ? NINLIL_SERVICE_WAIT_ROUTE
                                                : NINLIL_SERVICE_BACKOFF;
    delay = result == NINLIL_OK ? r->config.retry_interval_steps : 16u;
    w->next_step =
        r->step_count > UINT64_MAX - delay ? UINT64_MAX : r->step_count + delay;
}
static int find(ninlil_runtime *r, const ninlil_id *id, uint16_t *index)
{
    ninlil_outbound_entry *e;
    if (!r || !id || !index)
        return NINLIL_ERR_INVALID;
    if (r->fatal_error)
        return r->fatal_error;
    if (!r->service_wait)
        return NINLIL_ERR_STATE;
    e = ninlil_find_outbound(r, id);
    if (!e)
        return NINLIL_ERR_NOT_FOUND;
    *index = (uint16_t)(e - r->outbound);
    return NINLIL_OK;
}
int ninlil_service_query(ninlil_runtime *r, const ninlil_id *id,
                         ninlil_service_info *out)
{
    ninlil_service_info info = {0};
    uint16_t index;
    int rc = find(r, id, &index);
    if (!out)
        return NINLIL_ERR_INVALID;
    if (rc != NINLIL_OK)
        return rc;
    info.state = (ninlil_service_state)r->service_wait[index].state;
    info.next_step = r->service_wait[index].next_step;
    info.last_result = r->service_wait[index].result;
    info.owned_outbound = r->outbound_live;
    info.owned_capacity = r->outbound_capacity;
    info.service_capacity = r->config.spool.service_slots;
    for (uint16_t i = 0u; i < info.service_capacity; i++)
        info.servicing += (uint16_t)valid_slot(r, &r->service_slots[i]);
    *out = info;
    return NINLIL_OK;
}
int ninlil_service_pause(ninlil_runtime *r, const ninlil_id *id)
{
    uint16_t index;
    int rc = find(r, id, &index);
    if (rc == NINLIL_OK) {
        ninlil_service_clear(r, index);
        r->service_wait[index].state = NINLIL_SERVICE_PAUSED;
    }
    return rc;
}
int ninlil_service_resume(ninlil_runtime *r, const ninlil_id *id)
{
    uint16_t index;
    int rc = find(r, id, &index);
    if (rc == NINLIL_OK)
        ninlil_service_clear(r, index);
    return rc;
}
