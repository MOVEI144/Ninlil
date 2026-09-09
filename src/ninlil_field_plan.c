#include "ninlil_node_internal.h"
#include <string.h>

int ninlil_node_prepare_window(const ninlil_network_plan *p, uint64_t now)
{
    /* Do not commit a new lease after participants may have expired prepare.
     * Leave one route-service turn per participant after the clock bound.
     * A closed window waits for normal expiry; no deadline is extended. */
    return p->phase != NINLIL_PLAN_STAGED || !p->prepare_until_ms ||
           (p->prepare_until_ms > now &&
            p->prepare_until_ms - now >
                NINLIL_LEASE_SYNC_ERROR_BOUND_MS + 2000u * p->path.count);
}

void ninlil_node_plan_rejected(ninlil_node *n, const ninlil_network_plan *p)
{
    uint64_t now;
    int slot = ninlil_node_local_plan(n, p->path.nodes[0],
                                      p->path.nodes[p->path.count - 1u], 0);
    /* A delayed expired/older notice cannot invalidate a newer proof round.
     * Only a live matching local plan can need its bindings reconciled. */
    if (slot >= 0 && ninlil_node_same_plan(&n->local_plans[slot], p) &&
        ninlil_node_lease(n, &now) == NINLIL_OK && p->valid_until_ms > now)
        ninlil_node_need_reconcile(n, p->path.nodes[0],
                                   p->path.nodes[p->path.count - 1u]);
}

int ninlil_node_next_notification(ninlil_node *n, uint64_t now)
{
    for (unsigned int i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        unsigned int slot = n->notify_cursor;
        const ninlil_network_flow *f = &n->coordinator.flows[slot];
        const ninlil_network_plan *p = &f->active;
        uint8_t all = (uint8_t)((1u << p->path.count) - 1u);
        n->notify_cursor = (uint8_t)((slot + 1u) % NINLIL_NETWORK_FLOWS_MAX);
        if (p->epoch && p->phase == NINLIL_PLAN_EFFECTIVE &&
            p->valid_until_ms > now && f->reconciled == all &&
            n->effective_epoch[slot] == p->epoch &&
            n->effective_notified[slot] != all)
            return (int)slot;
    }
    return -1;
}

int ninlil_node_plan_frame_current(ninlil_node *n, uint16_t peer,
                                   const uint8_t *plain, size_t size)
{
    ninlil_network_plan p;
    const ninlil_network_plan *pending = &n->coordinator.pending;
    uint64_t now;
    uint8_t kind;
    if (!plain || !size)
        return NINLIL_ERR_INVALID;
    kind = plain[0];
    if (kind != NODE_PREPARE && kind != NODE_APPLY && kind != NODE_EFFECTIVE)
        return NINLIL_OK;
    if (n->config.local != n->config.root || size < 99u ||
        ninlil_network_plan_decode(plain + 1, NINLIL_NETWORK_PLAN_MAX, &p) !=
            NINLIL_OK ||
        size !=
            99u + (kind == NODE_EFFECTIVE ? (size_t)p.path.count * 16u : 0u))
        return NINLIL_ERR_INVALID;
    if (ninlil_node_lease(n, &now) != NINLIL_OK || p.valid_until_ms <= now ||
        ninlil_node_plan_position(&p, peer) < 0 ||
        (kind == NODE_APPLY && p.phase != NINLIL_PLAN_COMMITTED &&
         p.phase != NINLIL_PLAN_EFFECTIVE))
        return NINLIL_ERR_STATE;
    /* Queue admission is not transmission. Drop obsolete preparation/apply
     * retries after a phase transition instead of consuming more Relay time. */
    if (kind == NODE_PREPARE)
        return p.phase == NINLIL_PLAN_STAGED && pending->epoch == p.epoch &&
                       pending->phase == NINLIL_PLAN_STAGED &&
                       !(n->coordinator.prepared_live &
                         (1u << (unsigned int)ninlil_node_plan_position(
                              &p, peer))) &&
                       ninlil_node_same_plan(pending, &p)
                   ? NINLIL_OK
                   : NINLIL_ERR_STATE;
    if (kind == NODE_APPLY && pending->epoch == p.epoch &&
        pending->phase == NINLIL_PLAN_COMMITTED &&
        ninlil_node_same_plan(pending, &p))
        return NINLIL_OK;
    for (unsigned int i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        const ninlil_network_flow *f = &n->coordinator.flows[i];
        uint8_t all = (uint8_t)((1u << p.path.count) - 1u);
        int at = ninlil_node_plan_position(&p, peer);
        if (f->active.epoch != p.epoch ||
            f->active.phase != NINLIL_PLAN_EFFECTIVE ||
            !ninlil_node_same_plan(&f->active, &p))
            continue;
        if (kind == NODE_APPLY)
            return f->reconciled != all || n->effective_epoch[i] != p.epoch
                       ? NINLIL_OK
                       : NINLIL_ERR_STATE;
        return p.phase == NINLIL_PLAN_EFFECTIVE && f->reconciled == all &&
                       n->effective_epoch[i] == p.epoch &&
                       !(n->effective_notified[i] & (uint8_t)(1u << at)) &&
                       !memcmp(plain + 99, n->effective_bindings[i],
                               (size_t)p.path.count * 16u)
                   ? NINLIL_OK
                   : NINLIL_ERR_STATE;
    }
    return NINLIL_ERR_STATE;
}

int ninlil_node_plan_in_progress(ninlil_node *n, uint16_t source,
                                 uint16_t target, uint64_t now)
{
    const ninlil_network_plan *p = &n->prepared;
    int slot = ninlil_node_local_plan(n, source, target, 0);
    if (p->epoch && p->valid_until_ms > now && p->path.nodes[0] == source &&
        p->path.nodes[p->path.count - 1u] == target)
        return 1;
    /* Receiving PREPARE/APPLY already acknowledges the flow request. Root
     * retries unfinished confirmation; do not fill its Relay with duplicate
     * requests while waiting. Restart/session loss clears local_ready, and
     * lease expiry always permits requesting again. No custody is released. */
    return slot >= 0 && n->local_ready[slot] == 1u &&
           n->local_plans[slot].valid_until_ms > now;
}

int ninlil_node_same_plan(const ninlil_network_plan *a,
                          const ninlil_network_plan *b)
{
    ninlil_network_plan x = *a, y = *b;
    uint8_t left[NINLIL_NETWORK_PLAN_MAX], right[NINLIL_NETWORK_PLAN_MAX];
    if (!ninlil_network_plan_valid(a) || !ninlil_network_plan_valid(b))
        return 0;
    /* NP2 preparation authorizes a bounded lease beginning at commit.
     * Once committed, retries must keep the exact committed expiration. */
    if (x.prepare_until_ms && x.prepare_until_ms == y.prepare_until_ms &&
        (x.phase == NINLIL_PLAN_STAGED || y.phase == NINLIL_PLAN_STAGED))
        x.valid_until_ms = y.valid_until_ms = x.prepare_until_ms;
    x.phase = y.phase = NINLIL_PLAN_ABORTED;
    x.prepared = x.applied = y.prepared = y.applied = 0u;
    return ninlil_network_plan_encode(&x, left, sizeof(left)) == sizeof(left) &&
           ninlil_network_plan_encode(&y, right, sizeof(right)) ==
               sizeof(right) &&
           !memcmp(left, right, sizeof(left));
}
