#include "ninlil_node_internal.h"
#include <string.h>

int ninlil_node_revoke(ninlil_node *n, uint16_t peer, uint64_t expected)
{
    int index, rc;
    ninlil_join_peer *p;
    if (!n || n->status.fault || n->config.local != n->config.root ||
        peer == n->config.local || (index = ninlil_node_index(n, peer)) < 0)
        return NINLIL_ERR_UNAUTHORIZED;
    p = ninlil_node_authority_peer(n, (uint16_t)index);
    if (!p || !p->persisted || p->record.grant.membership_epoch != expected)
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_join_revoke(&n->authority, p->identity);
    if (rc != NINLIL_OK)
        return rc;
    if (!n->peers[index].revoked) {
        n->peers[index].revoked = 1u;
        n->peers[index].member_active = 0u;
        ninlil_coordinator_disconnect(&n->coordinator, peer);
        ninlil_node_membership_changed(n);
    }
    return NINLIL_OK;
}
int ninlil_node_relay_drain(ninlil_node *n, int draining)
{
    int rc;
    uint8_t before;
    if (!n || n->status.fault || !n->routed.config.relay)
        return NINLIL_ERR_STATE;
    before = n->relay.draining;
    rc = ninlil_relay_drain(&n->relay, draining);
    if (rc == NINLIL_OK) {
        if (before != n->relay.draining)
            n->removal_ready = 0u;
        n->peers[n->local_index].draining = n->relay.draining;
    }
    return rc;
}
int ninlil_node_drain(ninlil_node *n)
{
    return ninlil_node_relay_drain(n, 1);
}
int ninlil_node_ready_remove(ninlil_node *n)
{
    return n && !n->status.fault && n->removal_ready &&
           ninlil_relay_ready_remove(&n->relay);
}

static int revocation(ninlil_node *n, uint16_t peer, node_control_kind kind,
                      const uint8_t *data, size_t length)
{
    int index = ninlil_node_index(n, peer), rc;
    ninlil_join_record record;
    uint8_t ack[8];
    if (kind == NODE_REVOKE_ACK && n->config.local == n->config.root &&
        length == 8u && index >= 0 && n->peers[index].revoked &&
        ninlil_node_get(data, 8u) == n->members[index].grant.membership_epoch) {
        n->peers[index].revocation_applied = 1u;
        ninlil_node_disconnect(n, (uint16_t)index);
        return NINLIL_OK;
    }
    if (kind != NODE_REVOKE_NOTICE || peer != n->config.root ||
        n->config.local == n->config.root ||
        ninlil_join_decode(data, length, &record) != NINLIL_OK ||
        record.state != NINLIL_JOIN_REVOKED ||
        !ninlil_node_record_matches(n, n->local_index, &record))
        return NINLIL_ERR_UNAUTHORIZED;
    if (!n->endpoint.persisted ||
        n->endpoint.record.state != NINLIL_JOIN_REVOKED) {
        rc = ninlil_node_join_commit(n, &record);
        if (rc == NINLIL_OK)
            rc = ninlil_join_endpoint_restore(&n->endpoint, &record);
        if (rc != NINLIL_OK)
            return rc;
    }
    n->joined = 0u;
    n->peers[n->local_index].revoked = 1u;
    n->peers[n->local_index].member_active = 0u;
    memset(n->local_ready, 0, sizeof(n->local_ready));
    ninlil_lease_invalidate(&n->clock);
    ninlil_node_put(ack, record.grant.membership_epoch, 8u);
    return ninlil_node_control_send(n, peer, NODE_REVOKE_ACK, ack, sizeof(ack));
}

int ninlil_node_lifecycle_receive(ninlil_node *n, uint16_t peer,
                                  node_control_kind kind, const uint8_t *data,
                                  size_t length)
{
    int index = ninlil_node_index(n, peer);
    uint64_t epoch;
    if (kind == NODE_REVOKE_NOTICE || kind == NODE_REVOKE_ACK)
        return revocation(n, peer, kind, data, length);
    if (index < 0)
        return NINLIL_ERR_UNAUTHORIZED;
    if (kind == NODE_DRAIN_ACK && peer == n->config.root && length == 10u &&
        n->routed.config.relay &&
        ninlil_node_get(data, 8u) == n->relay.drain_epoch &&
        data[8] == n->relay.draining &&
        data[9] == (ninlil_relay_ready_remove(&n->relay) ? 1u : 0u)) {
        n->drain_ack_epoch = n->relay.drain_epoch;
        n->drain_ack_state = (uint8_t)(data[8] * 2u + data[9]);
        return NINLIL_OK;
    }
    if (kind == NODE_REMOVE_READY_ACK && n->config.local == n->config.root &&
        length == 8u && n->peers[index].draining &&
        n->peers[index].drain_ready &&
        ninlil_node_get(data, 8u) == n->peers[index].drain_epoch) {
        n->peers[index].remove_ack_epoch = n->peers[index].drain_epoch;
        return NINLIL_OK;
    }
    if (kind == NODE_REMOVE_READY && peer == n->config.root && length == 8u &&
        n->routed.config.relay && ninlil_relay_ready_remove(&n->relay) &&
        ninlil_node_get(data, 8u) == n->relay.drain_epoch) {
        n->removal_ready = 1u;
        return ninlil_node_control_send(n, peer, NODE_REMOVE_READY_ACK, data,
                                        length);
    }
    if (kind != NODE_DRAIN || n->config.local != n->config.root ||
        length != 10u || data[8] > 1u || data[9] > 1u ||
        !(n->members[index].grant.capabilities & NINLIL_CAP_RELAY_CUSTODY))
        return NINLIL_ERR_UNAUTHORIZED;
    epoch = ninlil_node_get(data, 8u);
    if (!epoch || epoch < n->peers[index].drain_epoch || (!data[8] && data[9]))
        return NINLIL_ERR_STATE;
    if (epoch == n->peers[index].drain_epoch &&
        (data[8] != n->peers[index].draining ||
         data[9] < n->peers[index].drain_ready))
        return NINLIL_ERR_STATE;
    n->peers[index].drain_epoch = epoch;
    n->peers[index].draining = data[8];
    n->peers[index].drain_ready = data[9];
    return ninlil_node_control_send(n, peer, NODE_DRAIN_ACK, data, length);
}

static int root_step(ninlil_node *n)
{
    unsigned int i;
    uint64_t now;
    for (i = 0u; i < n->config.member_count; i++) {
        node_peer *p = &n->peers[i];
        uint8_t data[NINLIL_JOIN_RECORD_MAX];
        if (i == n->local_index && n->routed.config.relay) {
            p->draining = n->relay.draining;
            p->drain_epoch = n->relay.drain_epoch;
            p->drain_ready = ninlil_relay_ready_remove(&n->relay) ? 1u : 0u;
        } else if (!p->sessions[0].ready)
            continue;
        if (p->revoked && !p->revocation_applied) {
            ninlil_join_peer *j = ninlil_node_authority_peer(n, (uint16_t)i);
            size_t length =
                j ? ninlil_join_encode(&j->record, data, sizeof(data)) : 0u;
            return length
                       ? ninlil_node_control_send(n, n->members[i].grant.node,
                                                  NODE_REVOKE_NOTICE, data,
                                                  length)
                       : NINLIL_ERR_CORRUPT;
        }
        if (p->draining && p->drain_ready &&
            p->remove_ack_epoch != p->drain_epoch &&
            ninlil_node_lease(n, &now) == NINLIL_OK) {
            ninlil_remove_status status;
            int rc = ninlil_coordinator_remove_status(
                &n->coordinator, n->members[i].grant.node, now, 1, &status);
            if (rc == NINLIL_OK && status.ready) {
                if (i == n->local_index) {
                    n->removal_ready = 1u;
                    continue;
                }
                ninlil_node_put(data, p->drain_epoch, 8u);
                return ninlil_node_control_send(n, n->members[i].grant.node,
                                                NODE_REMOVE_READY, data, 8u);
            }
        }
    }
    return NINLIL_OK;
}
int ninlil_node_lifecycle_step(ninlil_node *n)
{
    uint8_t data[10];
    if (n->config.local == n->config.root)
        return root_step(n);
    if (!n->joined || !n->routed.config.relay || !n->relay.drain_epoch)
        return NINLIL_OK;
    ninlil_node_put(data, n->relay.drain_epoch, 8u);
    data[8] = n->relay.draining;
    data[9] = ninlil_relay_ready_remove(&n->relay) ? 1u : 0u;
    if (n->drain_ack_epoch == n->relay.drain_epoch &&
        n->drain_ack_state == (uint8_t)(data[8] * 2u + data[9]))
        return NINLIL_OK;
    return ninlil_node_control_send(n, n->config.root, NODE_DRAIN, data,
                                    sizeof(data));
}
