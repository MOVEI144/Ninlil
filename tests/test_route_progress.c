/* Actual coordinator/plan handler, not a simulated RF timing claim. */
#include "../src/ninlil_node_internal.h"
#define main baseline_restart_cases
int baseline_restart_cases(void);
#include "test_network_restart.c"
#undef main

int main(void)
{
    fixture f = {0};
    ninlil_node *n = calloc(1u, sizeof(*n));
    ninlil_network_path path = {0};
    uint8_t bytes[8];
    uint64_t generation;
    unsigned int slot = 0u;
    REQUIRE(n);
    open_coordinator(&f);
    observe(&f, 1u, 2u);
    observe(&f, 2u, 1u);
    REQUIRE(ninlil_coordinator_select(&f.coordinator, 1u, 2u, 0u, 100u,
                                      &path) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_stage(&f.coordinator, &path, 100u, 10000u,
                                     NINLIL_TIME_RESTART_SAFE) == NINLIL_OK);
    n->config.local = n->config.root = 1u;
    n->now_ms = 100u;
    n->coordinator = f.coordinator;
    generation = n->coordinator.pending.epoch;
    ninlil_node_put(bytes, generation, sizeof(bytes));
    n->route_at = 2100u;
    REQUIRE(ninlil_node_plan_receive(n, 2u, NODE_PREPARED, bytes,
                                     sizeof(bytes)) == NINLIL_OK);
    REQUIRE(n->route_at == 100u && n->coordinator.prepared_live == 2u);
    n->route_at = 2100u;
    REQUIRE(ninlil_node_plan_receive(n, 2u, NODE_PREPARED, bytes,
                                     sizeof(bytes)) == NINLIL_OK);
    REQUIRE(n->route_at == 2100u); /* Duplicate does not accelerate control. */
    ninlil_node_put(bytes, generation + 1u, sizeof(bytes));
    REQUIRE(ninlil_node_plan_receive(n, 2u, NODE_PREPARED, bytes,
                                     sizeof(bytes)) != NINLIL_OK);
    REQUIRE(n->route_at == 2100u && n->coordinator.prepared_live == 2u);
    ninlil_node_put(bytes, generation, sizeof(bytes));
    REQUIRE(ninlil_node_plan_receive(n, 3u, NODE_PREPARED, bytes,
                                     sizeof(bytes)) != NINLIL_OK);
    REQUIRE(n->route_at == 2100u && n->coordinator.prepared_live == 2u);
    REQUIRE(ninlil_coordinator_prepared(&n->coordinator, 1u, generation) ==
            NINLIL_OK);
    REQUIRE(ninlil_coordinator_activate(&n->coordinator, 100u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_applied(&n->coordinator, 1u, generation) ==
            NINLIL_OK);
    REQUIRE(ninlil_coordinator_applied(&n->coordinator, 2u, generation) ==
            NINLIL_OK);
    while (slot < NINLIL_NETWORK_FLOWS_MAX &&
           n->coordinator.flows[slot].active.epoch != generation)
        slot++;
    REQUIRE(slot < NINLIL_NETWORK_FLOWS_MAX);
    n->effective_epoch[slot] = generation;
    REQUIRE(ninlil_node_plan_receive(n, 2u, NODE_EFFECTIVE_ACK, bytes,
                                     sizeof(bytes)) == NINLIL_OK);
    REQUIRE(n->route_at == 100u && n->effective_notified[slot] == 2u);
    n->route_at = 2100u;
    REQUIRE(ninlil_node_plan_receive(n, 2u, NODE_EFFECTIVE_ACK, bytes,
                                     sizeof(bytes)) == NINLIL_OK);
    REQUIRE(n->route_at == 2100u);
    free(n);
    puts("new verified route progress wakes next phase; duplicate/stale ACKs "
         "do not PASS");
    return 0;
}
