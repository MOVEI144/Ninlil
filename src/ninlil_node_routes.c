#include "ninlil_node_internal.h"
#include <string.h>

static uint8_t all(const ninlil_network_plan *p)
{
    return (uint8_t)((1u << p->path.count) - 1u);
}
static int flow(ninlil_node *n, uint16_t source, uint16_t target)
{
    unsigned int i;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        const ninlil_network_plan *p = &n->coordinator.flows[i].active;
        if (p->epoch && p->path.nodes[0] == source &&
            p->path.nodes[p->path.count - 1u] == target)
            return (int)i;
    }
    return -1;
}
static ninlil_network_plan *epoch(ninlil_node *n, uint64_t value)
{
    unsigned int i;
    if (value && n->coordinator.pending.epoch == value)
        return &n->coordinator.pending;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
        if (value && n->coordinator.flows[i].active.epoch == value)
            return &n->coordinator.flows[i].active;
    return NULL;
}
static int plan_send(ninlil_node *n, uint16_t peer, node_control_kind kind,
                     const ninlil_network_plan *p, const uint8_t *bindings)
{
    uint8_t bytes[NINLIL_NETWORK_PLAN_MAX + NINLIL_NETWORK_PATH_MAX * 16u];
    size_t length = ninlil_network_plan_encode(p, bytes, sizeof(bytes));
    if (!length)
        return NINLIL_ERR_INVALID;
    if (bindings) {
        size_t extra = (size_t)p->path.count * 16u;
        memcpy(bytes + length, bindings, extra);
        length += extra;
    }
    return ninlil_node_control_send(n, peer, kind, bytes, length);
}

static int proof_matches(ninlil_node *n, const ninlil_network_plan *p)
{
    unsigned int i;
    if (n->proof_mask != all(p))
        return 0;
    for (i = 0u; i + 1u < p->path.count; i++)
        if (memcmp(n->proofs[i] + 16, (uint8_t[16]){0}, 16u) == 0 ||
            memcmp(n->proofs[i] + 16, n->proofs[i + 1u], 16u) != 0)
            return 0;
    return memcmp(n->proofs[0] + 32, (uint8_t[16]){0}, 16u) != 0 &&
           memcmp(n->proofs[0] + 32, n->proofs[p->path.count - 1u] + 32, 16u) ==
               0;
}
static int applied(ninlil_node *n, uint16_t peer, uint64_t value,
                   const uint8_t proof[48])
{
    ninlil_network_plan *current = epoch(n, value), plan;
    int at, slot, rc = NINLIL_OK;
    unsigned int i;
    if (!current || n->proof_epoch != value ||
        (current->phase != NINLIL_PLAN_COMMITTED &&
         current->phase != NINLIL_PLAN_EFFECTIVE))
        return NINLIL_ERR_STATE;
    plan = *current;
    at = ninlil_node_plan_position(&plan, peer);
    if (at < 0 || (at == 0 && memcmp(proof, (uint8_t[16]){0}, 16u) != 0) ||
        ((unsigned int)at + 1u == plan.path.count &&
         memcmp(proof + 16, (uint8_t[16]){0}, 16u) != 0) ||
        (at > 0 && (unsigned int)at + 1u < plan.path.count &&
         memcmp(proof + 32, (uint8_t[16]){0}, 16u) != 0))
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(n->proofs[at], proof, 48u);
    n->proof_mask |= (uint8_t)(1u << (unsigned int)at);
    if (!proof_matches(n, &plan))
        return NINLIL_OK;
    /* No historical APPLIED bit substitutes for matching live hop/E2E reports.
     */
    for (i = 0u; i < plan.path.count && rc == NINLIL_OK; i++)
        rc = plan.phase == NINLIL_PLAN_COMMITTED
                 ? ninlil_coordinator_applied(&n->coordinator,
                                              plan.path.nodes[i], value)
                 : ninlil_coordinator_reconcile(&n->coordinator,
                                                plan.path.nodes[i], value);
    if (rc != NINLIL_OK)
        return rc;
    slot = flow(n, plan.path.nodes[0], plan.path.nodes[plan.path.count - 1u]);
    if (slot < 0)
        return NINLIL_ERR_FAULT;
    if (n->effective_epoch[slot] == value)
        return NINLIL_OK;
    for (i = 0u; i + 1u < plan.path.count; i++)
        memcpy(n->effective_bindings[slot][i], n->proofs[i] + 16, 16u);
    memcpy(n->effective_bindings[slot][plan.path.count - 1u], n->proofs[0] + 32,
           16u);
    n->effective_epoch[slot] = value;
    n->effective_notified[slot] = 0u;
    return NINLIL_OK;
}

static int release_ack(ninlil_node *n, uint16_t peer, uint64_t old,
                       uint64_t replacement)
{
    const ninlil_network_plan *p = &n->coordinator.pending;
    int slot, at;
    if (p->phase != NINLIL_PLAN_STAGED || p->epoch != replacement ||
        n->proof_epoch != replacement)
        return NINLIL_ERR_STATE;
    slot = flow(n, p->path.nodes[0], p->path.nodes[p->path.count - 1u]);
    if (slot < 0 || n->coordinator.flows[slot].active.epoch != old)
        return NINLIL_ERR_STATE;
    at = ninlil_node_plan_position(&n->coordinator.flows[slot].active, peer);
    if (at < 0)
        return NINLIL_ERR_UNAUTHORIZED;
    n->released_mask |= (uint8_t)(1u << (unsigned int)at);
    return NINLIL_OK;
}

static int receive_plan(ninlil_node *n, node_control_kind kind,
                        const uint8_t *data, size_t length)
{
    ninlil_network_plan plan;
    uint8_t reply[56];
    int rc;
    if (length < NINLIL_NETWORK_PLAN_MAX ||
        ninlil_network_plan_decode(data, NINLIL_NETWORK_PLAN_MAX, &plan) !=
            NINLIL_OK ||
        (kind != NODE_EFFECTIVE && length != NINLIL_NETWORK_PLAN_MAX))
        return NINLIL_ERR_INVALID;
    ninlil_node_put(reply, plan.epoch, 8u);
    if (kind == NODE_PREPARE) {
        rc = ninlil_node_prepare(n, &plan);
        return rc == NINLIL_OK
                   ? ninlil_node_control_send(n, n->config.root, NODE_PREPARED,
                                              reply, 8u)
                   : rc;
    }
    if (kind == NODE_APPLY) {
        rc = ninlil_node_apply(n, &plan, reply + 8);
        return rc == NINLIL_OK
                   ? ninlil_node_control_send(n, n->config.root, NODE_APPLIED,
                                              reply, sizeof(reply))
                   : rc;
    }
    rc = ninlil_node_effective(n, &plan, data + NINLIL_NETWORK_PLAN_MAX,
                               length - NINLIL_NETWORK_PLAN_MAX);
    if (rc != NINLIL_OK) {
        ninlil_node_need_reconcile(n, plan.path.nodes[0],
                                   plan.path.nodes[plan.path.count - 1u]);
        return rc;
    }
    return ninlil_node_control_send(n, n->config.root, NODE_EFFECTIVE_ACK,
                                    reply, 8u);
}

static int request_flow(ninlil_node *n, uint16_t peer, const uint8_t *data,
                        size_t length)
{
    uint16_t source, target;
    uint64_t token;
    int slot, at;
    if (length != 13u || data[12] > 1u)
        return NINLIL_ERR_INVALID;
    source = (uint16_t)ninlil_node_get(data, 2u);
    target = (uint16_t)ninlil_node_get(data + 2, 2u);
    token = ninlil_node_get(data + 4, 8u);
    slot = flow(n, source, target);
    at = slot < 0 ? -1
                  : ninlil_node_plan_position(
                        &n->coordinator.flows[slot].active, peer);
    if (!token || source == target || ninlil_node_index(n, source) < 0 ||
        ninlil_node_index(n, target) < 0 || (peer != source && at < 0))
        return NINLIL_ERR_UNAUTHORIZED;
    if (data[12] && slot >= 0 && at >= 0) {
        if (n->request_peer[slot][at] != peer) {
            n->request_peer[slot][at] = peer;
            n->request_seen[slot][at] = 0u;
        }
        if (token > n->request_seen[slot][at]) {
            n->request_seen[slot][at] = token;
            n->coordinator.flows[slot].reconciled = 0u;
            n->effective_epoch[slot] = 0u;
            n->effective_notified[slot] = 0u;
            /* Restart this round without discarding another flow's proofs. */
            if (!n->proof_epoch ||
                n->proof_epoch == n->coordinator.flows[slot].active.epoch) {
                n->proof_epoch = n->coordinator.flows[slot].active.epoch;
                n->proof_mask = 0u;
            }
        }
    }
    ninlil_node_want(n, source, target);
    return NINLIL_OK;
}

int ninlil_node_plan_receive(ninlil_node *n, uint16_t peer,
                             node_control_kind kind, const uint8_t *data,
                             size_t length)
{
    if (peer == n->config.root && n->config.local != n->config.root) {
        if (kind == NODE_PREPARE || kind == NODE_APPLY ||
            kind == NODE_EFFECTIVE)
            return receive_plan(n, kind, data, length);
        if (kind == NODE_RELEASE && length == 16u) {
            int rc = ninlil_node_release(n, ninlil_node_get(data, 8u),
                                         ninlil_node_get(data + 8, 8u));
            return rc == NINLIL_OK ? ninlil_node_control_send(
                                         n, peer, NODE_RELEASED, data, length)
                                   : rc;
        }
    }
    if (n->config.local == n->config.root) {
        if (kind == NODE_PREPARED && length == 8u)
            return ninlil_coordinator_prepared(&n->coordinator, peer,
                                               ninlil_node_get(data, 8u));
        if (kind == NODE_APPLIED && length == 56u)
            return applied(n, peer, ninlil_node_get(data, 8u), data + 8);
        if (kind == NODE_RELEASED && length == 16u)
            return release_ack(n, peer, ninlil_node_get(data, 8u),
                               ninlil_node_get(data + 8, 8u));
        if (kind == NODE_EFFECTIVE_ACK && length == 8u) {
            ninlil_network_plan *p = epoch(n, ninlil_node_get(data, 8u));
            int at, slot;
            if (!p || p->phase != NINLIL_PLAN_EFFECTIVE ||
                (at = ninlil_node_plan_position(p, peer)) < 0)
                return NINLIL_ERR_STATE;
            slot = flow(n, p->path.nodes[0], p->path.nodes[p->path.count - 1u]);
            if (slot < 0 || n->effective_epoch[slot] != p->epoch)
                return NINLIL_ERR_STATE;
            n->effective_notified[slot] |= (uint8_t)(1u << (unsigned int)at);
            return NINLIL_OK;
        }
        if (kind == NODE_FLOW_REQUEST)
            return request_flow(n, peer, data, length);
    }
    return ninlil_node_link_receive(n, peer, kind, data, length);
}

static int pending(ninlil_node *n, uint64_t now)
{
    ninlil_network_plan plan = n->coordinator.pending;
    uint16_t peer;
    int rc;
    unsigned int at;
    if (plan.epoch != n->proof_epoch) {
        n->proof_epoch = plan.epoch;
        n->proof_mask = n->released_mask = 0u;
    }
    if (now >= plan.valid_until_ms)
        return ninlil_coordinator_withdraw(&n->coordinator, plan.epoch, now,
                                           NINLIL_TIME_RESTART_SAFE, 0);
    if (plan.phase == NINLIL_PLAN_STAGED &&
        n->coordinator.prepared_live == all(&plan)) {
        int slot =
            flow(n, plan.path.nodes[0], plan.path.nodes[plan.path.count - 1u]);
        int released = 0;
        if (slot >= 0 &&
            n->coordinator.flows[slot].active.valid_until_ms > now) {
            const ninlil_network_plan *old = &n->coordinator.flows[slot].active;
            if (n->released_mask != all(old)) {
                uint8_t request[16];
                at = (unsigned int)(n->plan_cursor++ % old->path.count);
                if (n->released_mask & (uint8_t)(1u << at))
                    return NINLIL_OK;
                peer = old->path.nodes[at];
                if (peer == n->config.local) {
                    rc = ninlil_node_release(n, old->epoch, plan.epoch);
                    return rc == NINLIL_OK
                               ? release_ack(n, peer, old->epoch, plan.epoch)
                               : rc;
                }
                ninlil_node_put(request, old->epoch, 8u);
                ninlil_node_put(request + 8, plan.epoch, 8u);
                return ninlil_node_control_send(n, peer, NODE_RELEASE, request,
                                                sizeof(request));
            }
            released = 1;
        }
        return ninlil_coordinator_activate(&n->coordinator, now,
                                           NINLIL_TIME_RESTART_SAFE, released);
    }
    at = (unsigned int)(n->plan_cursor++ % plan.path.count);
    if ((plan.phase == NINLIL_PLAN_STAGED || n->proof_mask != all(&plan)) &&
        ((plan.phase == NINLIL_PLAN_STAGED ? n->coordinator.prepared_live
                                           : n->proof_mask) &
         (uint8_t)(1u << at)))
        return NINLIL_OK;
    peer = plan.path.nodes[at];
    if (peer != n->config.local)
        return plan_send(n, peer,
                         plan.phase == NINLIL_PLAN_STAGED ? NODE_PREPARE
                                                          : NODE_APPLY,
                         &plan, NULL);
    if (plan.phase == NINLIL_PLAN_STAGED) {
        rc = ninlil_node_prepare(n, &plan);
        return rc == NINLIL_OK ? ninlil_coordinator_prepared(&n->coordinator,
                                                             peer, plan.epoch)
                               : rc;
    }
    {
        uint8_t proof[48];
        rc = ninlil_node_apply(n, &plan, proof);
        return rc == NINLIL_OK ? applied(n, peer, plan.epoch, proof) : rc;
    }
}

static int active(ninlil_node *n, unsigned int slot)
{
    ninlil_network_flow *f = &n->coordinator.flows[slot];
    ninlil_network_plan *p = &f->active;
    unsigned int at = (unsigned int)(n->plan_cursor++ % p->path.count);
    uint16_t peer = p->path.nodes[at];
    int rc;
    if (f->reconciled != all(p) || n->effective_epoch[slot] != p->epoch) {
        uint8_t proof[48];
        if (n->proof_epoch != p->epoch) {
            n->proof_epoch = p->epoch;
            n->proof_mask = 0u;
        }
        if (n->proof_mask != all(p) && (n->proof_mask & (uint8_t)(1u << at)))
            return NINLIL_OK;
        if (peer != n->config.local)
            return plan_send(n, peer, NODE_APPLY, p, NULL);
        rc = ninlil_node_apply(n, p, proof);
        return rc == NINLIL_OK ? applied(n, peer, p->epoch, proof) : rc;
    }
    if (n->effective_notified[slot] & (uint8_t)(1u << at))
        return NINLIL_OK;
    if (peer != n->config.local)
        return plan_send(n, peer, NODE_EFFECTIVE, p,
                         &n->effective_bindings[slot][0][0]);
    rc = ninlil_node_effective(n, p, &n->effective_bindings[slot][0][0],
                               (size_t)p->path.count * 16u);
    if (rc == NINLIL_OK)
        n->effective_notified[slot] |= (uint8_t)(1u << at);
    return rc;
}

static int maintain(ninlil_node *n, uint64_t now)
{
    unsigned int i;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_plan *p = &n->coordinator.flows[i].active;
        ninlil_network_path path;
        int rc;
        if (!p->epoch)
            continue;
        if (p->valid_until_ms <= now) {
            n->effective_epoch[i] = 0u;
            n->effective_notified[i] = 0u;
            return ninlil_coordinator_retire(&n->coordinator, p->path.nodes[0],
                                             p->path.nodes[p->path.count - 1u],
                                             p->epoch, now,
                                             NINLIL_TIME_RESTART_SAFE, 0);
        }
        n->planning = 1u;
        rc = ninlil_coordinator_select(&n->coordinator, p->path.nodes[0],
                                       p->path.nodes[p->path.count - 1u], 0u,
                                       now, &path);
        n->planning = 0u;
        if (rc != NINLIL_OK)
            continue;
        /* Optimize a changed path; do not renew idle routes every minute. */
        if (p->path.count != path.count ||
            memcmp(p->path.nodes, path.nodes, sizeof(path.nodes)) != 0 ||
            memcmp(p->path.membership_epochs, path.membership_epochs,
                   sizeof(path.membership_epochs)) != 0)
            return ninlil_coordinator_stage(&n->coordinator, &path, now,
                                            now + NINLIL_NETWORK_LEASE_MAX_MS,
                                            NINLIL_TIME_RESTART_SAFE);
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_node_routes_step(ninlil_node *n)
{
    uint64_t now;
    unsigned int i;
    if (!n->joined || n->now_ms < n->route_at ||
        ninlil_node_lease(n, &now) != NINLIL_OK)
        return NINLIL_OK;
    /* Leave airtime for synchronization and responses between plan retries. */
    n->route_at = n->now_ms + 2000u;
    if (n->config.local == n->config.root && n->coordinator.pending.epoch)
        return pending(n, now);
    {
        int rc = ninlil_node_expire_plans(n, now);
        if (rc != NINLIL_OK)
            return rc;
    }
    if (n->config.local == n->config.root) {
        ninlil_network_plan *p;
        int rc = maintain(n, now);
        if (rc != NINLIL_ERR_EMPTY)
            return rc;
        /* Complete one proof round before starting another flow. */
        p = epoch(n, n->proof_epoch);
        if (p && p->phase == NINLIL_PLAN_EFFECTIVE) {
            int slot =
                flow(n, p->path.nodes[0], p->path.nodes[p->path.count - 1u]);
            if (slot >= 0 && n->effective_notified[slot] != all(p))
                return active(n, (unsigned int)slot);
        }
    }
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        unsigned int slot = n->wanted_cursor;
        uint16_t source = n->wanted[slot][0], target = n->wanted[slot][1];
        ninlil_network_plan plan;
        int existing, rc;
        n->wanted_cursor = (uint8_t)((slot + 1u) % NINLIL_NETWORK_FLOWS_MAX);
        if (!source)
            continue;
        /* Draining intermediates return old custody to the source instead of
         * requesting a new route which can no longer include them. */
        if (n->relay.draining && n->config.local != n->config.root &&
            source != n->config.local && target != n->config.local) {
            memset(n->wanted[slot], 0, sizeof(n->wanted[slot]));
            continue;
        }
        existing =
            n->config.local == n->config.root ? flow(n, source, target) : -1;
        rc = n->config.local == n->config.root
                 ? ninlil_coordinator_route(&n->coordinator, source, target,
                                            now, &plan)
                 : ninlil_node_route(n, source, target, now, &plan);
        if (rc == NINLIL_OK &&
            (n->config.local != n->config.root ||
             (existing >= 0 &&
              n->effective_notified[existing] == all(&plan)))) {
            memset(n->wanted[slot], 0, sizeof(n->wanted[slot]));
            continue;
        }
        if (n->config.local != n->config.root) {
            uint8_t request[13];
            ninlil_node_put(request, source, 2u);
            ninlil_node_put(request + 2, target, 2u);
            ninlil_node_put(request + 4, n->wanted_token[slot], 8u);
            request[12] = n->wanted_force[slot];
            return ninlil_node_control_send(
                n, n->config.root, NODE_FLOW_REQUEST, request, sizeof(request));
        }
        if (existing >= 0 &&
            n->coordinator.flows[existing].active.valid_until_ms > now)
            return active(n, (unsigned int)existing);
        n->planning = 1u;
        rc = ninlil_coordinator_tick(&n->coordinator, source, target, 0u, now,
                                     NINLIL_TIME_RESTART_SAFE);
        n->planning = 0u;
        return rc;
    }
    if (n->config.local == n->config.root)
        for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
            ninlil_network_plan *p = &n->coordinator.flows[i].active;
            if (p->epoch && p->valid_until_ms > now &&
                n->effective_notified[i] != all(p))
                return active(n, i);
        }
    return NINLIL_OK;
}
