#include "ninlil_airtime.h"

#include <string.h>

_Static_assert(NINLIL_TRAFFIC_CRITICAL == 0 && NINLIL_TRAFFIC_BULK == 3,
               "scheduler profile requires the four version-2 traffic classes");

static const uint8_t schedule[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                     1, 1, 1, 1, 2, 2, 2, 3};

int ninlil_airtime_open(ninlil_airtime_scheduler *s, uint64_t now,
                        uint32_t budget, uint32_t pause)
{
    if (!s || budget == 0u || budget > 1000000u || pause > 1000000u)
        return NINLIL_ERR_INVALID;
#ifdef NINLIL_AIRTIME_DEFAULT_DRR
    if (budget < 1000u)
        return NINLIL_ERR_INVALID;
#endif
    memset(s, 0, sizeof(*s));
    s->last_refill_us = now;
    s->budget_us = budget;
    s->pause_us = pause;
#ifdef NINLIL_AIRTIME_DEFAULT_DRR
    return ninlil_airtime_enable_drr(s, budget < 10000u ? budget : 10000u,
                                     budget < 50000u ? budget : 50000u);
#else
    return NINLIL_OK;
#endif
}

int ninlil_airtime_enable_drr(ninlil_airtime_scheduler *s, uint32_t quantum,
                             uint32_t bypass)
{
    if (!s || !s->budget_us || quantum < 1000u || quantum > 50000u ||
        quantum > s->budget_us || bypass > s->budget_us || bypass > 400000u)
        return NINLIL_ERR_INVALID;
    if (s->busy || s->waiting || s->drr_enabled)
        return NINLIL_ERR_STATE;
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
        if (s->jobs[i].used)
            return NINLIL_ERR_STATE;
    s->quantum_us = quantum;
    s->bypass_limit_us = bypass;
    s->drr_enabled = s->drr_enter = 1u;
    return NINLIL_OK;
}

static int peer_queued(const ninlil_airtime_scheduler *s, uint16_t peer,
                       int cls)
{
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
        if (s->jobs[i].used && s->jobs[i].peer == peer &&
            (cls < 0 || (int)s->jobs[i].traffic == cls))
            return 1;
    return 0;
}

static unsigned int peer_slot(const ninlil_airtime_scheduler *s, uint16_t peer)
{
    unsigned int empty = NINLIL_AIRTIME_QUEUE_MAX;
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++) {
        if (s->peer_ids[i] == peer)
            return i;
        if (!peer_queued(s, s->peer_ids[i], -1))
            empty = i;
    }
    return empty;
}

static void register_peer(ninlil_airtime_scheduler *s, unsigned int slot,
                           uint16_t peer)
{
    int existing = s->peer_ids[slot] == peer;
    /* New and returning idle peers join the current service frontier. Idle
     * time cannot accumulate a catch-up entitlement over continuously queued peers. */
    for (unsigned int cls = 0u; cls < 4u; cls++) {
        uint64_t floor = UINT64_MAX;
        if (existing && peer_queued(s, peer, (int)cls))
            continue;
        for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
            if (s->peer_ids[i] &&
                peer_queued(s, s->peer_ids[i], (int)cls) &&
                s->peer_service_us[i][cls] < floor)
                floor = s->peer_service_us[i][cls];
        if (floor != UINT64_MAX && (!existing || s->peer_service_us[slot][cls] < floor))
            s->peer_service_us[slot][cls] = floor;
        else if (!existing)
            s->peer_service_us[slot][cls] = 0u;
    }
    s->peer_ids[slot] = peer;
}

static unsigned int reserve(ninlil_traffic_class traffic,
                            const unsigned int counts[4], unsigned int each)
{
    unsigned int remaining = 0u;
    if (traffic != NINLIL_TRAFFIC_CRITICAL && counts[0] < each)
        remaining += each - counts[0];
    if (traffic != NINLIL_TRAFFIC_CONTROL && counts[1] < each)
        remaining += each - counts[1];
    return remaining;
}

int ninlil_airtime_enqueue(ninlil_airtime_scheduler *s, uint64_t token,
                           uint16_t peer, ninlil_traffic_class traffic,
                           uint32_t airtime, const uint8_t *frame,
                           size_t length)
{
    unsigned int counts[4] = {0}, per_peer[4] = {0}, total = 0u,
                 peer_total = 0u, i;
    ninlil_airtime_job *empty = NULL;
    unsigned int slot = 0u;
    if (!s || !s->budget_us || !token || peer == 0u || peer == UINT16_MAX ||
        !frame || !length || length > NINLIL_AIRTIME_FRAME_MAX ||
        traffic < NINLIL_TRAFFIC_CRITICAL || traffic > NINLIL_TRAFFIC_BULK ||
        !airtime || airtime > 400000u || airtime > s->budget_us)
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++) {
        ninlil_airtime_job *j = &s->jobs[i];
        if (!j->used) {
            empty = j;
            continue;
        }
        if (j->token == token)
            return j->peer == peer && j->traffic == traffic &&
                           j->airtime_us == airtime && j->length == length &&
                           memcmp(j->frame, frame, length) == 0
                       ? NINLIL_OK
                       : NINLIL_ERR_CONFLICT;
        counts[(unsigned int)j->traffic]++;
        total++;
        if (j->peer == peer) {
            per_peer[(unsigned int)j->traffic]++;
            peer_total++;
        }
    }
    if (!empty ||
        NINLIL_AIRTIME_QUEUE_MAX - total <= reserve(traffic, counts, 4u) ||
        peer_total >= 8u || 8u - peer_total <= reserve(traffic, per_peer, 2u))
        return NINLIL_ERR_CAPACITY;
    if (s->drr_enabled) {
        slot = peer_slot(s, peer);
        if (slot == NINLIL_AIRTIME_QUEUE_MAX || s->next_sequence == UINT64_MAX)
            return NINLIL_ERR_CAPACITY;
        register_peer(s, slot, peer);
        s->queued_sequence[(size_t)(empty - s->jobs)] = ++s->next_sequence;
    }
    empty->token = token;
    empty->peer = peer;
    empty->traffic = traffic;
    empty->airtime_us = airtime;
    empty->length = (uint16_t)length;
    memcpy(empty->frame, frame, length);
    empty->used = 1u;
    return NINLIL_OK;
}

int ninlil_airtime_enqueue_at(ninlil_airtime_scheduler *s, uint64_t token,
                              uint16_t peer, ninlil_traffic_class traffic,
                              uint32_t airtime, const uint8_t *frame,
                              size_t length, uint64_t now)
{
    int rc, existing = 0;
    if (!s || now < s->last_refill_us)
        return NINLIL_ERR_INVALID;
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
        if (s->jobs[i].used && s->jobs[i].token == token)
            existing = 1;
    rc = ninlil_airtime_enqueue(s, token, peer, traffic, airtime, frame, length);
    if (rc != NINLIL_OK || existing)
        return rc;
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++) {
        ninlil_airtime_job *job = &s->jobs[i];
        if (job->used && job->token == token) {
            job->queued_at_us = now;
            job->queued_time_known = 1u;
            return NINLIL_OK;
        }
    }
    return NINLIL_ERR_FAULT;
}

static int take_selected(ninlil_airtime_scheduler *s, uint64_t now,
                         const ninlil_airtime_job **out)
{
    ninlil_airtime_job *job = &s->jobs[s->active];
    if (job->airtime_us > s->credit_us)
        return NINLIL_ERR_EMPTY;
    if (now > UINT64_MAX - job->airtime_us - s->pause_us)
        return NINLIL_ERR_STATE;
    s->credit_us -= job->airtime_us;
    s->not_before_us = now + job->airtime_us + s->pause_us;
    s->waiting = 0u;
    s->busy = 1u;
    *out = job;
    return NINLIL_OK;
}

static unsigned int known_peer(const ninlil_airtime_scheduler *s, uint16_t peer)
{
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
        if (s->peer_ids[i] == peer)
            return i;
    return NINLIL_AIRTIME_QUEUE_MAX;
}

static int drr_candidate(const ninlil_airtime_scheduler *s, unsigned int cls)
{
    int best = -1;
    uint64_t finish = UINT64_MAX;
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++) {
        const ninlil_airtime_job *j = &s->jobs[i];
        unsigned int at;
        uint64_t value;
        int earlier = 0;
        if (!j->used || (unsigned int)j->traffic != cls)
            continue;
        for (unsigned int k = 0u; k < NINLIL_AIRTIME_QUEUE_MAX; k++)
            if (s->jobs[k].used && s->jobs[k].peer == j->peer &&
                s->jobs[k].traffic == j->traffic &&
                s->queued_sequence[k] < s->queued_sequence[i])
                earlier = 1;
        if (earlier)
            continue;
        at = known_peer(s, j->peer);
        if (at == NINLIL_AIRTIME_QUEUE_MAX)
            return -1;
        value = s->peer_service_us[at][cls];
        /* Saturation makes the job last; take rejects actual overflow. */
        value = value > UINT64_MAX - j->airtime_us
                    ? UINT64_MAX : value + j->airtime_us;
        if (best < 0 || value < finish ||
            (value == finish && s->queued_sequence[i] <
                                   s->queued_sequence[(unsigned int)best])) {
            best = (int)i;
            finish = value;
        }
    }
    return best;
}

static int drr_take(ninlil_airtime_scheduler *s, unsigned int index,
                    uint64_t now, int bypass, const ninlil_airtime_job **out)
{
    const ninlil_airtime_job *j = &s->jobs[index];
    unsigned int cls = (unsigned int)j->traffic;
    unsigned int peer = known_peer(s, j->peer);
    if (peer == NINLIL_AIRTIME_QUEUE_MAX || !j->used ||
        s->peer_service_us[peer][cls] > UINT64_MAX - j->airtime_us ||
        now > UINT64_MAX - j->airtime_us - s->pause_us)
        return NINLIL_ERR_STATE;
    if (j->airtime_us > s->credit_us ||
        (bypass && j->airtime_us > s->bypass_left_us))
        return NINLIL_ERR_EMPTY;
    s->credit_us -= j->airtime_us;
    s->deficit_us[cls] -= (int64_t)j->airtime_us;
    s->peer_service_us[peer][cls] += j->airtime_us;
    s->not_before_us = now + j->airtime_us + s->pause_us;
    s->active = (uint8_t)index;
    s->busy = 1u;
    if (bypass)
        s->bypass_left_us -= j->airtime_us;
    else
        s->waiting = 0u;
    *out = j;
    return NINLIL_OK;
}

static int drr_wait(ninlil_airtime_scheduler *s, uint64_t now,
                    const ninlil_airtime_job **out)
{
    const ninlil_airtime_job *reserved = &s->jobs[s->reserved];
    int urgent;
    if (!reserved->used)
        return NINLIL_ERR_STATE;
    if (reserved->airtime_us <= s->credit_us)
        return drr_take(s, s->reserved, now, 0, out);
    urgent = drr_candidate(s, (unsigned int)NINLIL_TRAFFIC_CRITICAL);
    if (reserved->traffic != NINLIL_TRAFFIC_CRITICAL && urgent >= 0 &&
        s->deficit_us[0] - (int64_t)s->jobs[(unsigned int)urgent].airtime_us >=
            -(int64_t)s->bypass_limit_us)
        return drr_take(s, (unsigned int)urgent, now, 1, out);
    return NINLIL_ERR_EMPTY;
}

static int drr_next(ninlil_airtime_scheduler *s, uint64_t now,
                    const ninlil_airtime_job **out)
{
    static const uint8_t weights[4] = {8u, 4u, 3u, 1u};
    if (s->waiting)
        return drr_wait(s, now, out);
    /* Bounded CPU work. A large frame may accumulate quanta over calls. */
    for (unsigned int work = 0u; work < 64u; work++) {
        unsigned int cls = s->drr_class;
        int at = drr_candidate(s, cls);
        if (at < 0)
            s->deficit_us[cls] = 0;
        else {
            if (s->drr_enter)
                s->deficit_us[cls] += (int64_t)s->quantum_us * weights[cls];
            s->drr_enter = 0u;
            if (s->deficit_us[cls] >= (int64_t)s->jobs[(unsigned int)at].airtime_us) {
                s->reserved = (uint8_t)at;
                s->waiting = 1u;
                s->bypass_left_us = s->bypass_limit_us;
                return drr_wait(s, now, out);
            }
        }
        s->drr_class = (uint8_t)((cls + 1u) % 4u);
        s->drr_enter = 1u;
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_airtime_next(ninlil_airtime_scheduler *s, uint64_t now,
                        const ninlil_airtime_job **out)
{
    uint64_t elapsed, credit;
    unsigned int phase, scanned;
    if (!s || !out || !s->budget_us || now < s->last_refill_us)
        return NINLIL_ERR_STATE;
    if (s->busy || now < s->not_before_us)
        return NINLIL_ERR_BUSY;
    elapsed = now - s->last_refill_us;
    if (elapsed >= 1000000u) {
        credit = s->budget_us;
        s->credit_remainder = 0u;
    } else {
        uint64_t numerator = elapsed * s->budget_us + s->credit_remainder;
        credit = numerator / 1000000u;
        s->credit_remainder = (uint32_t)(numerator % 1000000u);
    }
    credit += s->credit_us;
    s->credit_us = (uint32_t)(credit > s->budget_us ? s->budget_us : credit);
    if (credit >= s->budget_us)
        s->credit_remainder = 0u;
    s->last_refill_us = now;
    if (s->drr_enabled)
        return drr_next(s, now, out);
    if (s->waiting)
        return take_selected(s, now, out);
    for (phase = 0u; phase < 16u; phase++) {
        uint8_t cls = schedule[s->phase];
        s->phase = (uint8_t)((s->phase + 1u) % 16u);
        for (scanned = 0u; scanned < NINLIL_AIRTIME_QUEUE_MAX; scanned++) {
            uint16_t index = s->cursor[cls];
            ninlil_airtime_job *j = &s->jobs[index];
            s->cursor[cls] =
                (uint16_t)((index + 1u) % NINLIL_AIRTIME_QUEUE_MAX);
            if (!j->used || (unsigned int)j->traffic != cls)
                continue;
            s->active = (uint8_t)index;
            s->waiting = 1u;
            return take_selected(s, now, out);
        }
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_airtime_complete(ninlil_airtime_scheduler *s, int result)
{
    if (!s || !s->busy || !s->jobs[s->active].used)
        return NINLIL_ERR_STATE;
    if (result != NINLIL_OK && result != NINLIL_ERR_BUSY &&
        result != NINLIL_ERR_IO && result != NINLIL_ERR_TIMEOUT)
        return NINLIL_ERR_INVALID;
    if (result == NINLIL_OK)
        memset(&s->jobs[s->active], 0, sizeof(s->jobs[s->active]));
    s->busy = 0u;
    return NINLIL_OK;
}

int ninlil_airtime_discard_stale(ninlil_airtime_scheduler *s)
{
    if (!s || !s->busy || !s->jobs[s->active].used)
        return NINLIL_ERR_STATE;
    memset(&s->jobs[s->active], 0, sizeof(s->jobs[s->active]));
    s->busy = 0u;
    return NINLIL_OK;
}
