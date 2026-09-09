#include "ninlil_node_internal.h"
#include <string.h>

int ninlil_node_plan_position(const ninlil_network_plan *p, uint16_t address)
{
    unsigned int i;
    for (i = 0u; i < p->path.count; i++)
        if (p->path.nodes[i] == address)
            return (int)i;
    return -1;
}
int ninlil_node_local_plan(ninlil_node *n, uint16_t source, uint16_t target,
                           int create)
{
    unsigned int i;
    int empty = -1;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_plan *p = &n->local_plans[i];
        if (!p->epoch) {
            empty = (int)i;
            continue;
        }
        if (p->path.nodes[0] == source &&
            p->path.nodes[p->path.count - 1u] == target)
            return (int)i;
    }
    return create ? empty : -1;
}

static int remember(ninlil_node *n, const ninlil_network_plan *p)
{
    int slot;
    if (!ninlil_network_plan_valid(p))
        return NINLIL_ERR_CORRUPT;
    if (ninlil_node_plan_position(p, n->config.local) < 0)
        return n->config.local == n->config.root ? NINLIL_OK
                                                 : NINLIL_ERR_CORRUPT;
    slot = ninlil_node_local_plan(n, p->path.nodes[0],
                                  p->path.nodes[p->path.count - 1u], 1);
    if (slot < 0)
        return NINLIL_ERR_CAPACITY;
    if (p->epoch > n->local_plan_epoch)
        n->local_plan_epoch = p->epoch;
    if (p->phase == NINLIL_PLAN_STAGED) {
        if (n->prepared.epoch && n->prepared.epoch != p->epoch)
            return NINLIL_ERR_CORRUPT;
        n->prepared = *p;
    } else if (p->phase == NINLIL_PLAN_COMMITTED ||
               p->phase == NINLIL_PLAN_EFFECTIVE) {
        ninlil_network_plan *old = &n->local_plans[slot];
        if (old->epoch > p->epoch ||
            (old->epoch == p->epoch && !ninlil_node_same_plan(old, p)))
            return NINLIL_ERR_CORRUPT;
        *old = *p;
        if (n->prepared.epoch == p->epoch)
            memset(&n->prepared, 0, sizeof(n->prepared));
    } else {
        if (n->local_plans[slot].epoch &&
            (n->local_plans[slot].epoch == p->epoch ||
             (p->phase == NINLIL_PLAN_RETIRED &&
              n->local_plans[slot].epoch < p->epoch))) {
            memset(&n->local_plans[slot], 0, sizeof(n->local_plans[slot]));
            n->local_ready[slot] = 0u;
            n->retired[slot] = 0u;
        }
        if (n->prepared.epoch == p->epoch)
            memset(&n->prepared, 0, sizeof(n->prepared));
    }
    return NINLIL_OK;
}

int ninlil_node_plan_restore(void *ctx, const ninlil_network_plan *p)
{
    ninlil_node *n = ctx;
    int rc = n->config.local == n->config.root
                 ? ninlil_coordinator_restore(&n->coordinator, p)
                 : NINLIL_OK;
    return rc == NINLIL_OK ? remember(n, p) : rc;
}

int ninlil_node_expire_plans(ninlil_node *n, uint64_t now)
{
    ninlil_network_plan expired;
    unsigned int i;
    int rc;
    if (n->prepared.epoch && n->prepared.valid_until_ms <= now) {
        expired = n->prepared;
        expired.phase = NINLIL_PLAN_ABORTED;
        rc = n->config.local == n->config.root
                 ? NINLIL_OK
                 : ninlil_node_plan_commit(n, &expired);
        return rc == NINLIL_OK ? remember(n, &expired) : rc;
    }
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_plan *p = &n->local_plans[i];
        if (!p->epoch || p->valid_until_ms > now)
            continue;
        expired = *p;
        expired.phase = NINLIL_PLAN_ABORTED;
        rc = n->config.local == n->config.root
                 ? NINLIL_OK
                 : ninlil_node_plan_commit(n, &expired);
        return rc == NINLIL_OK ? remember(n, &expired) : rc;
    }
    return NINLIL_OK;
}

static int valid_live(ninlil_node *n, const ninlil_network_plan *p)
{
    uint64_t now;
    unsigned int i;
    if (!n->joined || !ninlil_network_plan_valid(p) ||
        p->profile != n->config.permitted_profile ||
        ninlil_node_plan_position(p, n->config.local) < 0 ||
        ninlil_node_lease(n, &now) != NINLIL_OK || p->valid_until_ms <= now ||
        p->valid_until_ms - now > NINLIL_NETWORK_LEASE_MAX_MS)
        return NINLIL_ERR_STATE;
    for (i = 0u; i < p->path.count; i++) {
        ninlil_peer_policy policy;
        if (ninlil_node_policy(n, p->path.nodes[i], &policy) != NINLIL_OK ||
            policy.session_membership_epoch != p->path.membership_epochs[i] ||
            (i && i + 1u < p->path.count &&
             (!(policy.capabilities & NINLIL_CAP_RELAY_CUSTODY) ||
              policy.role == NINLIL_ROLE_BATTERY_LEAF)))
            return NINLIL_ERR_UNAUTHORIZED;
    }
    return NINLIL_OK;
}
static int save(ninlil_node *n, const ninlil_network_plan *p)
{
    int rc = n->config.local == n->config.root ? NINLIL_OK
                                               : ninlil_node_plan_commit(n, p);
    return rc == NINLIL_OK ? remember(n, p) : rc;
}

int ninlil_node_prepare(ninlil_node *n, const ninlil_network_plan *p)
{
    int slot, at, rc = valid_live(n, p);
    if (rc != NINLIL_OK || p->phase != NINLIL_PLAN_STAGED)
        return rc != NINLIL_OK ? rc : NINLIL_ERR_STATE;
    at = ninlil_node_plan_position(p, n->config.local);
    if (n->relay.draining && at > 0 && (unsigned int)at + 1u < p->path.count)
        return NINLIL_ERR_BUSY;
    if (n->prepared.epoch == p->epoch)
        return ninlil_node_same_plan(&n->prepared, p) ? NINLIL_OK
                                                      : NINLIL_ERR_CONFLICT;
    if (n->prepared.epoch || p->epoch <= n->local_plan_epoch)
        return NINLIL_ERR_STATE;
    slot = ninlil_node_local_plan(n, p->path.nodes[0],
                                  p->path.nodes[p->path.count - 1u], 1);
    if (slot < 0)
        return NINLIL_ERR_CAPACITY;
    return save(n, p);
}

static int context(ninlil_node *n, uint16_t peer, int hop, uint8_t out[16])
{
    int index = ninlil_node_index(n, peer);
    if (index < 0 || !n->peers[index].sessions[hop].ready)
        return NINLIL_ERR_STATE;
    memcpy(out, n->peers[index].sessions[hop].material.fingerprint, 16u);
    return NINLIL_OK;
}

int ninlil_node_apply(ninlil_node *n, const ninlil_network_plan *p,
                      uint8_t proof[48])
{
    uint64_t now;
    int slot, at, rc = valid_live(n, p);
    if (rc != NINLIL_OK || (p->phase != NINLIL_PLAN_COMMITTED &&
                            p->phase != NINLIL_PLAN_EFFECTIVE))
        return rc != NINLIL_OK ? rc : NINLIL_ERR_STATE;
    slot = ninlil_node_local_plan(n, p->path.nodes[0],
                                  p->path.nodes[p->path.count - 1u], 1);
    if (slot < 0 || n->retired[slot] >= p->epoch)
        return NINLIL_ERR_STATE;
    if (n->local_plans[slot].epoch != p->epoch &&
        (n->prepared.epoch != p->epoch ||
         !ninlil_node_same_plan(&n->prepared, p)))
        return NINLIL_ERR_STATE;
    if (n->local_plans[slot].epoch != p->epoch && p->prepare_until_ms &&
        (ninlil_node_lease(n, &now) != NINLIL_OK || now >= p->prepare_until_ms))
        return NINLIL_ERR_EXPIRED;
    if (n->local_plans[slot].epoch && n->local_plans[slot].epoch != p->epoch &&
        n->local_ready[slot] && n->retired[slot] < n->local_plans[slot].epoch) {
        rc = ninlil_node_lease(n, &now);
        if (rc != NINLIL_OK || now < n->local_plans[slot].valid_until_ms)
            return NINLIL_ERR_STATE;
    }
    at = ninlil_node_plan_position(p, n->config.local);
    memset(proof, 0, 48u);
    if (at > 0)
        rc = context(n, p->path.nodes[(unsigned int)at - 1u], 1, proof);
    if (rc == NINLIL_OK && (unsigned int)at + 1u < p->path.count)
        rc = context(n, p->path.nodes[(unsigned int)at + 1u], 1, proof + 16);
    if (rc == NINLIL_OK && (at == 0 || (unsigned int)at + 1u == p->path.count))
        rc = context(n, p->path.nodes[at == 0 ? p->path.count - 1u : 0u], 0,
                     proof + 32);
    if (rc != NINLIL_OK)
        return rc;
    if (n->local_plans[slot].epoch != p->epoch) {
        rc = save(n, p);
        if (rc != NINLIL_OK)
            return rc;
        n->local_ready[slot] = 0u;
        memset(n->plan_bindings[slot], 0, sizeof(n->plan_bindings[slot]));
    } else if (!ninlil_node_same_plan(&n->local_plans[slot], p))
        return NINLIL_ERR_CONFLICT;
    /* Only the same epoch may retain EFFECTIVE on duplicate APPLY. A new
     * plan and a replaced session both require fresh authority confirmation. */
    n->local_ready[slot] |= 1u;
    return NINLIL_OK;
}

int ninlil_node_effective(ninlil_node *n, const ninlil_network_plan *p,
                          const uint8_t *bindings, size_t length)
{
    uint8_t proof[48];
    int slot, at, rc = valid_live(n, p);
    if (rc != NINLIL_OK || p->phase != NINLIL_PLAN_EFFECTIVE ||
        length != (size_t)p->path.count * 16u)
        return rc != NINLIL_OK ? rc : NINLIL_ERR_INVALID;
    slot = ninlil_node_local_plan(n, p->path.nodes[0],
                                  p->path.nodes[p->path.count - 1u], 0);
    if (slot < 0 || !(n->local_ready[slot] & 1u) ||
        !ninlil_node_same_plan(&n->local_plans[slot], p))
        return NINLIL_ERR_STATE;
    /* Re-read current contexts: a neighbor may have reauthenticated after our
     * APPLIED report and before this coordinator confirmation arrived. */
    rc = ninlil_node_apply(n, p, proof);
    if (rc != NINLIL_OK)
        return rc;
    at = ninlil_node_plan_position(p, n->config.local);
    if ((at > 0 &&
         memcmp(proof, bindings + ((unsigned int)at - 1u) * 16u, 16u) != 0) ||
        ((unsigned int)at + 1u < p->path.count &&
         memcmp(proof + 16, bindings + (unsigned int)at * 16u, 16u) != 0) ||
        ((at == 0 || (unsigned int)at + 1u == p->path.count) &&
         memcmp(proof + 32, bindings + (p->path.count - 1u) * 16u, 16u) != 0))
        return NINLIL_ERR_STATE;
    if (n->local_plans[slot].phase != NINLIL_PLAN_EFFECTIVE) {
        rc = save(n, p);
        if (rc != NINLIL_OK)
            return rc;
    }
    memcpy(n->plan_bindings[slot], bindings, length);
    n->local_ready[slot] = 3u;
    for (unsigned int i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
        if (n->wanted[i][0] == p->path.nodes[0] &&
            n->wanted[i][1] == p->path.nodes[p->path.count - 1u]) {
            memset(n->wanted[i], 0, sizeof(n->wanted[i]));
            n->wanted_force[i] = 0u;
        }
    return NINLIL_OK;
}

int ninlil_node_release(ninlil_node *n, uint64_t epoch, uint64_t replacement)
{
    unsigned int i;
    if (!epoch || replacement <= epoch)
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
        if (n->local_plans[i].epoch == epoch) {
            n->retired[i] = epoch;
            n->local_ready[i] = 0u;
            /* No custody is retired. Boot also starts without route rights;
             * delayed old-session traffic cannot undo this release. */
            return NINLIL_OK;
        }
    return NINLIL_ERR_NOT_FOUND;
}

void ninlil_node_want(ninlil_node *n, uint16_t source, uint16_t target)
{
    unsigned int i;
    int empty = -1;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        if (n->wanted[i][0] == source && n->wanted[i][1] == target)
            return;
        if (!n->wanted[i][0])
            empty = (int)i;
    }
    if (empty >= 0) {
        if (n->request_sequence == UINT64_MAX) {
            n->status.fault = NINLIL_ERR_STATE;
            return;
        }
        n->wanted[empty][0] = source;
        n->wanted[empty][1] = target;
        n->wanted_token[empty] = ++n->request_sequence;
        n->wanted_force[empty] = 0u;
    }
}

void ninlil_node_need_reconcile(ninlil_node *n, uint16_t source,
                                uint16_t target)
{
    unsigned int i;
    ninlil_node_want(n, source, target);
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
        if (n->wanted[i][0] == source && n->wanted[i][1] == target &&
            !n->wanted_force[i]) {
            if (n->request_sequence == UINT64_MAX) {
                n->status.fault = NINLIL_ERR_STATE;
                return;
            }
            n->wanted_token[i] = ++n->request_sequence;
            n->wanted_force[i] = 1u;
        }
}

int ninlil_node_route(void *ctx, uint16_t source, uint16_t target, uint64_t now,
                      ninlil_network_plan *out)
{
    ninlil_node *n = ctx;
    ninlil_network_plan *p;
    uint64_t current;
    int slot, rc;
    if (ninlil_node_lease(n, &current) != NINLIL_OK || now > current ||
        !n->joined)
        return NINLIL_ERR_STATE;
    slot = ninlil_node_local_plan(n, source, target, 0);
    if (slot < 0 || !n->local_ready[slot] ||
        (source == n->config.local && n->local_ready[slot] != 3u)) {
        ninlil_node_want(n, source, target);
        return NINLIL_ERR_NOT_FOUND;
    }
    p = &n->local_plans[slot];
    rc = valid_live(n, p);
    if (rc == NINLIL_OK && n->config.local == n->config.root)
        rc = ninlil_coordinator_route(&n->coordinator, source, target, current,
                                      out);
    else if (rc == NINLIL_OK)
        *out = *p;
    if (rc == NINLIL_OK && out->epoch != p->epoch)
        rc = NINLIL_ERR_STATE;
    if (rc != NINLIL_OK)
        ninlil_node_want(n, source, target);
    return rc;
}
