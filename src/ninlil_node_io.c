#include "ninlil_node_internal.h"
#include "ninlil_route_optimizer.h"
#include <string.h>

static int enter(ninlil_node *n, uint64_t now)
{
    if (!n || now < n->now_ms || now > UINT64_MAX - NINLIL_EDHOC_DEADLINE_MS)
        return NINLIL_ERR_INVALID;
    if (n->sleeping || n->config.offline)
        return NINLIL_ERR_BUSY;
    n->now_ms = now;
    return n->status.fault;
}

int ninlil_node_step(ninlil_node *n, uint64_t now)
{
    uint64_t lease;
    int rc = enter(n, now);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_node_result(n, ninlil_node_collection_step(n));
    if (rc == NINLIL_OK)
        rc = ninlil_node_result(n, ninlil_node_discovery_step(n));
    if (rc != NINLIL_OK)
        return rc;
    if (n->config.local == n->config.root &&
        now - n->clock.began_ms >= UINT32_MAX - 61000u) {
        rc = ninlil_node_root_clock(n, now);
        if (rc != NINLIL_OK) {
            n->status.fault = rc;
            return rc;
        }
    }
    ninlil_join_expire(&n->authority, now);
    rc = ninlil_node_result(n, ninlil_node_auth_step(n));
    if (rc == NINLIL_OK)
        rc = ninlil_node_result(n, ninlil_node_control_step(n));
    if (rc == NINLIL_OK)
        rc = ninlil_node_result(n, ninlil_node_links_step(n));
    if (rc == NINLIL_OK && n->coordinator.optimizer &&
        n->config.local == n->config.root &&
        ninlil_node_lease(n, &lease) == NINLIL_OK) {
        n->planning = 1u;
        int work = ninlil_route_optimizer_step(n->coordinator.optimizer,
                                               &n->coordinator, lease, 64u);
        n->planning = 0u;
        if (work == NINLIL_ERR_IO || work == NINLIL_ERR_CORRUPT ||
            work == NINLIL_ERR_FAULT)
            rc = ninlil_node_result(n, work);
    }
    if (rc == NINLIL_OK)
        rc = ninlil_node_result(n, ninlil_node_routes_step(n));
    if (rc != NINLIL_OK)
        return rc;
    if (ninlil_node_lease(n, &lease) == NINLIL_OK) {
        rc = ninlil_node_result(n, ninlil_routed_poll(&n->routed, lease));
        if (rc != NINLIL_OK)
            return rc;
    }
    /* A >=10 ms Core tick bounds retries; delayed calls never catch up. */
    if (now >= n->core_at) {
        uint32_t retry_ms = 1000u;
        for (unsigned int i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
            if (n->local_ready[i] == 3u &&
                n->local_plans[i].path.nodes[0] == n->config.local &&
                n->local_plans[i].rto_ms > retry_ms)
                retry_ms = n->local_plans[i].rto_ms;
        (void)ninlil_set_retry_interval(n->core, (retry_ms + 9u) / 10u);
        n->core_at = now + 10u;
        rc = ninlil_node_result(n, ninlil_step(n->core));
        if (ninlil_health(n->core) != NINLIL_OK)
            n->status.fault = ninlil_health(n->core);
    }
    return n->status.fault ? n->status.fault : rc;
}

int ninlil_node_receive(ninlil_node *n, const uint8_t *frame, size_t length,
                        uint64_t now)
{
    uint64_t lease;
    int rc = enter(n, now);
    if (rc != NINLIL_OK)
        return rc;
    if (!frame || length < 3u || length > NINLIL_SECURE_FRAME_MAX)
        return NINLIL_ERR_INVALID;
    if (memcmp(frame, "NB\001", 3u) == 0)
        rc = ninlil_node_bootstrap(n, frame, length);
    else if (length > NINLIL_SECURE_OVERHEAD &&
             memcmp(frame, "NS\001", 3u) == 0 &&
             ninlil_node_get(frame + 6, 2u) == n->config.local) {
        if (frame[31] == 2u) {
            uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
            size_t size = 0u;
            uint16_t peer = (uint16_t)ninlil_node_get(frame + 4, 2u);
            int index = ninlil_node_index(n, peer);
            rc = index < 0 ? NINLIL_ERR_UNAUTHORIZED
                           : ninlil_secure_unseal_neighbor(
                                 &n->peers[index].sessions[1], frame, length,
                                 plain, sizeof(plain), &size);
            if (rc == NINLIL_OK)
                rc = ninlil_node_control_receive(n, peer, plain, size, 1);
            /* Only this authenticated direct-neighbor ingress may feed RF
             * feedback. A routed/E2E control reply is not a direct RF sample.
             * The control handler has checked membership and the challenge. */
            if (rc == NINLIL_OK && size == 9u && plain[0] == NODE_PROBE_REPLY &&
                now >= n->peers[index].probe_sent_at &&
                now - n->peers[index].probe_sent_at < 3000u) {
                const uint8_t *session =
                    n->peers[index].sessions[1].material.fingerprint;
                uint64_t token = ninlil_node_get(plain + 1, 8u);
                if (n->config.probe_monitor)
                    (void)ninlil_probe_monitor_reply(n->config.probe_monitor,
                                                     peer, session, token, now);
                if (n->config.radio_observer.reply)
                    n->config.radio_observer.reply(n->config.radio_observer.ctx,
                                                   peer, token, session, now);
            }
            ninlil_secret_clear(plain, sizeof(plain));
        } else if (frame[31] == 0u && ninlil_node_lease(n, &lease) == NINLIL_OK)
            rc = ninlil_routed_receive(&n->routed, frame, length, lease);
        else
            rc = NINLIL_ERR_STATE;
    } else
        return NINLIL_ERR_EMPTY; /* Ordinary overheard traffic for another hop.
                                  */
    if (rc != NINLIL_OK && n->status.rejected_frames != UINT32_MAX)
        n->status.rejected_frames++;
    (void)ninlil_node_result(n, rc);
    return rc;
}

int ninlil_node_frame_current(ninlil_node *n, const uint8_t *frame,
                              size_t length, uint64_t now)
{
    uint64_t lease;
    int rc = enter(n, now);
    if (rc != NINLIL_OK)
        return rc;
    if (!frame || length < 3u || length > NINLIL_SECURE_FRAME_MAX)
        return NINLIL_ERR_INVALID;
    if (memcmp(frame, "NB\001", 3u) == 0)
        return ninlil_node_auth_current(n, frame, length);
    if (length > NINLIL_SECURE_OVERHEAD && frame[31] == 2u)
        return ninlil_node_probe_current(n, frame, length);
    rc = ninlil_node_lease(n, &lease);
    if (rc != NINLIL_OK)
        return rc;
    n->routed.now_ms = lease;
    return ninlil_routed_frame_current(&n->routed, frame, length);
}

void ninlil_node_transmitted(ninlil_node *n, const uint8_t *frame,
                             size_t length, int result, uint32_t airtime,
                             uint64_t now)
{
    if (enter(n, now) != NINLIL_OK || result != NINLIL_OK || !frame ||
        length <= NINLIL_SECURE_OVERHEAD || length > NINLIL_SECURE_FRAME_MAX ||
        memcmp(frame, "NS\001", 3u) != 0 || frame[31] != 2u || !airtime ||
        airtime > 400000u)
        return;
    ninlil_node_probe_transmitted(n, frame, length, airtime);
}
