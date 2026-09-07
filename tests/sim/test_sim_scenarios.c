#define _POSIX_C_SOURCE 200809L
#include "sim.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static sim_manifest baseline(void)
{
    sim_manifest m = {0};
    m.version = 1u;
    m.seed = 42u;
    m.nodes = 5u;
    m.burst = 4u;
    m.payload_bytes = 52u;
    m.sf = 7u;
    m.duration_ms = 180000u;
    m.latency_target_ms = 180000u;
    return m;
}

static int advance_until(sim_run *run, uint64_t until_us)
{
    while (run->network.now_us < until_us) {
        int rc = sim_run_tick(run);
        if (rc != NINLIL_OK)
            return rc;
    }
    return NINLIL_OK;
}

static int all_satisfied(const sim_run *run)
{
    size_t index;
    for (index = 0u; index < run->count; index++)
        if (run->messages[index].admitted && !run->messages[index].satisfied)
            return 0;
    return 1;
}

static int test_fault_campaign(sim_run *run, uint32_t seed, uint32_t restart)
{
    sim_manifest m = baseline();
    m.seed = seed;
    m.loss_permille = 100u;
    m.duplicate_permille = 250u;
    m.offline_node = 5u;
    m.offline_start_ms = 1000u;
    m.offline_end_ms = 60000u;
    m.restart_node = restart;
    m.restart_ms = 5000u;
    m.receipt_hold_ms = 10000u;
    CHECK(sim_run_open(run, &m) == NINLIL_OK);
    CHECK(advance_until(run, 60000000u) == NINLIL_OK);
    CHECK(sim_run_tick(run) == NINLIL_OK);
    CHECK(run->isolated_active > 0u && run->healthy_satisfied == 24u);
    CHECK(advance_until(run, (uint64_t)m.duration_ms * 1000u) == NINLIL_OK);
    CHECK(run->admitted == 32u && run->rejected == 0u && all_satisfied(run));
    CHECK(run->network.lost_data > 0u && run->network.lost_receipt > 0u);
    CHECK(run->network.duplicates > 0u && run->network.rx_overflow == 0u);
    CHECK(run->restarts == 1u && run->reoffers == (restart == 1u ? 16u : 4u));
    sim_run_close(run);
    return 0;
}

static int test_missing_receipts(sim_run *run)
{
    sim_manifest m = baseline();
    size_t index;
    FILE *report;
    m.receipt_hold_ms = m.duration_ms;
    CHECK(sim_run_open(run, &m) == NINLIL_OK);
    CHECK(advance_until(run, (uint64_t)m.duration_ms * 1000u) == NINLIL_OK);
    for (index = 0u; index < run->count; index++) {
        CHECK(run->messages[index].admitted && !run->messages[index].satisfied);
        CHECK(run->messages[index].offers == 1u);
    }
    report = tmpfile();
    CHECK(report != NULL);
    CHECK(sim_run_finish(run, report) == NINLIL_ERR_TIMEOUT);
    CHECK(fclose(report) == 0);
    sim_run_close(run);
    return 0;
}

static int test_capacity(sim_run *run)
{
    sim_manifest m = baseline();
    m.burst = SIM_BURST;
    m.duration_ms = 300000u;
    m.latency_target_ms = 300000u;
    CHECK(sim_run_open(run, &m) == NINLIL_OK);
    CHECK(sim_run_tick(run) == NINLIL_OK);
    /* Gateway NORMAL capacity 80, endpoints NORMAL capacity 20 each. */
    CHECK(run->offered == 192u && run->admitted == 160u &&
          run->rejected == 32u);
    CHECK(advance_until(run, (uint64_t)m.duration_ms * 1000u) == NINLIL_OK);
    CHECK(all_satisfied(run));
    CHECK(run->offered == run->admitted + run->rejected);
    sim_run_close(run);
    return 0;
}

static int test_periodic_slow_profile(sim_run *run)
{
    sim_manifest m = baseline();
    m.sf = 12u;
    m.nodes = 2u;
    m.burst = 2u;
    m.interval_ms = 10000u;
    m.duration_ms = 300000u;
    m.latency_target_ms = 300000u;
    CHECK(sim_run_open(run, &m) == NINLIL_OK);
    CHECK(advance_until(run, (uint64_t)m.duration_ms * 1000u) == NINLIL_OK);
    CHECK(run->admitted == 4u && all_satisfied(run));
    CHECK(run->network.data_airtime_us >= 4u * sim_airtime_us(12u, 92u));
    CHECK(run->network.receipt_airtime_us >= 4u * sim_airtime_us(12u, 26u));
    sim_run_close(run);
    return 0;
}

static int test_process_crash(sim_run *run)
{
    sim_manifest m = baseline();
    pid_t child;
    int status;
    uint32_t node;
    m.receipt_hold_ms = 10000u;
    CHECK(sim_run_open(run, &m) == NINLIL_OK);
    CHECK(sim_run_tick(run) == NINLIL_OK);
    CHECK(run->admitted == 32u);
    /* Release parent file locks before fork. The child then owns all journals,
     * commits actual radio delivery, and exits without ninlil_close. */
    for (node = 0u; node < m.nodes; node++) {
        ninlil_close(run->network.nodes[node].runtime);
        run->network.nodes[node].runtime = NULL;
    }
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        for (node = 1u; node <= m.nodes; node++)
            if (sim_run_reopen(run, node) != NINLIL_OK)
                _exit(1);
        if (advance_until(run, 5000000u) != NINLIL_OK)
            _exit(2);
        _exit(77);
    }
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
    run->network.now_us = 5000000u;
    run->network.in_flight = 0;
    for (node = 1u; node <= m.nodes; node++)
        CHECK(sim_run_reopen(run, node) == NINLIL_OK);
    CHECK(run->reoffers == 32u);
    CHECK(advance_until(run, (uint64_t)m.duration_ms * 1000u) == NINLIL_OK);
    CHECK(all_satisfied(run));
    sim_run_close(run);
    return 0;
}

int main(void)
{
    sim_run *run = calloc(1u, sizeof(*run));
    int rc;
    if (!run)
        return 1;
    rc = test_fault_campaign(run, 7u, 1u) ||
         test_fault_campaign(run, 42u, 1u) ||
         test_fault_campaign(run, 20260907u, 2u) ||
         test_missing_receipts(run) || test_capacity(run) ||
         test_periodic_slow_profile(run) || test_process_crash(run);
    if (rc != 0)
        sim_run_close(run);
    free(run);
    if (rc == 0)
        puts("sim scenarios: loss, duplicate, isolated peer, restart, receipt "
             "ambiguity, capacity, periodic SF12, abrupt process exit PASS");
    return rc;
}
