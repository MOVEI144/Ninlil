#include "ninlil_radio_feedback.h"
#include <string.h>

static int find_peer(const ninlil_radio_feedback *o, uint16_t peer, int create)
{
    int empty = -1;
    for (unsigned int i = 0u; i < NINLIL_FEEDBACK_PEERS; i++) {
        if (o->peers[i].address == peer)
            return (int)i;
        if (!o->peers[i].address)
            empty = (int)i;
    }
    return create ? empty : -1;
}

static int reset_peer(ninlil_feedback_peer *p, uint64_t now)
{
    int rc = ninlil_link_metrics_invalidate(&p->metrics, now);
    if (rc == NINLIL_OK)
        p->confirmed = 0u;
    return rc;
}

int ninlil_radio_feedback_open(ninlil_radio_feedback *o,
                                int8_t minimum, int8_t maximum)
{
    if (!o || minimum < -9 || maximum > 22 || minimum > maximum ||
        (maximum - minimum) % 3 != 0)
        return NINLIL_ERR_INVALID;
    memset(o, 0, sizeof(*o));
    o->minimum_dbm = minimum;
    o->maximum_dbm = maximum;
    o->opened = 1u;
    return NINLIL_OK;
}

int ninlil_radio_feedback_begin(ninlil_radio_feedback *o, uint16_t peer,
                                 uint32_t profile, const uint8_t session[16],
                                 uint64_t now, ninlil_link_context *out)
{
    ninlil_link_window w;
    ninlil_feedback_peer *p;
    int slot, rc;
    if (!o || !out || !session || !peer || peer == UINT16_MAX || !profile ||
        !memcmp(session, (uint8_t[16]){0}, 16u))
        return NINLIL_ERR_INVALID;
    if (!o->opened || o->fault || now < o->now_ms)
        return NINLIL_ERR_STATE;
    if (o->pending)
        return NINLIL_ERR_BUSY;
    slot = find_peer(o, peer, 1);
    if (slot < 0)
        return NINLIL_ERR_CAPACITY;
    p = &o->peers[slot];
    if (p->confirmed && (p->policy.context.profile != profile ||
                         memcmp(p->policy.context.session, session, 16u))) {
        rc = reset_peer(p, now);
        if (rc != NINLIL_OK)
            return rc;
    }
    p->address = peer;
    o->now_ms = now;
    if (!p->confirmed) {
        if (p->generation == UINT64_MAX)
            return NINLIL_ERR_CAPACITY;
        memset(&o->proposed, 0, sizeof(o->proposed));
        o->proposed.context.generation = p->generation + 1u;
        o->proposed.context.profile = profile;
        o->proposed.context.power_dbm = o->maximum_dbm;
        memcpy(o->proposed.context.session, session, 16u);
        /* No policy open yet: maximum output is unconfirmed until TX_DONE. */
    } else {
        rc = ninlil_link_metrics_tick(&p->metrics, now);
        if (rc != NINLIL_OK)
            return rc;
        o->proposed = p->policy;
        if (!p->metrics.pending) {
            rc = ninlil_link_metrics_read(&p->metrics, now, &w);
            if (rc != NINLIL_OK && rc != NINLIL_ERR_EMPTY)
                return rc;
            rc = ninlil_power_policy_plan(&p->policy,
                                           rc == NINLIL_OK ? &w : NULL,
                                           now, &o->proposed);
            if (rc != NINLIL_OK)
                return rc;
        }
    }
    o->slot = (uint8_t)slot;
    o->began_ms = now;
    o->pending = 1u;
    *out = o->proposed.context;
    return NINLIL_OK;
}

int ninlil_radio_feedback_finish(ninlil_radio_feedback *o, int result,
                                  int8_t applied, uint64_t now, uint64_t token,
                                  uint32_t airtime, uint32_t queue)
{
    ninlil_feedback_peer *p;
    int changed, rc;
    if (!o || !o->opened || !o->pending || o->fault || now < o->now_ms ||
        now < o->began_ms)
        return NINLIL_ERR_STATE;
    if (result != NINLIL_OK && result != NINLIL_ERR_BUSY &&
        result != NINLIL_ERR_IO && result != NINLIL_ERR_TIMEOUT)
        return NINLIL_ERR_INVALID;
    if (result == NINLIL_OK && (!airtime || airtime > 400000u ||
                               now > UINT64_MAX - NINLIL_METRIC_RESPONSE_MS))
        return NINLIL_ERR_INVALID;
    o->pending = 0u;
    o->now_ms = now;
    if (result == NINLIL_ERR_BUSY)
        return NINLIL_OK;
    if (result != NINLIL_OK)
        return o->fault = result;
    if (applied != o->proposed.context.power_dbm)
        return o->fault = NINLIL_ERR_IO;
    p = &o->peers[o->slot];
    changed = !p->confirmed ||
              p->generation != o->proposed.context.generation;
    if (!p->confirmed) {
        rc = ninlil_power_policy_open(&p->policy, &o->proposed.context,
                                       o->minimum_dbm, o->maximum_dbm, now);
        if (rc != NINLIL_OK)
            return o->fault = rc;
    } else {
        p->policy = o->proposed;
        p->policy.now_ms = now;
        if (changed)
            p->policy.changed_ms = p->policy.supported_ms = now;
    }
    p->generation = p->policy.context.generation;
    p->confirmed = 1u;
    if (changed) {
        rc = ninlil_link_metrics_invalidate(&p->metrics, now);
        if (rc != NINLIL_OK)
            return o->fault = rc;
    }
    if (!token || queue > 30000000u)
        return NINLIL_OK;
    rc = ninlil_link_metrics_tx(&p->metrics, &p->policy.context, token, now,
                                 airtime, queue);
    /* No duplicate challenge is silently promoted to another independent trial. */
    return rc == NINLIL_OK ? rc : (o->fault = rc);
}

int ninlil_radio_feedback_reply(ninlil_radio_feedback *o, uint16_t peer,
                                 const uint8_t session[16], uint64_t token,
                                 uint64_t now)
{
    ninlil_feedback_peer *p;
    int slot, rc;
    if (!o || !o->opened || o->fault || !peer || !session ||
        now < o->now_ms)
        return NINLIL_ERR_STATE;
    slot = find_peer(o, peer, 0);
    if (slot < 0)
        return NINLIL_ERR_NOT_FOUND;
    p = &o->peers[slot];
    if (!p->confirmed || memcmp(p->policy.context.session, session, 16u))
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_link_metrics_reply(&p->metrics, token, now);
    if (rc == NINLIL_OK)
        o->now_ms = now;
    return rc;
}

int ninlil_radio_feedback_invalidate(ninlil_radio_feedback *o,
                                      uint16_t peer, uint64_t now)
{
    if (!o || !o->opened || o->fault || now < o->now_ms || peer == UINT16_MAX)
        return NINLIL_ERR_STATE;
    if (o->pending)
        return NINLIL_ERR_BUSY;
    for (unsigned int i = 0u; i < NINLIL_FEEDBACK_PEERS; i++) {
        int rc;
        if (!o->peers[i].address || (peer && o->peers[i].address != peer))
            continue;
        rc = reset_peer(&o->peers[i], now);
        if (rc != NINLIL_OK)
            return o->fault = rc;
    }
    o->now_ms = now;
    return NINLIL_OK;
}

int ninlil_radio_feedback_read(const ninlil_radio_feedback *o, uint16_t peer,
                                uint64_t now, ninlil_link_window *out)
{
    const ninlil_feedback_peer *p;
    ninlil_link_window window;
    int slot, rc;
    if (!o || !o->opened || o->fault || !out || !peer || now < o->now_ms)
        return NINLIL_ERR_STATE;
    slot = find_peer(o, peer, 0);
    if (slot < 0)
        return NINLIL_ERR_NOT_FOUND;
    p = &o->peers[slot];
    if (!p->confirmed)
        return NINLIL_ERR_EMPTY;
    rc = ninlil_link_metrics_read(&p->metrics, now, &window);
    if (rc != NINLIL_OK)
        return rc;
    if (window.context.generation != p->generation)
        return NINLIL_ERR_EMPTY;
    *out = window;
    return NINLIL_OK;
}
