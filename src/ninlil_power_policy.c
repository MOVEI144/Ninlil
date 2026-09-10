#include "ninlil_power_policy.h"
#include <string.h>
static int context_equal(const ninlil_link_context *a,
                         const ninlil_link_context *b)
{
    return a->generation == b->generation && a->profile == b->profile &&
           a->power_dbm == b->power_dbm && !memcmp(a->session, b->session, 16u);
}
static int same(const ninlil_link_window *a, const ninlil_link_window *b)
{
    return context_equal(&a->context, &b->context) &&
           a->first_sequence == b->first_sequence &&
           a->last_sequence == b->last_sequence &&
           a->first_tx_ms == b->first_tx_ms && a->closed_ms == b->closed_ms &&
           a->airtime_sum_us == b->airtime_sum_us &&
           a->queue_max_us == b->queue_max_us && a->attempts == b->attempts &&
           a->delivered == b->delivered && a->last_token == b->last_token &&
           a->success_bits == b->success_bits;
}
int ninlil_power_policy_open(ninlil_power_policy *p,
                             const ninlil_link_context *c, int8_t minimum,
                             int8_t maximum, uint64_t now)
{
    if (!p || !c || !c->generation || !c->profile || minimum < -9 ||
        maximum > 22 || minimum > maximum || (maximum - minimum) % 3 ||
        c->power_dbm != maximum || !memcmp(c->session, (uint8_t[16]){0}, 16u))
        return NINLIL_ERR_INVALID;
    memset(p, 0, sizeof(*p));
    p->context = *c;
    p->minimum_dbm = minimum;
    p->maximum_dbm = maximum;
    p->now_ms = p->changed_ms = p->supported_ms = now;
    p->opened = 1u;
    return NINLIL_OK;
}
int ninlil_power_policy_plan(const ninlil_power_policy *p,
                             const ninlil_link_window *w, uint64_t now,
                             ninlil_power_policy *out)
{
    ninlil_power_policy next;
    int8_t power;
    if (!p || !out || out == p || !p->opened || p->poisoned || now < p->now_ms)
        return NINLIL_ERR_STATE;
    next = *p;
    next.now_ms = now;
    power = p->context.power_dbm;
    if (w) {
        if (!context_equal(&w->context, &p->context) ||
            w->attempts != NINLIL_METRIC_WINDOW || w->delivered > w->attempts ||
            !w->first_sequence || w->last_sequence < w->first_sequence ||
            w->last_sequence - w->first_sequence != NINLIL_METRIC_WINDOW - 1u ||
            w->first_tx_ms < p->changed_ms || w->closed_ms > now ||
            w->first_tx_ms > w->closed_ms ||
            w->closed_ms - w->first_tx_ms <
                NINLIL_METRIC_WINDOW * NINLIL_METRIC_RESPONSE_MS ||
            !w->airtime_sum_us ||
            w->airtime_sum_us > NINLIL_METRIC_WINDOW * 400000u ||
            w->queue_max_us > 30000000u ||
            now - w->first_tx_ms > NINLIL_METRIC_MAX_AGE_MS)
            return NINLIL_ERR_CONFLICT;
        if (p->last.last_sequence &&
            w->first_sequence <= p->last.last_sequence) {
            if (!same(w, &p->last))
                return NINLIL_ERR_CONFLICT;
        } else {
            if (w->first_tx_ms < p->last.closed_ms)
                return NINLIL_ERR_CONFLICT;
            next.last = *w;
            next.supported_ms = w->closed_ms;
            if (w->delivered <= 6u) {
                power = p->maximum_dbm;
                next.good = 0u;
            } else if (w->delivered == 8u) {
                if (next.good < 3u)
                    next.good++;
                if (next.good == 3u && now - p->changed_ms >= 30000u &&
                    power > p->minimum_dbm)
                    power = (int8_t)(power - 3);
            } else
                next.good = 0u;
        }
    }
    if (now - next.supported_ms >= NINLIL_METRIC_MAX_AGE_MS) {
        power = p->maximum_dbm;
        next.good = 0u;
    }
    if (power != p->context.power_dbm) {
        if (p->context.generation == UINT64_MAX)
            return NINLIL_ERR_CAPACITY;
        next.context.generation++;
        next.context.power_dbm = power;
        next.changed_ms = next.supported_ms = now;
        next.good = 0u;
        memset(&next.last, 0, sizeof(next.last));
    }
    *out = next;
    return NINLIL_OK;
}

int ninlil_power_policy_step(ninlil_power_policy *p,
                             const ninlil_link_window *w, uint64_t now,
                             ninlil_power_apply apply, void *ctx)
{
    ninlil_power_policy next;
    int rc;
    if (!apply)
        return NINLIL_ERR_STATE;
    rc = ninlil_power_policy_plan(p, w, now, &next);
    if (rc != NINLIL_OK)
        return rc;
    if (next.context.power_dbm != p->context.power_dbm) {
        rc = apply(ctx, next.context.power_dbm);
        if (rc != NINLIL_OK) {
            p->poisoned = 1u;
            return rc;
        }
    }
    *p = next;
    return NINLIL_OK;
}
