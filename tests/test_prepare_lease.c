/* Real coordinator codec/replay: preparation time is not active lease time. */
#define main legacy_network_tests
int legacy_network_tests(void);
#include "test_network_restart.c"
#undef main

static int failed_commit(void *ctx, const ninlil_network_plan *plan)
{
    (void)ctx;
    (void)plan;
    return NINLIL_ERR_IO;
}
int main(void)
{
    fixture f = {0};
    ninlil_network_path path = {.nodes = {1u, 2u, 4u}, .count = 3u};
    ninlil_network_plan committed, decoded, bad;
    uint8_t wire[NINLIL_NETWORK_PLAN_MAX];
    uint64_t epoch;
    open_coordinator(&f);
    f.coordinator.separate_prepare_lease = 1u;
    observe(&f, 1u, 2u);
    observe(&f, 2u, 4u);
    epoch = stage(&f, &path, 100u, 60100u);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 30100u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_OK);
    committed = f.coordinator.pending;
    REQUIRE(committed.prepare_until_ms == 60100u &&
            committed.valid_until_ms == 90100u);
    REQUIRE(ninlil_network_plan_encode(&committed, wire, sizeof(wire)) ==
            sizeof(wire));
    REQUIRE(wire[2] == 2u && ninlil_network_plan_decode(wire, sizeof(wire),
                                                        &decoded) == NINLIL_OK);
    REQUIRE(decoded.prepare_until_ms == 60100u &&
            decoded.valid_until_ms == 90100u);
    wire[2] = 1u;
    REQUIRE(ninlil_network_plan_decode(wire, sizeof(wire), &decoded) ==
            NINLIL_ERR_INVALID);
    wire[2] = 3u;
    REQUIRE(ninlil_network_plan_decode(wire, sizeof(wire), &decoded) ==
            NINLIL_ERR_INVALID);
    replay(&f);
    REQUIRE(f.coordinator.pending.valid_until_ms == 90100u &&
            !f.coordinator.applied_live);
    bad = committed;
    bad.valid_until_ms++;
    REQUIRE(ninlil_coordinator_restore(&f.coordinator, &bad) ==
            NINLIL_ERR_CORRUPT);
    for (unsigned int i = 0u; i < path.count; i++)
        REQUIRE(ninlil_coordinator_applied(&f.coordinator, path.nodes[i],
                                           epoch) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_route(&f.coordinator, 1u, 4u, 90099u,
                                     &decoded) == NINLIL_OK);
    REQUIRE(ninlil_coordinator_route(&f.coordinator, 1u, 4u, 90100u,
                                     &decoded) != NINLIL_OK);
    memset(&f, 0, sizeof(f));
    open_coordinator(&f);
    f.coordinator.separate_prepare_lease = 1u;
    observe(&f, 1u, 2u);
    observe(&f, 2u, 4u);
    (void)stage(&f, &path, 100u, 60100u);
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 60100u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_ERR_STATE);
    f.coordinator.commit = failed_commit;
    REQUIRE(ninlil_coordinator_activate(&f.coordinator, 30100u,
                                        NINLIL_TIME_RESTART_SAFE,
                                        0) == NINLIL_ERR_IO);
    REQUIRE(f.coordinator.poisoned &&
            f.coordinator.pending.phase == NINLIL_PLAN_STAGED &&
            f.coordinator.pending.valid_until_ms == 60100u);
    puts("NP1/NP2 bounds, preparation/active deadlines, committed replay and "
         "failed publication PASS");
    return 0;
}
