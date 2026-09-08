#include "ninlil_node_internal.h"
#include <string.h>

int ninlil_node_recovery_receive(ninlil_node *n, uint16_t peer,
                                 node_control_kind kind, const uint8_t *data,
                                 size_t length)
{
    int index = ninlil_node_index(n, peer), target_index;
    uint16_t target;
    uint8_t reply[34];
    const uint8_t *fresh;
    if (kind != NODE_RECOVER_REQUEST && kind != NODE_RECOVER_CONFIRMED)
        return ninlil_node_lifecycle_receive(n, peer, kind, data, length);
    if (index < 0 || length != (kind == NODE_RECOVER_REQUEST ? 18u : 34u))
        return NINLIL_ERR_INVALID;
    target = (uint16_t)ninlil_node_get(data, 2u);
    target_index = ninlil_node_index(n, target);
    if (target_index < 0 || target == peer || target == n->config.local)
        return NINLIL_ERR_UNAUTHORIZED;
    if (kind == NODE_RECOVER_CONFIRMED)
        return n->routed.config.relay
                   ? ninlil_relay_return_to_source(&n->relay, peer, target,
                                                   data + 2, data + 18)
                   : NINLIL_ERR_UNAUTHORIZED;
    if (!(n->members[index].grant.capabilities & NINLIL_CAP_RELAY_CUSTODY) ||
        ninlil_health(n->core) != NINLIL_OK)
        return NINLIL_ERR_STATE;
    if (!n->peers[target_index].sessions[0].ready)
        return NINLIL_ERR_BUSY;
    fresh = n->peers[target_index].sessions[0].material.fingerprint;
    if (memcmp(fresh, data + 2, 16u) == 0) {
        /* End the obsolete envelope context, retaining the bound Core store.
         * The relay repeats its request after a fresh EDHOC exchange. */
        ninlil_node_disconnect(n, (uint16_t)target_index);
        return NINLIL_ERR_BUSY;
    }
    {
        int rc = ninlil_verify_retained(n->core);
        if (rc != NINLIL_OK)
            return rc;
    }
    memcpy(reply, data, 18u);
    memcpy(reply + 18, fresh, 16u);
    return ninlil_node_control_send(n, peer, NODE_RECOVER_CONFIRMED, reply,
                                    sizeof(reply));
}

static int recover_copy(ninlil_node *n, ninlil_relay_record *record,
                        uint64_t now)
{
    uint16_t source = record->path.nodes[0];
    uint16_t target = record->path.nodes[record->path.count - 1u];
    ninlil_network_plan plan;
    int slot =
        n->relay.draining ? -1 : ninlil_node_local_plan(n, source, target, 0);
    int rc = n->relay.draining
                 ? NINLIL_ERR_BUSY
                 : ninlil_node_route(n, source, target, now, &plan);
    /* Route loss or an outstanding EFFECTIVE notification is not proof of
     * obsolete encryption. Route() requests reconciliation; keep custody and
     * wait for the authority's current E2E binding before requesting a rekey.
     */
    if (!n->relay.draining &&
        (rc != NINLIL_OK || slot < 0 || n->local_ready[slot] != 3u))
        return NINLIL_ERR_BUSY;
    if (!n->relay.draining && !record->legacy &&
        memcmp(record->ciphertext + 8,
               n->plan_bindings[slot][plan.path.count - 1u], 16u) == 0) {
        if (record->route_epoch < plan.epoch)
            return ninlil_relay_repair(&n->relay, record->packet_id, &plan.path,
                                       plan.epoch, now);
        if (record->route_epoch == plan.epoch)
            return NINLIL_OK;
    }
    /* A draining Relay cannot obtain a replacement route through itself.
     * It may explicitly return custody even while the old context is current;
     * the source ends that context and verifies durable Core retention first.
     * Lease/session expiry alone never authorizes discarding ciphertext. */
    {
        uint8_t request[18];
        ninlil_node_put(request, target, 2u);
        memcpy(request + 2, record->ciphertext + 8, 16u);
        return ninlil_node_control_send(n, source, NODE_RECOVER_REQUEST,
                                        request, sizeof(request));
    }
}

int ninlil_node_recovery_step(ninlil_node *n)
{
    uint64_t now;
    unsigned int i;
    int rc;
    if (n->now_ms < n->recovery_at)
        return NINLIL_OK;
    n->recovery_at = n->now_ms + NODE_RETRY_MS;
    rc = ninlil_node_lifecycle_step(n);
    if (rc != NINLIL_OK || !n->joined || !n->routed.config.relay ||
        ninlil_node_lease(n, &now) != NINLIL_OK)
        return rc;
    for (i = 0u; i < n->relay.capacity; i++) {
        unsigned int at =
            (unsigned int)((n->member_cursor + i) % n->relay.capacity);
        if (n->custody[at].used) {
            n->member_cursor = (uint8_t)((at + 1u) % n->relay.capacity);
            return recover_copy(n, &n->custody[at].record, now);
        }
    }
    return NINLIL_OK;
}
