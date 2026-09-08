#include "ninlil_node_internal.h"
#include <string.h>
int ninlil_node_epoch_restore(void *ctx, uint64_t epoch)
{
    ninlil_node *n = ctx;
    if (!epoch || epoch < n->local_plan_epoch ||
        epoch < n->coordinator.last_epoch)
        return NINLIL_ERR_CORRUPT;
    n->local_plan_epoch = epoch;
    if (n->config.local == n->config.root)
        n->coordinator.last_epoch = epoch;
    return NINLIL_OK;
}
static const ninlil_network_plan *plan_at(ninlil_node *n, unsigned int i)
{
    if (n->config.local == n->config.root)
        return i == NINLIL_NETWORK_FLOWS_MAX ? &n->coordinator.pending
                                             : &n->coordinator.flows[i].active;
    return i == NINLIL_NETWORK_FLOWS_MAX ? &n->prepared : &n->local_plans[i];
}
static int snapshot(void *ctx, ninlil_control_log *out)
{
    ninlil_node *n = ctx;
    uint64_t last = 0u, fence = n->local_plan_epoch;
    int rc = NINLIL_OK;
    if (n->config.local == n->config.root) {
        for (unsigned int i = 0u; rc == NINLIL_OK && i < n->authority.capacity;
             i++)
            if (n->authority.peers[i].persisted)
                rc =
                    ninlil_control_log_join(out, &n->authority.peers[i].record);
        if (n->coordinator.last_epoch > fence)
            fence = n->coordinator.last_epoch;
    } else if (n->endpoint.persisted)
        rc = ninlil_control_log_join(out, &n->endpoint.record);
    /* Restore requires increasing epochs; retain active flows plus the single
     * pending plan, then fence the highest historical retired/aborted epoch. */
    for (unsigned int i = 0u; rc == NINLIL_OK && i <= NINLIL_NETWORK_FLOWS_MAX;
         i++) {
        const ninlil_network_plan *next = NULL;
        for (unsigned int j = 0u; j <= NINLIL_NETWORK_FLOWS_MAX; j++) {
            const ninlil_network_plan *p = plan_at(n, j);
            if (p->epoch > last && (!next || p->epoch < next->epoch))
                next = p;
        }
        if (!next)
            break;
        rc = ninlil_control_log_plan(out, next);
        last = next->epoch;
    }
    if (rc == NINLIL_OK && fence)
        rc = ninlil_control_log_epoch(out, fence);
    if (n->routed.config.relay) {
        ninlil_relay_record control = {0};
        control.control = 1u;
        control.done = n->relay.draining;
        control.route_epoch = n->relay.drain_epoch;
        if (rc == NINLIL_OK && control.route_epoch)
            rc = ninlil_control_log_relay(out, &control);
        for (unsigned int i = 0u; rc == NINLIL_OK && i < n->relay.capacity; i++)
            if (n->custody[i].used)
                rc = ninlil_control_log_relay(out, &n->custody[i].record);
    }
    return rc;
}
int ninlil_node_collection_step(ninlil_node *n)
{
    return ninlil_control_log_collect(n->log, snapshot, n, 0);
}
int ninlil_node_collect(ninlil_node *n)
{
    int rc;
    if (!n)
        return NINLIL_ERR_INVALID;
    rc = ninlil_collect(n->core);
    if (rc == NINLIL_OK)
        rc = ninlil_control_log_collect(n->log, snapshot, n, 1);
    (void)ninlil_node_result(n, rc);
    return rc;
}
