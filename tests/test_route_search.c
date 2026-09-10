#include "ninlil_route_search.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static int admit(void *ctx, const ninlil_search_path *p)
{
    unsigned int *count = ctx;
    (*count)++;
    return p->cost_us <= 1000000 ? NINLIL_OK : NINLIL_ERR_CAPACITY;
}
static int finish(ninlil_route_search *s, ninlil_search_result *r)
{
    int rc = NINLIL_ERR_BUSY;
    for (unsigned int i = 0; i < 4098 && rc == NINLIL_ERR_BUSY; i++)
        rc = ninlil_route_search_step(s, 1);
    return rc ? rc : ninlil_route_search_result(s, r);
}
static ninlil_search_edge edge(uint16_t from, uint16_t to, uint32_t cost)
{
    ninlil_search_edge e = {.from = from,
                            .to = to,
                            .attempts = 8,
                            .delivered = 8,
                            .profile = 1,
                            .exchange_us = cost,
                            .observed_ms = 100,
                            .from_epoch = 1,
                            .to_epoch = 1,
                            .known = 15};
    return e;
}
int main(void)
{
    ninlil_search_node nodes[512] = {0};
    ninlil_search_edge edges[512] = {0};
    ninlil_route_search s;
    ninlil_search_result r, before;
    uint64_t generation = 1;
    unsigned int validated = 0;
    ninlil_search_config c = {.nodes = nodes,
                              .edges = edges,
                              .current_generation = &generation,
                              .generation = 1,
                              .now_ms = 1000,
                              .max_age_ms = 30000,
                              .work_limit = 4096,
                              .node_count = 7,
                              .edge_count = 7,
                              .source = 0,
                              .target = 6,
                              .excluded = UINT16_MAX,
                              .validate = admit,
                              .validate_ctx = &validated};
    for (unsigned int i = 0; i < 512; i++) {
        nodes[i].address = (uint16_t)(i + 1);
        nodes[i].membership_epoch = nodes[i].session_epoch = 1;
        nodes[i].role = NINLIL_ROLE_POWERED_ENDPOINT;
        nodes[i].failure_domain = i + 1;
    }
    nodes[1].capabilities = nodes[2].capabilities = NINLIL_CAP_RELAY_CUSTODY;
    edges[0] = edge(0, 1, 10000);
    edges[1] = edge(1, 6, 10000);
    edges[2] = edge(0, 2, 20000);
    edges[3] = edge(2, 6, 20000);
    edges[4] = edge(0, 6, 100000);
    edges[5] = edge(1, 0, 1);
    edges[6] = edge(2, 1, 50000);
    CHECK(ninlil_route_search_begin(&s, &c) == 0 && finish(&s, &r) == 0);
    CHECK(r.count == 3 && r.paths[0].cost_us == 20000 &&
          r.paths[0].nodes[1] == 1);
    CHECK(r.paths[1].nodes[1] == 2 && r.paths[2].count == 2);
    CHECK(r.declared_disjoint && validated >= 3);
    /* An unrelated registered but offline participant is not a global veto. */
    nodes[5].session_epoch = 0;
    CHECK(ninlil_route_search_begin(&s, &c) == 0 && finish(&s, &r) == 0);
    CHECK(r.paths[0].nodes[1] == 1);
    nodes[5].session_epoch = 1;
    edges[0].queue_us = 200000;
    CHECK(ninlil_route_search_begin(&s, &c) == 0 && finish(&s, &r) == 0);
    CHECK(r.paths[0].nodes[1] == 2);
    nodes[2].role = NINLIL_ROLE_BATTERY_LEAF;
    CHECK(ninlil_route_search_begin(&s, &c) == 0 && finish(&s, &r) == 0);
    CHECK(r.paths[0].count == 2);
    edges[4].known = 1;
    edges[0].queue_us = 30000000;
    CHECK(ninlil_route_search_begin(&s, &c) == 0 &&
          finish(&s, &r) == NINLIL_ERR_NOT_FOUND);
    /* Valid proposals are never returned after snapshot replacement. */
    edges[4].known = 15;
    CHECK(ninlil_route_search_begin(&s, &c) == 0);
    generation++;
    before = r;
    CHECK(ninlil_route_search_step(&s, 64) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_route_search_result(&s, &r) == NINLIL_ERR_CONFLICT &&
          !memcmp(&r, &before, sizeof(r)));
    generation = 1;
    c.work_limit = 1;
    CHECK(ninlil_route_search_begin(&s, &c) == 0 &&
          finish(&s, &r) == NINLIL_ERR_CAPACITY);
    c.work_limit = 4096;
    c.node_count = 512;
    c.edge_count = 511;
    c.target = 511;
    for (unsigned int i = 0; i < 512; i++)
        nodes[i].capabilities = 0;
    for (unsigned int i = 0; i < 511; i++)
        edges[i] = edge(0, (uint16_t)(i + 1), 10000);
    CHECK(ninlil_route_search_begin(&s, &c) == 0 && finish(&s, &r) == 0);
    CHECK(r.count == 1 && r.paths[0].nodes[1] == 511 &&
          r.paths[0].cost_us == 10000);
    printf("7-node diverse paths / queue / battery exclusion / 512-node graph "
           "PASS, work=%u\n",
           r.work);
    return 0;
}
