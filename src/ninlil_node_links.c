#include "ninlil_node_internal.h"
#include <string.h>

static int allowed(ninlil_node *n, int index)
{
    return index >= 0 && n->joined && n->peers[index].member_active &&
           !n->peers[index].revoked && !n->peers[n->local_index].revoked;
}
static int send_neighbor(ninlil_node *n, unsigned int index,
                         const uint8_t *plain, size_t length)
{
    uint8_t frame[NINLIL_SECURE_FRAME_MAX];
    size_t size = 0u;
    int rc = ninlil_secure_seal_neighbor(&n->peers[index].sessions[1], plain,
                                         length, frame, sizeof(frame), &size);
    return rc == NINLIL_OK
               ? n->config.emit(n->config.emit_ctx,
                                n->members[index].grant.node,
                                plain[0] == NODE_PROBE ? NINLIL_TRAFFIC_NORMAL
                                                       : NINLIL_TRAFFIC_CONTROL,
                                frame, size)
               : rc;
}
static int inspect(ninlil_node *n, const uint8_t *frame, size_t length,
                   uint8_t *plain, size_t *size)
{
    int index;
    if (length <= NINLIL_SECURE_OVERHEAD || frame[31] != 2u)
        return NINLIL_ERR_INVALID;
    index = ninlil_node_index(n, (uint16_t)ninlil_node_get(frame + 6, 2u));
    if (!allowed(n, index))
        return NINLIL_ERR_UNAUTHORIZED;
    if (ninlil_secure_inspect_tx(&n->peers[index].sessions[1], frame, length,
                                 plain, NINLIL_SECURE_PLAINTEXT_MAX,
                                 size) != NINLIL_OK)
        return NINLIL_ERR_STATE;
    return index;
}
int ninlil_node_probe_current(ninlil_node *n, const uint8_t *frame,
                              size_t length)
{
    uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
    size_t size = 0u;
    int index = inspect(n, frame, length, plain, &size);
    if (index < 0)
        return index;
    if (size == 9u && plain[0] == NODE_PROBE_REPLY)
        return NINLIL_OK;
    return size == sizeof(plain) && plain[0] == NODE_PROBE &&
                   ninlil_node_get(plain + 1, 8u) == n->peers[index].probe_token
               ? NINLIL_OK
               : NINLIL_ERR_STATE;
}
void ninlil_node_probe_transmitted(ninlil_node *n, const uint8_t *frame,
                                   size_t length, uint32_t airtime)
{
    uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
    size_t size = 0u;
    int index = inspect(n, frame, length, plain, &size);
    node_peer *p;
    if (index < 0 || size != sizeof(plain) || plain[0] != NODE_PROBE)
        return;
    p = &n->peers[index];
    if (p->probe_sent || ninlil_node_get(plain + 1, 8u) != p->probe_token)
        return;
    p->probe_sent = 1u;
    p->probe_window = (uint8_t)(p->probe_window << 1);
    if (p->attempts < 8u)
        p->attempts++;
    p->probe_airtime_us = airtime;
    p->probe_sent_at = p->observed_at = n->now_ms;
    p->probe_at = n->now_ms + (p->attempts < 8u ? 4000u : 10000u) +
                  p->probe_token % 1001u;
    p->probe_report = 1u;
}
static int observation(ninlil_node *n, uint16_t peer, const uint8_t *data,
                       size_t length)
{
    ninlil_network_edge e = {0};
    uint64_t now;
    uint8_t ack[11], bits;
    unsigned int count = 0u;
    int rc;
    if (n->config.local != n->config.root || length != 41u ||
        !ninlil_node_get(data + 32, 8u) ||
        ninlil_node_lease(n, &now) != NINLIL_OK)
        return NINLIL_ERR_STATE;
    e.from = (uint16_t)ninlil_node_get(data, 2u);
    e.to = (uint16_t)ninlil_node_get(data + 2, 2u);
    e.attempts = (uint16_t)ninlil_node_get(data + 4, 2u);
    e.delivered = (uint16_t)ninlil_node_get(data + 6, 2u);
    e.airtime_us = (uint32_t)ninlil_node_get(data + 8, 4u);
    e.queue_us = (uint32_t)ninlil_node_get(data + 12, 4u);
    e.observed_ms = ninlil_node_get(data + 16, 8u);
    e.membership_epoch = ninlil_node_get(data + 24, 8u);
    e.used = 1u;
    bits = data[40];
    while (bits) {
        count += bits & 1u;
        bits = (uint8_t)(bits >> 1);
    }
    if (e.attempts != 8u || count != e.delivered)
        return NINLIL_ERR_INVALID;
    rc = ninlil_coordinator_observe(&n->coordinator, peer, &e, now);
    if (rc != NINLIL_OK || peer == n->config.local)
        return rc;
    memcpy(ack, data + 2, 2u);
    memcpy(ack + 2, data + 32, 9u);
    return ninlil_node_control_send(n, peer, NODE_OBSERVATION_ACK, ack,
                                    sizeof(ack));
}
int ninlil_node_link_receive(ninlil_node *n, uint16_t peer,
                             node_control_kind kind, const uint8_t *data,
                             size_t length)
{
    int index = ninlil_node_index(n, peer);
    node_peer *p;
    if (!allowed(n, index))
        return NINLIL_ERR_UNAUTHORIZED;
    p = &n->peers[index];
    if (kind == NODE_OBSERVATION_ACK && peer == n->config.root &&
        length == 11u) {
        int measured =
            ninlil_node_index(n, (uint16_t)ninlil_node_get(data, 2u));
        if (measured < 0)
            return NINLIL_ERR_INVALID;
        p = &n->peers[measured];
        if (ninlil_node_get(data + 2, 8u) != p->probe_token ||
            data[10] != p->probe_window)
            return NINLIL_ERR_STATE;
        p->probe_report = 0u;
        return NINLIL_OK;
    }
    if (kind == NODE_PROBE) {
        uint8_t reply[9];
        if (length != NINLIL_SECURE_PLAINTEXT_MAX - 1u ||
            !ninlil_node_get(data, 8u))
            return NINLIL_ERR_INVALID;
        for (size_t i = 8u; i < length; i++)
            if (data[i])
                return NINLIL_ERR_INVALID;
        reply[0] = NODE_PROBE_REPLY;
        memcpy(reply + 1, data, 8u);
        return send_neighbor(n, (unsigned int)index, reply, sizeof(reply));
    }
    if (kind == NODE_PROBE_REPLY) {
        if (length != 8u || !p->probe_sent ||
            ninlil_node_get(data, 8u) != p->probe_token ||
            n->now_ms - p->probe_sent_at > 3000u)
            return NINLIL_ERR_STATE;
        p->probe_window |= 1u;
        p->probe_report = 1u;
        return NINLIL_OK;
    }
    if (kind == NODE_OBSERVATION)
        return observation(n, peer, data, length);
    return ninlil_node_recovery_receive(n, peer, kind, data, length);
}
static int report(ninlil_node *n, unsigned int index)
{
    node_peer *p = &n->peers[index];
    uint8_t data[41], bits = p->probe_window;
    uint64_t lease, age = n->now_ms - p->observed_at;
    uint16_t delivered = 0u;
    int rc;
    if (p->attempts < 8u || age > NINLIL_NETWORK_STALE_MS ||
        ninlil_node_lease(n, &lease) != NINLIL_OK ||
        lease < age + NINLIL_LEASE_SYNC_ERROR_BOUND_MS)
        return NINLIL_ERR_STATE;
    while (bits) {
        delivered += (uint16_t)(bits & 1u);
        bits = (uint8_t)(bits >> 1);
    }
    p->delivered = delivered;
    ninlil_node_put(data, n->config.local, 2u);
    ninlil_node_put(data + 2, n->members[index].grant.node, 2u);
    ninlil_node_put(data + 4, p->attempts, 2u);
    ninlil_node_put(data + 6, delivered, 2u);
    ninlil_node_put(data + 8, p->probe_airtime_us, 4u);
    ninlil_node_put(data + 12, 0u, 4u);
    /* Conservative timestamp: bounded clock reply age must not make old RF
     * evidence appear fresh at the authority. */
    ninlil_node_put(data + 16, lease - age - NINLIL_LEASE_SYNC_ERROR_BOUND_MS,
                    8u);
    ninlil_node_put(data + 24,
                    n->members[n->local_index].grant.membership_epoch, 8u);
    ninlil_node_put(data + 32, p->probe_token, 8u);
    data[40] = p->probe_window;
    rc = n->config.local == n->config.root
             ? observation(n, n->config.local, data, sizeof(data))
             : ninlil_node_control_send(n, n->config.root, NODE_OBSERVATION,
                                        data, sizeof(data));
    if (rc == NINLIL_OK && n->config.local == n->config.root)
        p->probe_report = 0u;
    return rc;
}
int ninlil_node_links_step(ninlil_node *n)
{
    uint64_t lease;
    unsigned int i;
    int rc = ninlil_node_recovery_step(n);
    if (rc != NINLIL_OK || !n->joined ||
        ninlil_node_lease(n, &lease) != NINLIL_OK)
        return rc;
    for (i = 0u; i < n->config.member_count; i++) {
        node_peer *p = &n->peers[i];
        if (i == n->local_index || !allowed(n, (int)i) || !p->sessions[1].ready)
            continue;
        if (p->probe_report && p->attempts == 8u && n->now_ms >= p->report_at &&
            n->now_ms - p->probe_sent_at >= 1000u) {
            /* Six directed samples plus their ACKs share one slow channel.
             * A lost ACK must not turn every owner into a continuous reporter.
             */
            p->report_at = n->now_ms + 4000u + p->probe_token % 2001u;
            rc = report(n, i);
            if (rc != NINLIL_ERR_STATE)
                return rc;
        }
        if (n->now_ms >= p->probe_at) {
            uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX] = {NODE_PROBE};
            rc = n->config.random.fill(n->config.random.ctx, plain + 1, 8u);
            if (rc != NINLIL_OK)
                return rc;
            p->probe_token = ninlil_node_get(plain + 1, 8u);
            if (!p->probe_token)
                return NINLIL_ERR_IO;
            p->probe_sent = 0u;
            /* A queued measurement owns its challenge until actual TX_DONE.
             * Bound abandoned staging too; failed admission is not an attempt.
             */
            rc = send_neighbor(n, i, plain, sizeof(plain));
            p->probe_at =
                n->now_ms +
                (rc == NINLIL_OK ? NINLIL_EDHOC_DEADLINE_MS : NODE_RETRY_MS);
            if (rc != NINLIL_OK)
                p->probe_token = 0u;
            return rc;
        }
    }
    return NINLIL_OK;
}
