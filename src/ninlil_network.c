#include "ninlil_network_internal.h"

#include <string.h>

ninlil_network_flow *ninlil_network_flow_find(ninlil_coordinator *c,
                                              const ninlil_network_path *p,
                                              int create)
{
    unsigned int i;
    ninlil_network_flow *empty = NULL;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_flow *f = &c->flows[i];
        if (f->active.epoch == 0u) {
            empty = f;
            continue;
        }
        if (f->active.path.nodes[0] == p->nodes[0] &&
            f->active.path.nodes[f->active.path.count - 1u] ==
                p->nodes[p->count - 1u])
            return f;
    }
    return create ? empty : NULL;
}

int ninlil_network_snapshot_epochs(ninlil_coordinator *c,
                                   ninlil_network_path *p)
{
    unsigned int i;
    for (i = 0u; i < p->count; i++) {
        ninlil_peer_policy grant;
        memset(&grant, 0, sizeof(grant));
        if (c->policy(c->policy_ctx, p->nodes[i], &grant) != NINLIL_OK ||
            grant.membership_epoch == 0u ||
            grant.membership_epoch != grant.session_membership_epoch)
            return 0;
        p->membership_epochs[i] = grant.membership_epoch;
    }
    return 1;
}

int ninlil_network_path_valid(const ninlil_network_path *p)
{
    unsigned int i, j;
    if (!p || p->count < 2u || p->count > NINLIL_NETWORK_PATH_MAX)
        return 0;
    for (i = 0u; i < p->count; i++) {
        if (p->nodes[i] == 0u || p->nodes[i] == UINT16_MAX)
            return 0;
        for (j = 0u; j < i; j++)
            if (p->nodes[i] == p->nodes[j])
                return 0;
    }
    return 1;
}

int ninlil_network_policy(ninlil_coordinator *c, uint16_t node, int relay,
                          uint64_t epoch)
{
    ninlil_peer_policy p;
    memset(&p, 0, sizeof(p));
    if (c->policy(c->policy_ctx, node, &p) != NINLIL_OK ||
        p.membership_epoch == 0u ||
        p.membership_epoch != p.session_membership_epoch ||
        (epoch != 0u && epoch != p.membership_epoch))
        return 0;
    return !relay || (p.role != NINLIL_ROLE_BATTERY_LEAF &&
                      (p.capabilities & NINLIL_CAP_RELAY_CUSTODY) != 0u);
}

int ninlil_coordinator_open(ninlil_coordinator *c, ninlil_network_node *nodes,
                            uint16_t node_capacity, ninlil_network_edge *edges,
                            uint16_t edge_capacity, uint32_t profile,
                            ninlil_policy_lookup lookup, void *policy_ctx,
                            ninlil_plan_commit_fn writer, void *commit_ctx)
{
    if (!c || !nodes || !edges || !lookup || !writer || profile == 0u ||
        node_capacity < 2u || node_capacity > NINLIL_NETWORK_NODES_MAX ||
        edge_capacity == 0u || edge_capacity > NINLIL_NETWORK_EDGES_MAX)
        return NINLIL_ERR_INVALID;
    memset(c, 0, sizeof(*c));
    memset(nodes, 0, sizeof(*nodes) * node_capacity);
    memset(edges, 0, sizeof(*edges) * edge_capacity);
    c->nodes = nodes;
    c->node_capacity = node_capacity;
    c->edges = edges;
    c->edge_capacity = edge_capacity;
    c->permitted_profile = profile;
    c->policy = lookup;
    c->policy_ctx = policy_ctx;
    c->commit = writer;
    c->commit_ctx = commit_ctx;
    c->enabled = 1u;
    return NINLIL_OK;
}

static int persist(ninlil_coordinator *c, const ninlil_network_plan *p)
{
    if (!ninlil_network_plan_valid(p))
        return NINLIL_ERR_INVALID;
    int rc = c->commit(c->commit_ctx, p);
    if (rc != NINLIL_OK)
        c->poisoned = 1u;
    else
        c->last_record = *p;
    return rc;
}

static uint8_t all(const ninlil_network_plan *p)
{
    return (uint8_t)((1u << p->path.count) - 1u);
}

static void clear_pending(ninlil_coordinator *c)
{
    memset(&c->pending, 0, sizeof(c->pending));
    c->prepared_live = c->applied_live = 0u;
}

int ninlil_coordinator_stage(ninlil_coordinator *c,
                             const ninlil_network_path *path, uint64_t now,
                             uint64_t until, ninlil_time_quality quality)
{
    ninlil_network_plan p;
    uint64_t cost;
    int rc;
    if (!c || c->poisoned || !c->enabled || !ninlil_network_path_valid(path) ||
        quality != NINLIL_TIME_RESTART_SAFE || until <= now ||
        until - now > NINLIL_NETWORK_LEASE_MAX_MS ||
        c->last_epoch == UINT64_MAX || c->pending.epoch != 0u ||
        now < c->last_change_ms)
        return NINLIL_ERR_STATE;
    cost = ninlil_network_path_cost(c, path, now);
    if (cost == UINT64_MAX)
        return NINLIL_ERR_NOT_FOUND;
    memset(&p, 0, sizeof(p));
    p.path = *path;
    for (unsigned int unused = p.path.count; unused < NINLIL_NETWORK_PATH_MAX;
         unused++) {
        p.path.nodes[unused] = 0u;
        p.path.membership_epochs[unused] = 0u;
    }
    if (!ninlil_network_snapshot_epochs(c, &p.path))
        return NINLIL_ERR_UNAUTHORIZED;
    {
        ninlil_network_flow *f = ninlil_network_flow_find(c, path, 1);
        if (!f)
            return NINLIL_ERR_CAPACITY;
        c->active = f->active;
    }
    p.path.cost_us = cost;
    p.epoch = c->last_epoch + 1u;
    p.valid_until_ms = until;
    p.profile = c->permitted_profile;
    p.rto_ms = (uint32_t)(cost / 500u > 30000u ? 30000u : cost / 500u);
    if (p.rto_ms < 100u)
        p.rto_ms = 100u;
    p.phase = NINLIL_PLAN_STAGED;
    rc = persist(c, &p);
    if (rc == NINLIL_OK) {
        c->pending = p;
        c->prepared_live = c->applied_live = 0u;
        c->last_epoch = p.epoch;
    }
    return rc;
}

static int acknowledge(ninlil_coordinator *c, uint16_t peer, uint64_t epoch,
                       int applied)
{
    ninlil_network_plan p;
    uint8_t bit, *live;
    unsigned int i;
    int rc;
    if (!c || c->poisoned || epoch == 0u)
        return NINLIL_ERR_STATE;
    if (c->pending.epoch != epoch) {
        for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
            unsigned int j;
            ninlil_network_flow *f = &c->flows[i];
            if (f->active.epoch != epoch)
                continue;
            for (j = 0u; j < f->active.path.count; j++)
                if (f->active.path.nodes[j] == peer &&
                    ninlil_network_policy(c, peer, 0,
                                          f->active.path.membership_epochs[j]))
                    return NINLIL_OK;
        }
        return NINLIL_ERR_STATE;
    }
    if (!applied && c->pending.phase == NINLIL_PLAN_COMMITTED) {
        for (i = 0u; i < c->pending.path.count; i++)
            if (c->pending.path.nodes[i] == peer &&
                ninlil_network_policy(c, peer, 0,
                                      c->pending.path.membership_epochs[i]))
                return NINLIL_OK;
    }
    if (c->pending.epoch != epoch ||
        c->pending.phase !=
            (applied ? NINLIL_PLAN_COMMITTED : NINLIL_PLAN_STAGED))
        return NINLIL_ERR_STATE;
    p = c->pending;
    for (i = 0u; i < p.path.count; i++)
        if (p.path.nodes[i] == peer)
            break;
    if (i == p.path.count ||
        !ninlil_network_policy(c, peer, 0, p.path.membership_epochs[i]))
        return NINLIL_ERR_UNAUTHORIZED;
    bit = (uint8_t)(1u << i);
    live = applied ? &c->applied_live : &c->prepared_live;
    if ((*live & bit) != 0u)
        return NINLIL_OK;
    if (applied)
        p.applied |= bit;
    else
        p.prepared |= bit;
    if (applied && (uint8_t)(*live | bit) == all(&p))
        p.phase = NINLIL_PLAN_EFFECTIVE;
    rc = p.phase == c->pending.phase && p.applied == c->pending.applied &&
                 p.prepared == c->pending.prepared
             ? NINLIL_OK
             : persist(c, &p);
    if (rc == NINLIL_OK) {
        *live |= bit;
        c->pending = p;
        if (p.phase == NINLIL_PLAN_EFFECTIVE) {
            ninlil_network_flow *f = ninlil_network_flow_find(c, &p.path, 1);
            if (!f) {
                c->poisoned = 1u;
                return NINLIL_ERR_CORRUPT;
            }
            f->active = p;
            f->reconciled = all(&p);
            c->active = p;
            clear_pending(c);
        }
    }
    return rc;
}

int ninlil_coordinator_prepared(ninlil_coordinator *c, uint16_t peer,
                                uint64_t epoch)
{
    return acknowledge(c, peer, epoch, 0);
}
int ninlil_coordinator_applied(ninlil_coordinator *c, uint16_t peer,
                               uint64_t epoch)
{
    return acknowledge(c, peer, epoch, 1);
}

int ninlil_coordinator_activate(ninlil_coordinator *c, uint64_t now,
                                ninlil_time_quality quality, int released)
{
    ninlil_network_plan p;
    ninlil_network_flow *old;
    int rc;
    if (!c || c->poisoned || quality != NINLIL_TIME_RESTART_SAFE ||
        c->pending.phase != NINLIL_PLAN_STAGED || c->pending.epoch == 0u ||
        c->pending.prepared != all(&c->pending) ||
        c->prepared_live != all(&c->pending) || now < c->last_change_ms ||
        now >= c->pending.valid_until_ms)
        return NINLIL_ERR_STATE;
    old = ninlil_network_flow_find(c, &c->pending.path, 0);
    if (old && !released && now < old->active.valid_until_ms)
        return NINLIL_ERR_STATE;
    for (unsigned int i = 0u; i < c->pending.path.count; i++)
        if (!ninlil_network_policy(c, c->pending.path.nodes[i],
                                   i > 0u && i + 1u < c->pending.path.count,
                                   c->pending.path.membership_epochs[i]))
            return NINLIL_ERR_UNAUTHORIZED;
    p = c->pending;
    p.phase = NINLIL_PLAN_COMMITTED;
    rc = persist(c, &p);
    if (rc == NINLIL_OK) {
        c->pending = p;
        c->last_change_ms = now;
    }
    return rc;
}

int ninlil_coordinator_abort(ninlil_coordinator *c)
{
    ninlil_network_plan p;
    int rc;
    if (!c || c->poisoned || c->pending.epoch == 0u ||
        c->pending.phase != NINLIL_PLAN_STAGED)
        return NINLIL_ERR_STATE;
    p = c->pending;
    p.phase = NINLIL_PLAN_ABORTED;
    rc = persist(c, &p);
    if (rc == NINLIL_OK)
        clear_pending(c);
    return rc;
}

int ninlil_coordinator_withdraw(ninlil_coordinator *c, uint64_t epoch,
                                uint64_t now, ninlil_time_quality quality,
                                int released)
{
    ninlil_network_plan p;
    int rc;
    if (!c || c->poisoned || !epoch || c->pending.epoch != epoch ||
        (c->pending.phase != NINLIL_PLAN_STAGED &&
         c->pending.phase != NINLIL_PLAN_COMMITTED) ||
        quality != NINLIL_TIME_RESTART_SAFE || now < c->last_change_ms ||
        (c->pending.phase == NINLIL_PLAN_COMMITTED && !released &&
         now < c->pending.valid_until_ms))
        return NINLIL_ERR_STATE;
    p = c->pending;
    p.phase = NINLIL_PLAN_ABORTED;
    rc = persist(c, &p);
    if (rc == NINLIL_OK)
        clear_pending(c);
    return rc;
}

int ninlil_coordinator_restore(ninlil_coordinator *c,
                               const ninlil_network_plan *p)
{
    if (!c || !p || c->poisoned || !ninlil_network_plan_valid(p) ||
        p->epoch == 0u || p->epoch < c->last_epoch ||
        p->profile != c->permitted_profile || p->rto_ms < 100u ||
        p->rto_ms > 30000u || p->phase < NINLIL_PLAN_STAGED ||
        p->phase > NINLIL_PLAN_RETIRED ||
        (p->prepared & (uint8_t)~all(p)) != 0u ||
        (p->applied & (uint8_t)~all(p)) != 0u ||
        (p->phase == NINLIL_PLAN_EFFECTIVE &&
         (p->prepared != all(p) || p->applied != all(p))))
        return NINLIL_ERR_CORRUPT;
    if (p->epoch == c->last_epoch && c->last_epoch != 0u) {
        const ninlil_network_plan *old = &c->last_record;
        if (old->path.count != p->path.count ||
            old->valid_until_ms != p->valid_until_ms ||
            old->rto_ms != p->rto_ms || old->path.cost_us != p->path.cost_us ||
            memcmp(old->path.nodes, p->path.nodes, sizeof(p->path.nodes)) !=
                0 ||
            memcmp(old->path.membership_epochs, p->path.membership_epochs,
                   sizeof(p->path.membership_epochs)) != 0 ||
            (old->prepared & p->prepared) != old->prepared ||
            (old->applied & p->applied) != old->applied ||
            p->phase < old->phase ||
            (old->phase == NINLIL_PLAN_EFFECTIVE && p->phase != old->phase))
            return NINLIL_ERR_CORRUPT;
    }
    if (p->phase == NINLIL_PLAN_RETIRED) {
        ninlil_network_flow *f = ninlil_network_flow_find(c, &p->path, 0);
        if (!f || f->active.epoch >= p->epoch || c->pending.epoch != 0u ||
            f->active.path.count != p->path.count ||
            memcmp(f->active.path.nodes, p->path.nodes,
                   sizeof(p->path.nodes)) != 0 ||
            memcmp(f->active.path.membership_epochs, p->path.membership_epochs,
                   sizeof(p->path.membership_epochs)) != 0)
            return NINLIL_ERR_CORRUPT;
        if (c->active.epoch == f->active.epoch)
            memset(&c->active, 0, sizeof(c->active));
        memset(f, 0, sizeof(*f));
    }
    c->last_epoch = p->epoch;
    c->last_record = *p;
    if (p->phase == NINLIL_PLAN_EFFECTIVE) {
        ninlil_network_flow *f = ninlil_network_flow_find(c, &p->path, 1);
        if (!f)
            return NINLIL_ERR_CAPACITY;
        f->active = *p;
        f->reconciled = 0u;
        c->active = *p;
        clear_pending(c);
    } else if (p->phase == NINLIL_PLAN_ABORTED ||
               p->phase == NINLIL_PLAN_RETIRED)
        clear_pending(c);
    else {
        c->pending = *p;
        c->prepared_live = c->applied_live = 0u;
    }
    return NINLIL_OK;
}

void ninlil_coordinator_enable(ninlil_coordinator *c, int enabled)
{
    if (c)
        c->enabled = enabled ? 1u : 0u;
}

void ninlil_coordinator_disconnect(ninlil_coordinator *c, uint16_t peer)
{
    unsigned int i, j;
    if (!c || !peer)
        return;
    for (i = 0u; i < c->pending.path.count; i++)
        if (c->pending.path.nodes[i] == peer) {
            c->prepared_live = c->applied_live = 0u;
        }
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
        for (j = 0u; j < c->flows[i].active.path.count; j++)
            if (c->flows[i].active.path.nodes[j] == peer)
                c->flows[i].reconciled = 0u;
    for (i = 0u; i < c->edge_capacity; i++)
        if (c->edges[i].from == peer || c->edges[i].to == peer)
            c->edges[i].used = 0u;
}

int ninlil_coordinator_reconcile(ninlil_coordinator *c, uint16_t peer,
                                 uint64_t epoch)
{
    unsigned int i, j;
    if (!c || c->poisoned || !ninlil_network_policy(c, peer, 0, 0u))
        return NINLIL_ERR_UNAUTHORIZED;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_flow *f = &c->flows[i];
        if (f->active.epoch != epoch || epoch == 0u)
            continue;
        for (j = 0u; j < f->active.path.count; j++)
            if (f->active.path.nodes[j] == peer) {
                if (!ninlil_network_policy(c, peer, 0,
                                           f->active.path.membership_epochs[j]))
                    return NINLIL_ERR_UNAUTHORIZED;
                f->reconciled |= (uint8_t)(1u << j);
                return NINLIL_OK;
            }
    }
    return NINLIL_ERR_NOT_FOUND;
}

int ninlil_coordinator_route_check(void *ctx, const ninlil_network_path *p,
                                   uint64_t epoch, uint64_t now)
{
    ninlil_coordinator *c = ctx;
    ninlil_network_flow *f;
    unsigned int i;
    if (!c || c->poisoned || !ninlil_network_path_valid(p) ||
        now < c->last_change_ms)
        return NINLIL_ERR_STATE;
    f = ninlil_network_flow_find(c, p, 0);
    if (!f || f->active.epoch != epoch || now >= f->active.valid_until_ms ||
        f->reconciled != all(&f->active) || f->active.path.count != p->count ||
        memcmp(f->active.path.nodes, p->nodes,
               (size_t)p->count * sizeof(uint16_t)) != 0)
        return NINLIL_ERR_STATE;
    for (i = 0u; i < p->count; i++)
        if (!ninlil_network_policy(c, p->nodes[i], i > 0u && i + 1u < p->count,
                                   f->active.path.membership_epochs[i]))
            return NINLIL_ERR_UNAUTHORIZED;
    return NINLIL_OK;
}

uint32_t ninlil_network_rto(uint32_t current, uint32_t sample,
                            int retransmitted, uint32_t minimum)
{
    uint64_t result;
    if (minimum < 100u)
        minimum = 100u;
    if (minimum > 30000u)
        minimum = 30000u;
    if (current < minimum)
        current = minimum;
    result = retransmitted || sample == 0u
                 ? current
                 : ((uint64_t)current * 7u + (uint64_t)sample * 2u) / 8u;
    if (result < minimum)
        result = minimum;
    return result > 30000u ? 30000u : (uint32_t)result;
}

int ninlil_coordinator_remove_status(ninlil_coordinator *c, uint16_t peer,
                                     uint64_t now, int drained,
                                     ninlil_remove_status *out)
{
    ninlil_remove_status status;
    ninlil_network_plan saved;
    unsigned int i, j;
    if (!c || c->poisoned || !out || peer == 0u || peer == UINT16_MAX)
        return NINLIL_ERR_INVALID;
    memset(&status, 0, sizeof(status));
    saved = c->active;
    if (c->pending.epoch != 0u)
        status.blocked_flows++;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_path path, alternative;
        ninlil_network_flow *f = &c->flows[i];
        if (f->active.epoch == 0u)
            continue;
        path = f->active.path;
        for (j = 0u; j < path.count; j++)
            if (path.nodes[j] == peer)
                break;
        if (j == path.count)
            continue;
        status.dependent_flows++;
        if (ninlil_coordinator_select(c, path.nodes[0],
                                      path.nodes[path.count - 1u], peer, now,
                                      &alternative) == NINLIL_OK)
            status.reroutable_flows++;
        else
            status.blocked_flows++;
    }
    c->active = saved;
    status.ready =
        drained && status.dependent_flows == 0u && status.blocked_flows == 0u
            ? 1u
            : 0u;
    *out = status;
    return NINLIL_OK;
}

int ninlil_coordinator_route(void *ctx, uint16_t source, uint16_t target,
                             uint64_t now, ninlil_network_plan *plan)
{
    ninlil_coordinator *c = ctx;
    unsigned int i;
    if (!c || !plan)
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_plan *p = &c->flows[i].active;
        if (p->path.count >= 2u && p->path.nodes[0] == source &&
            p->path.nodes[p->path.count - 1u] == target) {
            int rc = ninlil_coordinator_route_check(c, &p->path, p->epoch, now);
            if (rc == NINLIL_OK)
                *plan = *p;
            return rc;
        }
    }
    return NINLIL_ERR_NOT_FOUND;
}

int ninlil_coordinator_retire(ninlil_coordinator *c, uint16_t source,
                              uint16_t target, uint64_t expected_epoch,
                              uint64_t now, ninlil_time_quality quality,
                              int released)
{
    unsigned int i;
    if (!c || c->poisoned || c->pending.epoch != 0u ||
        c->last_epoch == UINT64_MAX || quality != NINLIL_TIME_RESTART_SAFE)
        return NINLIL_ERR_STATE;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_flow *f = &c->flows[i];
        ninlil_network_plan retired;
        int rc;
        if (!f->active.epoch || f->active.path.nodes[0] != source ||
            f->active.path.nodes[f->active.path.count - 1u] != target)
            continue;
        if (f->active.epoch != expected_epoch)
            return NINLIL_ERR_STATE;
        if (!released && now < f->active.valid_until_ms)
            return NINLIL_ERR_BUSY;
        retired = f->active;
        retired.phase = NINLIL_PLAN_RETIRED;
        retired.epoch = c->last_epoch + 1u;
        rc = persist(c, &retired);
        if (rc != NINLIL_OK)
            return rc;
        c->last_epoch = retired.epoch;
        if (c->active.epoch == f->active.epoch)
            memset(&c->active, 0, sizeof(c->active));
        memset(f, 0, sizeof(*f));
        return NINLIL_OK;
    }
    return NINLIL_ERR_NOT_FOUND;
}
