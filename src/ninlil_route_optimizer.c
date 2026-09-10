#include "ninlil_route_optimizer.h"
#include "ninlil_network_internal.h"
#include <string.h>

int ninlil_route_optimizer_open(ninlil_route_optimizer *o,
                                ninlil_search_node *nodes, uint16_t nc,
                                ninlil_search_edge *edges, uint16_t ec,
                                uint32_t limit,
                                ninlil_route_constraint constraint, void *ctx)
{
    if (!o || !nodes || !edges || nc < 2u || nc > NINLIL_SEARCH_NODES || !ec ||
        ec > NINLIL_SEARCH_EDGES || !limit || limit > 4096u)
        return NINLIL_ERR_INVALID;
    memset(o, 0, sizeof(*o));
    o->nodes = nodes;
    o->edges = edges;
    o->node_capacity = nc;
    o->edge_capacity = ec;
    o->work_limit = limit;
    o->constraint = constraint;
    o->constraint_ctx = ctx;
    o->opened = 1u;
    return NINLIL_OK;
}

int ninlil_route_optimizer_attach(ninlil_route_optimizer *o,
                                  ninlil_coordinator *c)
{
    if (!o || !o->opened || o->owner || !c || !c->policy || c->poisoned ||
        c->optimizer || c->pending.epoch ||
        c->node_capacity > o->node_capacity ||
        c->edge_capacity > o->edge_capacity)
        return NINLIL_ERR_STATE;
    o->owner = c;
    c->optimizer = o;
    return NINLIL_OK;
}

static int dense(const ninlil_route_optimizer *o, uint16_t count,
                 uint16_t address)
{
    for (unsigned int i = 0u; i < count; i++)
        if (o->nodes[i].address == address)
            return (int)i;
    return -1;
}

static int live_edge(ninlil_coordinator *c, uint16_t from, uint16_t to,
                     uint64_t now)
{
    for (unsigned int i = 0u; i < c->edge_capacity; i++) {
        const ninlil_network_edge *e = &c->edges[i];
        if (e->used && e->from == from && e->to == to && e->attempts >= 8u &&
            e->delivered && e->delivered <= e->attempts &&
            e->observed_ms <= now &&
            now - e->observed_ms <= NINLIL_NETWORK_STALE_MS &&
            ninlil_network_policy(c, from, 0, e->membership_epoch))
            return 1;
    }
    return 0;
}

int ninlil_route_optimizer_validate(ninlil_route_optimizer *o,
                                    ninlil_coordinator *c,
                                    const ninlil_network_path *p, uint64_t now)
{
    if (!o || !o->opened || o->owner != c || !c || c->poisoned ||
        !ninlil_network_path_valid(p))
        return NINLIL_ERR_STATE;
    for (unsigned int i = 0u; i < p->count; i++) {
        if (!ninlil_network_policy(c, p->nodes[i], i > 0u && i + 1u < p->count,
                                   p->membership_epochs[i]))
            return NINLIL_ERR_UNAUTHORIZED;
        if (i && (!live_edge(c, p->nodes[i - 1u], p->nodes[i], now) ||
                  !live_edge(c, p->nodes[i], p->nodes[i - 1u], now)))
            return NINLIL_ERR_NOT_FOUND;
    }
    return o->constraint ? o->constraint(o->constraint_ctx, p, now) : NINLIL_OK;
}

static void path_from(ninlil_route_optimizer *o, const ninlil_search_path *p,
                      ninlil_network_path *out)
{
    memset(out, 0, sizeof(*out));
    out->count = p->count;
    out->cost_us = p->cost_us;
    for (unsigned int i = 0u; i < p->count; i++) {
        out->nodes[i] = o->nodes[p->nodes[i]].address;
        out->membership_epochs[i] = o->nodes[p->nodes[i]].membership_epoch;
    }
}

static int candidate(void *ctx, const ninlil_search_path *p)
{
    ninlil_route_optimizer *o = ctx;
    ninlil_network_path path;
    path_from(o, p, &path);
    int rc = ninlil_route_optimizer_validate(o, o->owner, &path, o->now_ms);
    return rc == NINLIL_ERR_BUSY ? NINLIL_ERR_CAPACITY : rc;
}

static int snapshot(ninlil_route_optimizer *o, ninlil_coordinator *c,
                    uint64_t now)
{
    ninlil_search_config config = {0};
    int source, target, excluded;
    if (c->node_count > o->node_capacity || c->edge_capacity > o->edge_capacity)
        return NINLIL_ERR_CAPACITY;
    for (unsigned int i = 0u; i < c->node_count; i++) {
        ninlil_peer_policy policy = {0};
        ninlil_search_node *n = &o->nodes[config.node_count];
        if (c->policy(c->policy_ctx, c->nodes[i].id, &policy) != NINLIL_OK ||
            !policy.membership_epoch ||
            policy.membership_epoch != policy.session_membership_epoch)
            continue;
        memset(n, 0, sizeof(*n));
        n->address = c->nodes[i].id;
        n->membership_epoch = policy.membership_epoch;
        n->session_epoch = policy.session_membership_epoch;
        n->role = policy.role;
        n->capabilities = policy.capabilities;
        /* No fabricated power/radio failure domains. */
        config.node_count++;
    }
    source = dense(o, config.node_count, o->source);
    target = dense(o, config.node_count, o->target);
    excluded = dense(o, config.node_count, o->excluded);
    if (source < 0 || target < 0)
        return NINLIL_ERR_NOT_FOUND;
    for (unsigned int i = 0u; i < c->edge_capacity; i++) {
        const ninlil_network_edge *e = &c->edges[i];
        ninlil_search_edge *copy = &o->edges[config.edge_count];
        int from = dense(o, config.node_count, e->from);
        int to = dense(o, config.node_count, e->to);
        if (!e->used || from < 0 || to < 0 || e->attempts > 32u)
            continue;
        memset(copy, 0, sizeof(*copy));
        copy->from = (uint16_t)from;
        copy->to = (uint16_t)to;
        copy->from_epoch = e->membership_epoch;
        copy->to_epoch = o->nodes[to].membership_epoch;
        copy->attempts = e->attempts;
        copy->delivered = e->delivered;
        copy->exchange_us = e->airtime_us; /* Explicit probe-cost-only mode. */
        copy->queue_us = e->queue_us;
        copy->observed_ms = e->observed_ms;
        copy->profile = c->permitted_profile;
        copy->known = 3u; /* Wake/commit UNKNOWN, not calibrated zero. */
        config.edge_count++;
    }
    if (o->generation == UINT64_MAX)
        return NINLIL_ERR_CAPACITY;
    o->generation++;
    config.nodes = o->nodes;
    config.edges = o->edges;
    config.current_generation = &o->generation;
    config.generation = o->generation;
    config.now_ms = now;
    config.max_age_ms = NINLIL_NETWORK_STALE_MS;
    config.work_limit = o->work_limit;
    config.source = (uint16_t)source;
    config.target = (uint16_t)target;
    config.excluded = excluded < 0 ? UINT16_MAX : (uint16_t)excluded;
    config.probe_cost_only = 1u;
    config.validate = candidate;
    config.validate_ctx = o;
    return ninlil_route_search_begin(&o->search, &config);
}

int ninlil_route_optimizer_step(ninlil_route_optimizer *o,
                                ninlil_coordinator *c, uint64_t now,
                                unsigned int work)
{
    int rc;
    if (!o || !c || !o->opened || o->owner != c || c->optimizer != o || !work ||
        work > 64u || now < o->now_ms)
        return NINLIL_ERR_STATE;
    o->now_ms = now;
    if (!o->active || o->ready)
        return NINLIL_ERR_EMPTY;
    if (c->poisoned || !c->enabled || c->pending.epoch ||
        now - o->began_ms >= NINLIL_OPTIMIZER_MAX_AGE_MS) {
        o->result_code = NINLIL_ERR_EXPIRED;
        o->ready = 1u;
        return o->result_code;
    }
    rc = ninlil_route_search_step(&o->search, work);
    if (rc == NINLIL_ERR_BUSY)
        return rc;
    if (rc == NINLIL_OK)
        rc = ninlil_route_search_result(&o->search, &o->result);
    o->result_code = rc;
    o->ready = 1u;
    return rc;
}

int ninlil_route_optimizer_select(ninlil_route_optimizer *o,
                                  ninlil_coordinator *c, uint16_t source,
                                  uint16_t target, uint16_t excluded,
                                  uint64_t now, ninlil_network_path *out)
{
    ninlil_network_path best = {0};
    int rc;
    if (!o || !c || !out || !o->opened || o->owner != c || c->optimizer != o ||
        c->poisoned || !c->enabled || c->pending.epoch || !source || !target ||
        source == target || source == excluded || target == excluded ||
        now < o->now_ms)
        return NINLIL_ERR_STATE;
    o->now_ms = now;
    if (o->active) {
        if (o->source != source || o->target != target ||
            o->excluded != excluded) {
            /* An abandoned request cannot pin the sole workspace indefinitely.
             */
            if (now - o->began_ms >= NINLIL_OPTIMIZER_MAX_AGE_MS)
                o->active = 0u;
            return NINLIL_ERR_BUSY;
        }
        if (!o->ready)
            return NINLIL_ERR_BUSY;
        o->active = 0u;
        if (now - o->began_ms >= NINLIL_OPTIMIZER_MAX_AGE_MS)
            return NINLIL_ERR_EXPIRED;
        if (o->result_code != NINLIL_OK)
            return o->result_code;
        for (unsigned int i = 0u; i < o->result.count; i++) {
            ninlil_network_path current;
            path_from(o, &o->result.paths[i], &current);
            rc = ninlil_route_optimizer_validate(o, c, &current, now);
            if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_CORRUPT ||
                rc == NINLIL_ERR_FAULT)
                return rc;
            if (rc != NINLIL_OK)
                continue;
            current.cost_us = ninlil_network_path_cost(c, &current, now);
            if (current.cost_us != UINT64_MAX &&
                (!best.count || current.cost_us < best.cost_us))
                best = current;
        }
        if (!best.count)
            return NINLIL_ERR_NOT_FOUND;
        *out = best;
        return NINLIL_OK;
    }
    o->source = source;
    o->target = target;
    o->excluded = excluded;
    o->began_ms = now;
    o->ready = 0u;
    memset(&o->result, 0, sizeof(o->result));
    rc = snapshot(o, c, now);
    if (rc != NINLIL_OK)
        return rc;
    o->active = 1u;
    return NINLIL_ERR_BUSY;
}
