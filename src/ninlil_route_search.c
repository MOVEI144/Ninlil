#include "ninlil_route_search.h"
#include <string.h>
static int registered(const ninlil_search_node *n)
{
    return n->address && n->address != UINT16_MAX && n->membership_epoch &&
           n->role >= NINLIL_ROLE_BATTERY_LEAF &&
           n->role <= NINLIL_ROLE_SITE_GATEWAY &&
           !(n->capabilities & ~NINLIL_CAP_KNOWN_MASK);
}
static int member(const ninlil_search_node *n)
{
    return registered(n) && n->membership_epoch == n->session_epoch;
}
static int forwarder(const ninlil_search_node *n)
{
    return member(n) && n->role != NINLIL_ROLE_BATTERY_LEAF &&
           (n->capabilities & NINLIL_CAP_RELAY_CUSTODY);
}
int ninlil_route_search_begin(ninlil_route_search *s,
                              const ninlil_search_config *c)
{
    if (!s || !c || !c->nodes || !c->edges || !c->validate ||
        !c->current_generation || !c->generation || c->probe_cost_only > 1u ||
        *c->current_generation != c->generation || c->node_count < 2u ||
        c->node_count > NINLIL_SEARCH_NODES ||
        c->edge_count > NINLIL_SEARCH_EDGES || !c->max_age_ms ||
        c->max_age_ms > 120000u || !c->work_limit || c->work_limit > 4096u ||
        c->source >= c->node_count || c->target >= c->node_count ||
        c->source == c->target ||
        (c->excluded != UINT16_MAX && c->excluded >= c->node_count) ||
        c->source == c->excluded || c->target == c->excluded)
        return NINLIL_ERR_INVALID;
    for (unsigned int i = 0u; i < c->node_count; i++) {
        if (!registered(&c->nodes[i]))
            return NINLIL_ERR_UNAUTHORIZED;
        for (unsigned int j = 0u; j < i; j++)
            if (c->nodes[i].address == c->nodes[j].address)
                return NINLIL_ERR_CONFLICT;
    }
    if (!member(&c->nodes[c->source]) || !member(&c->nodes[c->target]))
        return NINLIL_ERR_UNAUTHORIZED;
    for (unsigned int i = 0u; i < c->edge_count; i++) {
        const ninlil_search_edge *e = &c->edges[i];
        if (e->from >= c->node_count || e->to >= c->node_count ||
            e->from == e->to || !e->profile ||
            (e->known & ~NINLIL_SEARCH_KNOWN_ALL) ||
            e->delivered > e->attempts || e->attempts > 32u ||
            e->exchange_us > 2000000u || e->queue_us > 30000000u ||
            e->wake_us > 120000000u || e->commit_us > 30000000u)
            return NINLIL_ERR_INVALID;
    }
    memset(s, 0, sizeof(*s));
    s->config = *c;
    s->frontier[0].nodes[0] = c->source;
    s->frontier[0].count = 1u;
    s->frontier_count = 1u;
    return NINLIL_OK;
}
static int compare(const ninlil_route_search *s, const ninlil_search_path *a,
                   const ninlil_search_path *b)
{
    if (a->cost_us != b->cost_us)
        return a->cost_us < b->cost_us ? -1 : 1;
    for (unsigned int i = 0u; i < a->count && i < b->count; i++) {
        uint16_t x = s->config.nodes[a->nodes[i]].address;
        uint16_t y = s->config.nodes[b->nodes[i]].address;
        if (x != y)
            return x < y ? -1 : 1;
        if (i && a->profiles[i - 1u] != b->profiles[i - 1u])
            return a->profiles[i - 1u] < b->profiles[i - 1u] ? -1 : 1;
    }
    return a->count == b->count ? 0 : a->count < b->count ? -1 : 1;
}
static void push(ninlil_route_search *s, const ninlil_search_path *p)
{
    unsigned int worst = 0u;
    for (unsigned int i = 0u; i < s->frontier_count; i++) {
        if (!compare(s, p, &s->frontier[i]))
            return;
        if (compare(s, &s->frontier[i], &s->frontier[worst]) > 0)
            worst = i;
    }
    if (s->frontier_count < NINLIL_SEARCH_FRONTIER)
        s->frontier[s->frontier_count++] = *p;
    else {
        s->limited = 1u;
        if (compare(s, p, &s->frontier[worst]) < 0)
            s->frontier[worst] = *p;
    }
}
static int pop(ninlil_route_search *s)
{
    unsigned int best = 0u;
    if (!s->frontier_count) {
        s->done = 1u;
        return NINLIL_OK;
    }
    for (unsigned int i = 1u; i < s->frontier_count; i++)
        if (compare(s, &s->frontier[i], &s->frontier[best]) < 0)
            best = i;
    s->expanding = s->frontier[best];
    s->frontier[best] = s->frontier[--s->frontier_count];
    s->edge_cursor = 0u;
    if (s->expanding.nodes[s->expanding.count - 1u] == s->config.target) {
        int rc = s->config.validate(s->config.validate_ctx, &s->expanding);
        if (rc == NINLIL_OK) {
            s->candidates[s->candidate_count++] = s->expanding;
            if (s->candidate_count == NINLIL_SEARCH_CANDIDATES) {
                s->done = 1u;
                s->limited = s->limited || s->frontier_count != 0u;
            }
        } else if (rc != NINLIL_ERR_CAPACITY && rc != NINLIL_ERR_UNAUTHORIZED &&
                   rc != NINLIL_ERR_STATE && rc != NINLIL_ERR_NOT_FOUND)
            return rc;
    } else if (s->expanding.count < NINLIL_SEARCH_PATH)
        s->active = 1u;
    return NINLIL_OK;
}
static void expand(ninlil_route_search *s)
{
    const ninlil_search_config *c = &s->config;
    const ninlil_search_edge *e = &c->edges[s->edge_cursor++];
    ninlil_search_path p = s->expanding;
    uint64_t cost;
    if (s->edge_cursor == c->edge_count)
        s->active = 0u;
    if (e->from != p.nodes[p.count - 1u] || e->to == c->excluded ||
        !member(&c->nodes[e->to]) ||
        (e->known != NINLIL_SEARCH_KNOWN_ALL &&
         !(c->probe_cost_only && e->known == 3u && !e->wake_us &&
           !e->commit_us)) ||
        e->attempts < 8u || !e->delivered || !e->exchange_us ||
        e->observed_ms > c->now_ms ||
        c->now_ms - e->observed_ms > c->max_age_ms ||
        e->from_epoch != c->nodes[e->from].membership_epoch ||
        e->to_epoch != c->nodes[e->to].membership_epoch ||
        (e->to != c->target && !forwarder(&c->nodes[e->to])))
        return;
    for (unsigned int i = 0u; i < p.count; i++)
        if (p.nodes[i] == e->to)
            return;
    cost = ((uint64_t)e->exchange_us * e->attempts + e->delivered - 1u) /
               e->delivered +
           e->queue_us + e->wake_us + e->commit_us;
    if (p.cost_us > UINT64_MAX - cost) {
        s->error = NINLIL_ERR_INVALID;
        return;
    }
    p.profiles[p.count - 1u] = e->profile;
    p.nodes[p.count++] = e->to;
    p.cost_us += cost;
    push(s, &p);
}
int ninlil_route_search_step(ninlil_route_search *s, unsigned int work)
{
    if (!s || !s->config.current_generation || !work || work > 64u)
        return NINLIL_ERR_INVALID;
    if (*s->config.current_generation != s->config.generation)
        return s->error = NINLIL_ERR_CONFLICT;
    if (s->error)
        return s->error;
    for (unsigned int i = 0u; i < work && !s->done; i++) {
        if (s->work == s->config.work_limit) {
            s->limited = s->done = 1u;
            break;
        }
        s->work++;
        if (s->active && s->edge_cursor < s->config.edge_count)
            expand(s);
        else {
            s->active = 0u;
            s->error = pop(s);
        }
        if (s->error)
            return s->error;
    }
    return s->done ? NINLIL_OK : NINLIL_ERR_BUSY;
}
static unsigned int overlap(const ninlil_route_search *s,
                            const ninlil_search_path *a,
                            const ninlil_search_path *b)
{
    unsigned int count = 0u;
    for (unsigned int i = 1u; i + 1u < a->count; i++)
        for (unsigned int j = 1u; j + 1u < b->count; j++) {
            const ninlil_search_node *x = &s->config.nodes[a->nodes[i]];
            const ninlil_search_node *y = &s->config.nodes[b->nodes[j]];
            if (a->nodes[i] == b->nodes[j] ||
                (x->failure_domain && x->failure_domain == y->failure_domain))
                count++;
        }
    return count;
}
int ninlil_route_search_result(const ninlil_route_search *s,
                               ninlil_search_result *out)
{
    ninlil_search_result result = {0};
    uint8_t used[NINLIL_SEARCH_CANDIDATES] = {0};
    if (!s || !out || !s->config.current_generation)
        return NINLIL_ERR_INVALID;
    if (*s->config.current_generation != s->config.generation)
        return NINLIL_ERR_CONFLICT;
    if (s->error)
        return s->error;
    if (!s->done)
        return NINLIL_ERR_BUSY;
    if (!s->candidate_count)
        return s->limited ? NINLIL_ERR_CAPACITY : NINLIL_ERR_NOT_FOUND;
    result.work = s->work;
    result.search_limited = s->limited;
    result.declared_disjoint = 1u;
    for (unsigned int pick = 0u; pick < 3u && pick < s->candidate_count;
         pick++) {
        unsigned int best = NINLIL_SEARCH_CANDIDATES, least = UINT32_MAX;
        for (unsigned int i = 0u; i < s->candidate_count; i++) {
            unsigned int shared = 0u;
            if (used[i])
                continue;
            for (unsigned int j = 0u; j < pick; j++)
                shared += overlap(s, &s->candidates[i], &result.paths[j]);
            if (best == NINLIL_SEARCH_CANDIDATES || shared < least ||
                (shared == least &&
                 compare(s, &s->candidates[i], &s->candidates[best]) < 0)) {
                least = shared;
                best = i;
            }
        }
        used[best] = 1u;
        result.paths[pick] = s->candidates[best];
        result.count++;
        if (least)
            result.declared_disjoint = 0u;
        for (unsigned int i = 1u; i + 1u < result.paths[pick].count; i++)
            if (!s->config.nodes[result.paths[pick].nodes[i]].failure_domain)
                result.declared_disjoint = 0u;
    }
    if (result.count < 2u)
        result.declared_disjoint = 0u;
    *out = result;
    return NINLIL_OK;
}
