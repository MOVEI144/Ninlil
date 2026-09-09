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
    memset(s, 0, sizeof(*s));
    s->last_refill_us = now;
    s->budget_us = budget;
    s->pause_us = pause;
    return NINLIL_OK;
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
    empty->token = token;
    empty->peer = peer;
    empty->traffic = traffic;
    empty->airtime_us = airtime;
    empty->length = (uint16_t)length;
    memcpy(empty->frame, frame, length);
    empty->used = 1u;
    return NINLIL_OK;
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
