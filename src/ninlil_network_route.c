#include "ninlil_network_internal.h"
#include <string.h>

static ninlil_network_node *node(ninlil_coordinator *c, uint16_t id)
{
    uint16_t i;
    for (i = 0u; i < c->node_count; i++)
        if (c->nodes[i].id == id)
            return &c->nodes[i];
    return NULL;
}

int ninlil_coordinator_observe(ninlil_coordinator *c, uint16_t reporter,
                               const ninlil_network_edge *e, uint64_t now)
{
    ninlil_network_edge *slot = NULL;
    uint16_t i, needed = 0u;
    if (!c || c->poisoned || !e || reporter != e->from || e->from == 0u ||
        e->to == 0u || e->from == UINT16_MAX || e->to == UINT16_MAX ||
        e->from == e->to || e->attempts < 8u || e->delivered > e->attempts ||
        e->airtime_us == 0u || e->airtime_us > 400000u ||
        e->queue_us > 30000000u || e->observed_ms > now ||
        now - e->observed_ms > NINLIL_NETWORK_STALE_MS ||
        !ninlil_network_policy(c, e->from, 0, e->membership_epoch) ||
        !ninlil_network_policy(c, e->to, 0, 0u))
        return NINLIL_ERR_UNAUTHORIZED;
    if (!node(c, e->from))
        needed++;
    if (!node(c, e->to))
        needed++;
    if (needed > c->node_capacity - c->node_count)
        return NINLIL_ERR_CAPACITY;
    for (i = 0u; i < c->edge_capacity; i++) {
        if (c->edges[i].used && c->edges[i].from == e->from &&
            c->edges[i].to == e->to) {
            if (e->observed_ms < c->edges[i].observed_ms)
                return NINLIL_ERR_CONFLICT;
            slot = &c->edges[i];
            break;
        }
        if (!c->edges[i].used)
            slot = &c->edges[i];
    }
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    if (!node(c, e->from))
        c->nodes[c->node_count++].id = e->from;
    if (!node(c, e->to))
        c->nodes[c->node_count++].id = e->to;
    *slot = *e;
    slot->used = 1u;
    return NINLIL_OK;
}

static int edge_valid(ninlil_coordinator *c, const ninlil_network_edge *e,
                      uint64_t now)
{
    return e->used && e->delivered != 0u && e->observed_ms <= now &&
           now - e->observed_ms <= NINLIL_NETWORK_STALE_MS &&
           ninlil_network_policy(c, e->from, 0, e->membership_epoch) &&
           ninlil_network_policy(c, e->to, 0, 0u);
}

static uint64_t edge_cost(const ninlil_network_edge *e)
{
    return ((uint64_t)e->airtime_us * e->attempts + e->delivered - 1u) /
               e->delivered +
           e->queue_us;
}

uint64_t ninlil_network_path_cost(ninlil_coordinator *c,
                                  const ninlil_network_path *p, uint64_t now)
{
    uint64_t cost = 0u;
    unsigned int hop;
    if (!ninlil_network_path_valid(p))
        return UINT64_MAX;
    for (hop = 1u; hop < p->count; hop++) {
        uint16_t i;
        int found = 0;
        if (hop + 1u < p->count &&
            !ninlil_network_policy(c, p->nodes[hop], 1, 0u))
            return UINT64_MAX;
        for (i = 0u; i < c->edge_capacity; i++) {
            ninlil_network_edge *e = &c->edges[i];
            if (e->from == p->nodes[hop - 1u] && e->to == p->nodes[hop] &&
                edge_valid(c, e, now)) {
                cost += edge_cost(e);
                found = 1;
                break;
            }
        }
        if (!found)
            return UINT64_MAX;
    }
    return cost;
}

int ninlil_coordinator_select(ninlil_coordinator *c, uint16_t source,
                              uint16_t target, uint16_t excluded, uint64_t now,
                              ninlil_network_path *out)
{
    ninlil_network_node *start, *end;
    uint16_t i;
    unsigned int hop;
    if (!c || c->poisoned || !c->enabled || !out || source == target ||
        source == excluded || target == excluded || c->pending.epoch != 0u)
        return NINLIL_ERR_STATE;
    start = node(c, source);
    end = node(c, target);
    if (!start || !end)
        return NINLIL_ERR_NOT_FOUND;
    for (i = 0u; i < c->node_count; i++) {
        memset(&c->nodes[i].path, 0, sizeof(c->nodes[i].path));
        c->nodes[i].path.cost_us = UINT64_MAX;
    }
    start->path.nodes[0] = source;
    start->path.count = 1u;
    start->path.cost_us = 0u;
    for (hop = 0u; hop < NINLIL_NETWORK_HOPS_MAX; hop++) {
        for (i = 0u; i < c->node_count; i++)
            c->nodes[i].previous = c->nodes[i].path;
        for (i = 0u; i < c->edge_capacity; i++) {
            ninlil_network_edge *e = &c->edges[i];
            ninlil_network_node *from, *to;
            ninlil_network_path candidate;
            unsigned int j;
            int loop = 0;
            if (!edge_valid(c, e, now) || e->from == excluded ||
                e->to == excluded ||
                (e->from != source &&
                 !ninlil_network_policy(c, e->from, 1, 0u)))
                continue;
            from = node(c, e->from);
            to = node(c, e->to);
            if (!from || !to || from->previous.count == 0u ||
                from->previous.count >= NINLIL_NETWORK_PATH_MAX)
                continue;
            for (j = 0u; j < from->previous.count; j++)
                if (from->previous.nodes[j] == e->to)
                    loop = 1;
            if (loop)
                continue;
            candidate = from->previous;
            candidate.nodes[candidate.count++] = e->to;
            candidate.cost_us += edge_cost(e);
            if (candidate.cost_us < to->path.cost_us)
                to->path = candidate;
        }
    }
    if (!ninlil_network_path_valid(&end->path))
        return NINLIL_ERR_NOT_FOUND;
    {
        ninlil_network_flow *f = ninlil_network_flow_find(c, &end->path, 0);
        if (f)
            c->active = f->active;
        else
            memset(&c->active, 0, sizeof(c->active));
    }
    if (c->active.epoch != 0u && c->active.path.nodes[0] == source &&
        c->active.path.nodes[c->active.path.count - 1u] == target) {
        uint64_t old = ninlil_network_path_cost(c, &c->active.path, now);
        int excluded_active = 0;
        for (hop = 0u; hop < c->active.path.count; hop++)
            if (c->active.path.nodes[hop] == excluded)
                excluded_active = 1;
        if (!excluded_active && old != UINT64_MAX &&
            (now < c->last_change_ms ||
             now - c->last_change_ms < NINLIL_NETWORK_HOLD_MS ||
             end->path.cost_us >= old - old / 5u)) {
            *out = c->active.path;
            out->cost_us = old;
            return NINLIL_OK;
        }
    }
    if (!ninlil_network_snapshot_epochs(c, &end->path))
        return NINLIL_ERR_UNAUTHORIZED;
    *out = end->path;
    return NINLIL_OK;
}

int ninlil_coordinator_tick(ninlil_coordinator *c, uint16_t source,
                            uint16_t target, uint16_t excluded, uint64_t now,
                            ninlil_time_quality quality)
{
    ninlil_network_path selected;
    ninlil_network_flow *f;
    int rc;
    if (!c || c->poisoned || quality != NINLIL_TIME_RESTART_SAFE ||
        now > UINT64_MAX - NINLIL_NETWORK_LEASE_MAX_MS ||
        now < c->last_change_ms)
        return NINLIL_ERR_STATE;
    if (!c->enabled)
        return NINLIL_ERR_EMPTY;
    if (c->pending.epoch)
        return NINLIL_ERR_BUSY;
    rc = ninlil_coordinator_select(c, source, target, excluded, now, &selected);
    if (rc != NINLIL_OK)
        return rc;
    f = ninlil_network_flow_find(c, &selected, 0);
    if (f && f->active.valid_until_ms > now + 10000u &&
        f->active.path.count == selected.count &&
        memcmp(f->active.path.nodes, selected.nodes,
               (size_t)selected.count * sizeof(uint16_t)) == 0 &&
        memcmp(f->active.path.membership_epochs, selected.membership_epochs,
               (size_t)selected.count * sizeof(uint64_t)) == 0) {
        uint64_t old_cost = f->active.path.cost_us;
        uint64_t difference = old_cost > selected.cost_us
                                  ? old_cost - selected.cost_us
                                  : selected.cost_us - old_cost;
        if (difference <= old_cost / 5u)
            return NINLIL_ERR_EMPTY;
    }
    return ninlil_coordinator_stage(c, &selected, now,
                                    now + NINLIL_NETWORK_LEASE_MAX_MS, quality);
}
