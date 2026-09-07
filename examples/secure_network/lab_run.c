#include "lab.h"

static int expected_backpressure(int rc)
{
    return rc == NINLIL_OK || rc == NINLIL_ERR_BUSY ||
           rc == NINLIL_ERR_CAPACITY || rc == NINLIL_ERR_UNAUTHORIZED ||
           rc == NINLIL_ERR_NOT_FOUND || rc == NINLIL_ERR_STATE ||
           rc == NINLIL_ERR_CONFLICT;
}

int lab_tick(lab *l)
{
    unsigned int i;
    l->now_ms += 10u;
    l->ticks++;
    for (i = 0u; i < LAB_NODES; i++) {
        lab_node *n = &l->nodes[i];
        if (l->offline[i])
            continue;
        int rc = ninlil_routed_poll(&n->routed, l->now_ms);
        ASSERT(expected_backpressure(rc));
        rc = ninlil_step(n->core);
        ASSERT(expected_backpressure(rc));
    }
    for (i = 0u; i < LAB_NODES; i++) {
        lab_node *n = &l->nodes[i];
        if (l->offline[i])
            continue;
        const ninlil_airtime_job *job;
        int drop = 0;
        int rc = ninlil_airtime_next(&n->scheduler, l->now_ms * 1000u, &job);
        if (rc == NINLIL_ERR_EMPTY || rc == NINLIL_ERR_BUSY)
            continue;
        REQUIRE(rc);
        ASSERT(job->peer >= 1u && job->peer <= LAB_NODES &&
               job->length <= 232u);
        l->physical_attempts++;
        if ((n->id == 1u && job->peer == 4u) ||
            (n->id == 4u && job->peer == 1u))
            drop = ++l->weak_attempts[n->id == 1u ? 0u : 1u] % 4u != 0u;
        if (l->drop_ack && n->id == 4u &&
            job->traffic == NINLIL_TRAFFIC_CONTROL) {
            drop = 1;
            l->drop_ack = 0;
        }
        if (l->offline[job->peer - 1u])
            drop = 1;
        if (drop)
            l->dropped++;
        else {
            rc = ninlil_routed_receive(&l->nodes[job->peer - 1u].routed,
                                       job->frame, job->length, l->now_ms);
            ASSERT(expected_backpressure(rc));
        }
        /* Model TX_DONE can occur even when the remote frame is lost. */
        REQUIRE(ninlil_airtime_complete(&n->scheduler, NINLIL_OK));
    }
    return 0;
}
