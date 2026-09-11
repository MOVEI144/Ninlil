#include "ninlil_node_internal.h"

int ninlil_node_sync_step(ninlil_node *n)
{
    node_peer *root = &n->peers[n->root_index];
    uint8_t request[8];
    uint64_t token = n->clock.challenge;
    int rc;
    if (n->now_ms < root->control_at)
        return NINLIL_OK;
    if (token &&
        n->now_ms - n->clock.request_ms >= NINLIL_LEASE_SYNC_MAX_RTT_MS) {
        /* An unreachable Root gets bounded retry windows, not continuous
         * floods. Never extend the challenge's original acceptance deadline. */
        if (n->now_ms - n->clock.request_ms < NINLIL_LEASE_SYNC_MAX_AGE_MS) {
            root->control_at =
                n->clock.request_ms + NINLIL_LEASE_SYNC_MAX_AGE_MS;
            return NINLIL_OK;
        }
        token = 0u;
    }
    if (!token) {
        rc = n->config.random.fill(n->config.random.ctx, request,
                                   sizeof(request));
        if (rc != NINLIL_OK)
            return rc;
        token = ninlil_node_get(request, sizeof(request));
    }
    ninlil_node_put(request, token, sizeof(request));
    rc = ninlil_lease_request(&n->clock, token, n->now_ms);
    if (rc == NINLIL_OK)
        rc = ninlil_node_control_send(n, n->config.root, NODE_CLOCK_REQUEST,
                                      request, sizeof(request));
    root->control_at = n->now_ms + 2000u + token % 501u;
    return rc;
}
