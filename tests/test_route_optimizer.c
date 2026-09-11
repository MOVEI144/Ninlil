/* Real Coordinator/search/plan codecs. Only policy authority and the
 * persistence callback are in-memory fixtures; not a physical journal/crypto/RF
 * claim. */
#include "ninlil_route_optimizer.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static ninlil_coordinator c, recovered;
static ninlil_network_node graph[16], recovery_graph[16];
static ninlil_network_edge edges[64], recovery_edges[64];
static ninlil_route_optimizer optimizer;
static ninlil_search_node sn[16];
static ninlil_search_edge se[64];
static ninlil_peer_policy policies[8];
static ninlil_network_plan records[128];
static unsigned int record_count;
static int refuse, writer_error;

static int policy(void *ctx, uint16_t peer, ninlil_peer_policy *out)
{
    (void)ctx;
    if (!peer || peer > 7u)
        return NINLIL_ERR_UNAUTHORIZED;
    *out = policies[peer];
    return NINLIL_OK;
}
static int commit(void *ctx, const ninlil_network_plan *p)
{
    uint8_t bytes[NINLIL_NETWORK_PLAN_MAX];
    ninlil_network_plan decoded;
    (void)ctx;
    if (writer_error)
        return NINLIL_ERR_IO;
    if (record_count == 128u ||
        ninlil_network_plan_encode(p, bytes, sizeof(bytes)) != sizeof(bytes) ||
        ninlil_network_plan_decode(bytes, sizeof(bytes), &decoded) != NINLIL_OK)
        return NINLIL_ERR_CORRUPT;
    records[record_count++] = decoded;
    return NINLIL_OK;
}
static int constraint(void *ctx, const ninlil_network_path *path, uint64_t now)
{
    (void)ctx;
    (void)now;
    for (unsigned int i = 0; i < path->count; i++)
        if ((int)path->nodes[i] == refuse)
            return NINLIL_ERR_CAPACITY;
    return NINLIL_OK;
}
static int observe(uint16_t from, uint16_t to, uint32_t queue, uint64_t now)
{
    ninlil_network_edge e = {.from = from,
                             .to = to,
                             .attempts = 8,
                             .delivered = 8,
                             .airtime_us = 10000,
                             .queue_us = queue,
                             .observed_ms = now,
                             .membership_epoch = 1,
                             .used = 1};
    return ninlil_coordinator_observe(&c, from, &e, now);
}
static int initialize(void)
{
    record_count = 0;
    refuse = writer_error = 0;
    for (unsigned int i = 1; i <= 7; i++) {
        policies[i] = (ninlil_peer_policy){0};
        policies[i].membership_epoch = policies[i].session_membership_epoch = 1;
        policies[i].role = NINLIL_ROLE_POWERED_ENDPOINT;
        if (i == 2 || i == 3)
            policies[i].capabilities = NINLIL_CAP_RELAY_CUSTODY;
    }
    CHECK(ninlil_coordinator_open(&c, graph, 16, edges, 64, 1, policy, NULL,
                                  commit, NULL) == 0);
    CHECK(ninlil_route_optimizer_open(&optimizer, sn, 16, se, 64, 4096,
                                      constraint, NULL) == 0);
    CHECK(ninlil_route_optimizer_attach(&optimizer, &c) == 0);
    for (uint16_t relay = 2; relay <= 3; relay++) {
        CHECK(observe(1, relay, relay == 2 ? 0u : 10000u, 1000) == 0);
        CHECK(observe(relay, 1, 0, 1000) == 0);
        for (uint16_t target = 4; target <= 7; target++) {
            CHECK(observe(relay, target, 0, 1000) == 0);
            CHECK(observe(target, relay, 0, 1000) == 0);
        }
    }
    return 0;
}
static int select_path(uint16_t target, uint64_t *now,
                       ninlil_network_path *path)
{
    int rc = NINLIL_ERR_BUSY;
    for (unsigned int i = 0; i < 256 && rc == NINLIL_ERR_BUSY; i++) {
        rc = ninlil_coordinator_select(&c, 1, target, 0, *now, path);
        if (rc == NINLIL_ERR_BUSY) {
            int step = ninlil_route_optimizer_step(&optimizer, &c, ++*now, 64);
            CHECK(step == NINLIL_OK || step == NINLIL_ERR_BUSY ||
                  step == NINLIL_ERR_EMPTY ||
                  optimizer.ready); /* select consumes the completed request,
                                       including refusals */
        }
    }
    return rc;
}
static int apply(ninlil_network_path *path, uint64_t now)
{
    uint64_t epoch;
    CHECK(ninlil_coordinator_stage(&c, path, now, now + 60000,
                                   NINLIL_TIME_RESTART_SAFE) == 0);
    epoch = c.pending.epoch;
    CHECK(ninlil_coordinator_activate(&c, now, NINLIL_TIME_RESTART_SAFE, 1) ==
          NINLIL_ERR_STATE);
    for (unsigned int i = 0; i < path->count; i++)
        CHECK(ninlil_coordinator_prepared(&c, path->nodes[i], epoch) == 0);
    CHECK(ninlil_coordinator_activate(&c, now, NINLIL_TIME_RESTART_SAFE, 1) ==
          0);
    for (unsigned int i = 0; i < path->count; i++)
        CHECK(ninlil_coordinator_applied(&c, path->nodes[i], epoch) == 0);
    CHECK(c.active.phase == NINLIL_PLAN_EFFECTIVE && !c.pending.epoch);
    return 0;
}
int main(void)
{
    uint64_t now = 1000;
    ninlil_network_path path, before, other;
    ninlil_network_plan route;
    unsigned int saved;
    CHECK(initialize() == 0);
    memset(&path, 0xa5, sizeof(path));
    before = path;
    CHECK(ninlil_coordinator_select(&c, 1, 4, 0, now, &path) ==
          NINLIL_ERR_BUSY);
    CHECK(!memcmp(&path, &before, sizeof(path)) && record_count == 0);
    CHECK(ninlil_coordinator_select(&c, 1, 5, 0, now, &path) ==
          NINLIL_ERR_BUSY);
    CHECK(optimizer.target ==
          4); /* another flow does not reset the in-flight search */
    CHECK(select_path(4, &now, &path) == 0 && path.nodes[1] == 2);
    CHECK(optimizer.result.count >= 2 && !optimizer.result.declared_disjoint);
    CHECK(apply(&path, now) == 0);
    CHECK(ninlil_coordinator_route(&c, 1, 4, now, &route) == 0);
    /* A different flow commits later; it must not extend this flow's hold. */
    now += 3000;
    CHECK(select_path(5, &now, &other) == 0 && apply(&other, now) == 0);
    CHECK(observe(1, 2, 1000000, now) == 0);
    CHECK(select_path(4, &now, &path) == 0 && path.nodes[1] == 3);
    saved = record_count;
    CHECK(ninlil_coordinator_stage(&c, &path, now, now + 60000,
                                   NINLIL_TIME_RESTART_SAFE) == 0);
    for (unsigned int i = 0; i < path.count; i++)
        CHECK(ninlil_coordinator_prepared(&c, path.nodes[i], c.pending.epoch) ==
              0);
    CHECK(ninlil_coordinator_activate(&c, now, NINLIL_TIME_RESTART_SAFE, 0) ==
          NINLIL_ERR_STATE);
    CHECK(ninlil_coordinator_abort(&c) == 0 && record_count > saved);
    /* Changed authority must be rejected before any durable write. */
    policies[4].membership_epoch = policies[4].session_membership_epoch = 2;
    saved = record_count;
    CHECK(ninlil_coordinator_stage(&c, &path, now, now + 60000,
                                   NINLIL_TIME_RESTART_SAFE) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(record_count == saved && !c.pending.epoch);
    policies[4].membership_epoch = policies[4].session_membership_epoch = 1;
    /* Current capacity refusal is checked AGAIN at stage, not only at search.
     */
    refuse = 3;
    saved = record_count;
    CHECK(ninlil_coordinator_stage(&c, &path, now, now + 60000,
                                   NINLIL_TIME_RESTART_SAFE) ==
          NINLIL_ERR_CAPACITY);
    CHECK(record_count == saved);
    refuse = 0;
    /* Missing reverse evidence and battery capability reject otherwise cheap
     * paths. */
    ninlil_coordinator_disconnect(&c, 3);
    CHECK(observe(1, 3, 0, now) == 0 && observe(3, 4, 0, now) == 0);
    CHECK(select_path(4, &now, &path) == 0 && path.nodes[1] == 2);
    policies[2].role = NINLIL_ROLE_BATTERY_LEAF;
    CHECK(select_path(4, &now, &path) == NINLIL_ERR_NOT_FOUND);
    policies[2].role = NINLIL_ROLE_POWERED_ENDPOINT;
    /* Replaying durable EFFECTIVE alone never restores a usable route. */
    CHECK(ninlil_coordinator_open(&recovered, recovery_graph, 16,
                                  recovery_edges, 64, 1, policy, NULL, commit,
                                  NULL) == 0);
    for (unsigned int i = 0; i < record_count; i++)
        CHECK(ninlil_coordinator_restore(&recovered, &records[i]) == 0);
    CHECK(ninlil_coordinator_route(&recovered, 1, 4, now, &route) != 0);
    /* Ambiguous writer error poisons the actual coordinator, no active plan
     * invented. */
    CHECK(select_path(6, &now, &path) == 0);
    writer_error = 1;
    CHECK(ninlil_coordinator_stage(&c, &path, now, now + 60000,
                                   NINLIL_TIME_RESTART_SAFE) == NINLIL_ERR_IO);
    CHECK(c.poisoned && !c.pending.epoch);
    printf("actual Coordinator/candidate search/stage/replay fences PASS; "
           "workspace=%zu\n",
           sizeof(optimizer));
    return 0;
}
