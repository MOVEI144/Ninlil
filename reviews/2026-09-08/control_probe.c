/* Review-only witnesses for the unmodified f5b5b7e implementation.
 * A REPRODUCED line confirms the bad behavior, not a correctness pass. */
#include "ninlil_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
typedef struct fixture {
    ninlil_coordinator coordinator;
    ninlil_network_node nodes[4];
    ninlil_network_edge edges[12];
    ninlil_network_plan records[64];
    size_t count;
} fixture;

static int policy(void *ctx, uint16_t peer, ninlil_peer_policy *out)
{
    (void)ctx;
    if (!peer || peer > 4u)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(out, 0, sizeof(*out));
    out->role = NINLIL_ROLE_POWERED_ENDPOINT;
    out->capabilities = NINLIL_CAP_RELAY_CUSTODY;
    out->membership_epoch = out->session_membership_epoch = 1u;
    return NINLIL_OK;
}
static int commit(void *ctx, const ninlil_network_plan *plan)
{
    fixture *f = ctx;
    REQUIRE(f->count < 64u);
    f->records[f->count++] = *plan;
    return NINLIL_OK;
}
static void open_coordinator(fixture *f)
{
    REQUIRE(ninlil_coordinator_open(&f->coordinator, f->nodes, 4u, f->edges,
                                    12u, 1u, policy, f, commit,
                                    f) == NINLIL_OK);
}
static void observe(fixture *f, uint16_t from, uint16_t to)
{
    ninlil_network_edge edge = {0};
    edge.from = from;
    edge.to = to;
    edge.attempts = edge.delivered = 10u;
    edge.airtime_us = 1000u;
    edge.membership_epoch = 1u;
    edge.observed_ms = 100u;
    REQUIRE(ninlil_coordinator_observe(&f->coordinator, from, &edge, 100u) ==
            NINLIL_OK);
}
static uint64_t stage(fixture *f, const ninlil_network_path *path, uint64_t now,
                      uint64_t until)
{
    uint64_t epoch;
    REQUIRE(ninlil_coordinator_stage(&f->coordinator, path, now, until,
                                     NINLIL_TIME_RESTART_SAFE) == NINLIL_OK);
    epoch = f->coordinator.pending.epoch;
    for (unsigned int i = 0u; i < path->count; i++)
        REQUIRE(ninlil_coordinator_prepared(&f->coordinator, path->nodes[i],
                                            epoch) == NINLIL_OK);
    return epoch;
}
static uint64_t install(fixture *f, const ninlil_network_path *path,
                        uint64_t now, uint64_t until)
{
    uint64_t epoch = stage(f, path, now, until);
    REQUIRE(ninlil_coordinator_activate(&f->coordinator, now,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    for (unsigned int i = 0u; i < path->count; i++)
        REQUIRE(ninlil_coordinator_applied(&f->coordinator, path->nodes[i],
                                           epoch) == NINLIL_OK);
    return epoch;
}
static void replay(fixture *f)
{
    open_coordinator(f);
    for (size_t i = 0u; i < f->count; i++)
        REQUIRE(ninlil_coordinator_restore(&f->coordinator, &f->records[i]) ==
                NINLIL_OK);
}
static void abort_committed(void)
{
    fixture f = {0};
    ninlil_network_path old = {.nodes = {1u, 2u, 4u}, .count = 3u};
    ninlil_network_path next = {.nodes = {1u, 3u, 4u}, .count = 3u};
    uint64_t first, second;
    open_coordinator(&f);
    observe(&f, 1u, 2u);
    observe(&f, 2u, 4u);
    observe(&f, 1u, 3u);
    observe(&f, 3u, 4u);
    first = stage(&f, &old, 100u, 50000u);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 101u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_applied(&f.coordinator, 2u, first) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_abort(&f.coordinator) == NINLIL_OK);
    second = stage(&f, &next, 102u, 50100u);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 103u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    printf("REPRODUCED abort: epoch=%llu partly applied until=50000; "
           "epoch=%llu activated at=103 without release\n",
           (unsigned long long)first, (unsigned long long)second);
}
static void restore_wrong_active(void)
{
    fixture f = {0};
    ninlil_network_path a = {.nodes = {1u, 4u}, .count = 2u};
    ninlil_network_path b = {.nodes = {2u, 3u}, .count = 2u};
    ninlil_network_path replacement = {.nodes = {1u, 3u, 4u}, .count = 3u};
    uint64_t a_epoch, pending;
    open_coordinator(&f);
    observe(&f, 1u, 4u);
    observe(&f, 2u, 3u);
    observe(&f, 1u, 3u);
    observe(&f, 3u, 4u);
    a_epoch = install(&f, &a, 100u, 50100u);
    (void)install(&f, &b, 110u, 1000u);
    pending = stage(&f, &replacement, 200u, 5000u);
    REQUIRE(f.coordinator.active.epoch == a_epoch);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 1001u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_ERR_STATE);
    replay(&f);
    REQUIRE(f.coordinator.active.path.nodes[0] == 2u);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 1001u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    printf("REPRODUCED replay fence: flow 1->4 epoch=%llu until=50100; "
           "pending=%llu activated at=1001 using flow 2->3 until=1000\n",
           (unsigned long long)a_epoch, (unsigned long long)pending);
}
static void restore_partial_application(void)
{
    fixture f = {0};
    ninlil_network_path path = {.nodes = {1u, 2u}, .count = 2u};
    uint64_t epoch;
    open_coordinator(&f);
    observe(&f, 1u, 2u);
    epoch = stage(&f, &path, 100u, 50000u);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 101u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_applied(&f.coordinator, 1u, epoch) == NINLIL_OK);
    replay(&f);
    REQUIRE(f.coordinator.pending.applied == 1u);
    REQUIRE(ninlil_coordinator_applied(&f.coordinator, 2u, epoch) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_route_check(&f.coordinator, &path, epoch,
                                           200u) == NINLIL_OK);
    printf("REPRODUCED partial replay: epoch=%llu usable after restart with "
           "fresh report only from node 2; node 1 never reconciled\n",
           (unsigned long long)epoch);
}
int main(void)
{
    abort_committed();
    restore_wrong_active();
    restore_partial_application();
    return 0;
}
