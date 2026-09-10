#include "ninlil_node_internal.h"
#include "ninlil_sleep.h"

int ninlil_node_suspend(ninlil_node *n, uint64_t now)
{
    int rc;
    if (!n || now < n->now_ms || now > UINT64_MAX - NINLIL_EDHOC_DEADLINE_MS ||
        n->config.local == n->config.root ||
        n->members[n->local_index].grant.role != NINLIL_ROLE_BATTERY_LEAF)
        return NINLIL_ERR_INVALID;
    rc = n->status.fault ? n->status.fault : ninlil_verify_retained(n->core);
    if (rc != NINLIL_OK)
        return rc;
    if (n->config.radio_observer.suspend) {
        rc =
            n->config.radio_observer.suspend(n->config.radio_observer.ctx, now);
        if (rc != NINLIL_OK)
            return rc;
    }
    if (n->config.probe_monitor) {
        rc = ninlil_probe_monitor_pause(n->config.probe_monitor, now);
        if (rc != NINLIL_OK)
            return rc;
    }
    ninlil_edhoc_close(&n->handshake);
    n->handshake_peer = NODE_NO_PEER;
    ninlil_lease_invalidate(&n->clock);
    n->now_ms = now;
    n->sleeping = 1u;
    return NINLIL_OK;
}
int ninlil_node_resume(ninlil_node *n, uint64_t now)
{
    int rc;
    if (!n || !n->sleeping || now < n->now_ms ||
        now > UINT64_MAX - NINLIL_EDHOC_DEADLINE_MS)
        return NINLIL_ERR_STATE;
    rc = n->status.fault ? n->status.fault : ninlil_health(n->core);
    if (rc != NINLIL_OK)
        return rc;
    /* Retain session keys and their monotonically advancing counter stores.
     * Only authenticated renewed time can make an unexpired route usable. */
    ninlil_lease_invalidate(&n->clock);
    n->now_ms = now;
    n->clock.last_local_ms = now;
    n->peers[n->root_index].control_at = now;
    n->sleeping = 0u;
    return NINLIL_OK;
}
