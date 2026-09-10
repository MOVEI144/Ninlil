#include "ninlil_probe_monitor.h"

#include <string.h>

static ninlil_probe_monitor_peer *find(ninlil_probe_monitor *m, uint16_t peer)
{
    for (unsigned int i = 0u; i < NINLIL_PROBE_MONITOR_PEERS; i++)
        if (m->peers[i].address == peer)
            return &m->peers[i];
    return NULL;
}

static int valid_peer(uint16_t peer, const uint8_t session[16])
{
    return peer && peer != UINT16_MAX && session &&
           memcmp(session, (uint8_t[16]){0}, 16u) != 0;
}

static int skip(ninlil_probe_monitor *m, int result)
{
    if (m->skipped_measurements != UINT32_MAX)
        m->skipped_measurements++;
    return result;
}

int ninlil_probe_monitor_open(ninlil_probe_monitor *m, uint32_t profile)
{
    if (!m || !profile)
        return NINLIL_ERR_INVALID;
    memset(m, 0, sizeof(*m));
    m->profile = profile;
    return NINLIL_OK;
}

static int close_trial(ninlil_probe_monitor_peer *p, uint64_t now)
{
    ninlil_link_metrics *m = &p->metrics;
    int completed = m->pending && now >= m->due_ms;
    ninlil_probe_sample sample = {m->tx_ms,
                                  m->token,
                                  m->sequence,
                                  m->pending_airtime_us,
                                  m->pending_queue_us,
                                  m->replied};
    int rc = ninlil_link_metrics_tick(m, now);
    if (rc != NINLIL_OK || !completed)
        return rc;
    p->samples[p->cursor] = sample;
    p->cursor = (uint8_t)((p->cursor + 1u) % NINLIL_METRIC_WINDOW);
    if (p->sample_count < NINLIL_METRIC_WINDOW)
        p->sample_count++;
    return NINLIL_OK;
}

static int route_window(ninlil_probe_monitor_peer *p, uint64_t now,
                        ninlil_link_window *w)
{
    ninlil_link_window result = {0};
    if (p->sample_count != NINLIL_METRIC_WINDOW)
        return NINLIL_ERR_EMPTY;
    result.context = p->metrics.context;
    for (unsigned int i = 0u; i < NINLIL_METRIC_WINDOW; i++) {
        const ninlil_probe_sample *sample =
            &p->samples[(p->cursor + i) % NINLIL_METRIC_WINDOW];
        if (sample->tx_ms > now ||
            now - sample->tx_ms > NINLIL_METRIC_MAX_AGE_MS)
            return NINLIL_ERR_EMPTY;
        if (!i) {
            result.first_sequence = sample->sequence;
            result.first_tx_ms = sample->tx_ms;
        }
        result.last_sequence = sample->sequence;
        result.last_token = sample->token;
        result.closed_ms = sample->tx_ms + NINLIL_METRIC_RESPONSE_MS;
        result.airtime_sum_us += sample->airtime_us;
        if (sample->queue_us > result.queue_max_us)
            result.queue_max_us = sample->queue_us;
        result.attempts++;
        result.delivered += sample->delivered;
        result.success_bits =
            (uint8_t)((result.success_bits << 1) | sample->delivered);
    }
    *w = result;
    return NINLIL_OK;
}

int ninlil_probe_monitor_tx(ninlil_probe_monitor *m, uint16_t peer,
                            const uint8_t session[16], int8_t power,
                            uint64_t token, uint64_t now, uint32_t airtime,
                            uint64_t queue)
{
    ninlil_probe_monitor_peer *p, next;
    ninlil_link_context context;
    int changed, rc;
    if (!m || !m->profile || !valid_peer(peer, session) || !token ||
        power < -9 || power > 22 || !airtime || airtime > 400000u ||
        now > UINT64_MAX - NINLIL_METRIC_RESPONSE_MS)
        return NINLIL_ERR_INVALID;
    p = find(m, peer);
    if (p && now < p->metrics.now_ms)
        return NINLIL_ERR_INVALID;
    if (queue > 30000000u) {
        if (p) {
            (void)ninlil_link_metrics_invalidate(&p->metrics, now);
            p->sample_count = p->cursor = 0u;
            memset(&p->report, 0, sizeof(p->report));
        }
        return skip(m, NINLIL_ERR_EXPIRED);
    }
    if (!p) {
        for (unsigned int i = 0u; i < NINLIL_PROBE_MONITOR_PEERS; i++) {
            ninlil_probe_monitor_peer *candidate = &m->peers[i];
            if (!candidate->address ||
                (now >= candidate->metrics.now_ms &&
                 now - candidate->metrics.now_ms > NINLIL_METRIC_MAX_AGE_MS)) {
                p = candidate;
                break;
            }
        }
    }
    if (!p)
        return skip(m, NINLIL_ERR_CAPACITY);
    next = *p;
    if (next.address != peer) {
        /* Eviction is permitted only for stale, non-authoritative telemetry. */
        memset(&next, 0, sizeof(next));
        next.address = peer;
    }
    changed = !next.metrics.active || next.metrics.context.power_dbm != power ||
              next.metrics.context.profile != m->profile ||
              memcmp(next.metrics.context.session, session, 16u) != 0;
    if (changed) {
        if (next.generation == UINT64_MAX)
            return skip(m, NINLIL_ERR_CAPACITY);
        rc = ninlil_link_metrics_invalidate(&next.metrics, now);
        if (rc != NINLIL_OK)
            return rc;
        next.generation++;
        next.acknowledged_sequence = 0u;
        next.sample_count = next.cursor = 0u;
        memset(&next.report, 0, sizeof(next.report));
    }
    rc = close_trial(&next, now);
    if (rc != NINLIL_OK)
        return rc;
    memset(&context, 0, sizeof(context));
    context.generation = next.generation;
    context.profile = m->profile;
    context.power_dbm = power;
    memcpy(context.session, session, 16u);
    rc = ninlil_link_metrics_tx(&next.metrics, &context, token, now, airtime,
                                (uint32_t)queue);
    if (rc == NINLIL_OK)
        *p = next;
    else
        (void)skip(m, rc);
    return rc;
}

int ninlil_probe_monitor_reply(ninlil_probe_monitor *m, uint16_t peer,
                               const uint8_t session[16], uint64_t token,
                               uint64_t now)
{
    ninlil_probe_monitor_peer *p;
    if (!m || !m->profile || !valid_peer(peer, session))
        return NINLIL_ERR_INVALID;
    p = find(m, peer);
    if (!p || !p->metrics.active ||
        memcmp(p->metrics.context.session, session, 16u) != 0)
        return NINLIL_ERR_STATE;
    return ninlil_link_metrics_reply(&p->metrics, token, now);
}

int ninlil_probe_monitor_read(ninlil_probe_monitor *m, uint16_t peer,
                              const uint8_t session[16], uint64_t now,
                              int pending_only, ninlil_link_window *out)
{
    ninlil_probe_monitor_peer *p;
    ninlil_link_window window;
    int rc;
    if (!m || !m->profile || !out || !valid_peer(peer, session) ||
        (pending_only != 0 && pending_only != 1))
        return NINLIL_ERR_INVALID;
    p = find(m, peer);
    if (!p || !p->metrics.active ||
        memcmp(p->metrics.context.session, session, 16u) != 0)
        return NINLIL_ERR_EMPTY;
    rc = close_trial(p, now);
    if (rc == NINLIL_OK)
        rc = route_window(p, now, &window);
    if (rc != NINLIL_OK)
        return rc;
    if (pending_only && p->acknowledged_sequence == window.last_sequence)
        return NINLIL_ERR_EMPTY;
    p->report = window;
    *out = window;
    return NINLIL_OK;
}

int ninlil_probe_monitor_ack(ninlil_probe_monitor *m, uint16_t peer,
                             uint64_t token, uint8_t bits)
{
    ninlil_probe_monitor_peer *p;
    if (!m || !m->profile || !peer || peer == UINT16_MAX || !token)
        return NINLIL_ERR_INVALID;
    p = find(m, peer);
    if (!p || !p->report.last_sequence || token != p->report.last_token ||
        bits != p->report.success_bits)
        return NINLIL_ERR_STATE;
    p->acknowledged_sequence = p->report.last_sequence;
    return NINLIL_OK;
}

int ninlil_probe_monitor_invalidate(ninlil_probe_monitor *m, uint16_t peer,
                                    uint64_t now)
{
    if (!m || !m->profile || peer == UINT16_MAX)
        return NINLIL_ERR_INVALID;
    for (unsigned int i = 0u; i < NINLIL_PROBE_MONITOR_PEERS; i++)
        if ((!peer || m->peers[i].address == peer) &&
            m->peers[i].metrics.now_ms > now)
            return NINLIL_ERR_INVALID;
    for (unsigned int i = 0u; i < NINLIL_PROBE_MONITOR_PEERS; i++) {
        if (peer && m->peers[i].address != peer)
            continue;
        (void)ninlil_link_metrics_invalidate(&m->peers[i].metrics, now);
        m->peers[i].acknowledged_sequence = 0u;
        m->peers[i].sample_count = m->peers[i].cursor = 0u;
        memset(&m->peers[i].report, 0, sizeof(m->peers[i].report));
    }
    return NINLIL_OK;
}

int ninlil_probe_monitor_pause(ninlil_probe_monitor *m, uint64_t now)
{
    return ninlil_probe_monitor_invalidate(m, 0u, now);
}
