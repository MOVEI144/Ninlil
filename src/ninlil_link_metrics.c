#include "ninlil_link_metrics.h"
#include <string.h>
static int context_valid(const ninlil_link_context *c)
{
    uint8_t nonzero = 0u;
    if (!c || !c->generation || !c->profile || c->power_dbm < -9 ||
        c->power_dbm > 22)
        return 0;
    for (unsigned int i = 0u; i < sizeof(c->session); i++)
        nonzero |= c->session[i];
    return nonzero != 0u;
}
static int same_context(const ninlil_link_context *a, const ninlil_link_context *b)
{
    return a->generation == b->generation && a->profile == b->profile &&
           a->power_dbm == b->power_dbm &&
           !memcmp(a->session, b->session, sizeof(a->session));
}
void ninlil_link_metrics_open(ninlil_link_metrics *m)
{
    if (m)
        memset(m, 0, sizeof(*m));
}
int ninlil_link_metrics_invalidate(ninlil_link_metrics *m, uint64_t now)
{
    uint64_t sequence;
    if (!m || now < m->now_ms)
        return NINLIL_ERR_INVALID;
    sequence = m->sequence;
    memset(m, 0, sizeof(*m));
    m->sequence = sequence;
    m->now_ms = now;
    return NINLIL_OK;
}
static void reset_window(ninlil_link_metrics *m)
{
    m->attempts = m->delivered = 0u;
    m->airtime_sum_us = m->queue_max_us = 0u;
}
int ninlil_link_metrics_tick(ninlil_link_metrics *m, uint64_t now)
{
    if (!m || now < m->now_ms)
        return NINLIL_ERR_INVALID;
    m->now_ms = now;
    if (!m->pending || now < m->due_ms)
        return NINLIL_OK;
    /* Sparse trials do not make an arbitrarily old first sample fresh. */
    if (m->attempts && m->tx_ms - m->first_tx_ms > NINLIL_METRIC_MAX_AGE_MS)
        reset_window(m);
    if (!m->attempts) {
        m->first_tx_ms = m->tx_ms;
        m->first_sequence = m->sequence;
    }
    m->attempts++;
    m->delivered += m->replied;
    m->airtime_sum_us += m->pending_airtime_us;
    if (m->pending_queue_us > m->queue_max_us)
        m->queue_max_us = m->pending_queue_us;
    if (m->replied)
        m->consecutive_losses = 0u;
    else if (m->consecutive_losses < NINLIL_METRIC_WINDOW)
        m->consecutive_losses++;
    m->last_closed_ms = m->due_ms;
    m->previous_token = m->token;
    m->pending = 0u;
    if (m->attempts == NINLIL_METRIC_WINDOW) {
        m->window.context = m->context;
        m->window.first_sequence = m->first_sequence;
        m->window.last_sequence = m->sequence;
        m->window.first_tx_ms = m->first_tx_ms;
        m->window.closed_ms = m->due_ms;
        m->window.airtime_sum_us = m->airtime_sum_us;
        m->window.queue_max_us = m->queue_max_us;
        m->window.attempts = m->attempts;
        m->window.delivered = m->delivered;
        reset_window(m);
    }
    return NINLIL_OK;
}
int ninlil_link_metrics_tx(ninlil_link_metrics *m, const ninlil_link_context *c,
                           uint64_t token, uint64_t now, uint32_t airtime,
                           uint32_t queue)
{
    int rc;
    if (!m || !context_valid(c) || !token || !airtime || airtime > 400000u ||
        queue > 30000000u || now < m->now_ms ||
        now > UINT64_MAX - NINLIL_METRIC_RESPONSE_MS)
        return NINLIL_ERR_INVALID;
    if (m->pending && token == m->token && same_context(&m->context, c))
        return now == m->tx_ms && airtime == m->pending_airtime_us &&
                       queue == m->pending_queue_us ? NINLIL_OK : NINLIL_ERR_CONFLICT;
    if (token == m->previous_token && m->active && same_context(&m->context, c))
        return NINLIL_ERR_CONFLICT;
    if (m->sequence == UINT64_MAX)
        return NINLIL_ERR_CAPACITY;
    if (m->pending && now < m->due_ms)
        return NINLIL_ERR_BUSY;
    rc = ninlil_link_metrics_tick(m, now);
    if (rc != NINLIL_OK)
        return rc;
    if (!m->active || !same_context(&m->context, c)) {
        reset_window(m);
        memset(&m->window, 0, sizeof(m->window));
        m->context = *c;
        m->active = 1u;
        m->consecutive_losses = 0u;
        m->last_closed_ms = 0u;
    }
    m->sequence++;
    m->pending = 1u;
    m->replied = 0u;
    m->token = token;
    m->tx_ms = now;
    m->due_ms = now + NINLIL_METRIC_RESPONSE_MS;
    m->pending_airtime_us = airtime;
    m->pending_queue_us = queue;
    return NINLIL_OK;
}
int ninlil_link_metrics_reply(ninlil_link_metrics *m, uint64_t token, uint64_t now)
{
    if (!m || now < m->now_ms)
        return NINLIL_ERR_INVALID;
    if (!m->pending || !token || token != m->token)
        return NINLIL_ERR_NOT_FOUND;
    if (now >= m->due_ms)
        return NINLIL_ERR_EXPIRED;
    m->now_ms = now;
    m->replied = 1u;
    return NINLIL_OK;
}
int ninlil_link_metrics_read(const ninlil_link_metrics *m, uint64_t now,
                             ninlil_link_window *out)
{
    if (!m || !out || now < m->now_ms)
        return NINLIL_ERR_INVALID;
    if (m->window.attempts != NINLIL_METRIC_WINDOW ||
        now < m->window.closed_ms || now < m->window.first_tx_ms ||
        now - m->window.first_tx_ms > NINLIL_METRIC_MAX_AGE_MS ||
        !same_context(&m->window.context, &m->context))
        return NINLIL_ERR_EMPTY;
    *out = m->window;
    return NINLIL_OK;
}
