#include "ninlil_node_internal.h"
#include <string.h>

static int frame_digest(const uint8_t *frame, size_t length, uint8_t digest[16])
{
    uint8_t canonical[NINLIL_SECURE_FRAME_MAX];
    if (!frame || length <= NINLIL_NODE_BOOTSTRAP_HEADER ||
        length > sizeof(canonical) || memcmp(frame, "NB\001", 3u))
        return NINLIL_ERR_INVALID;
    memcpy(canonical, frame, length);
    canonical[3] &= 31u;
    return ninlil_psa_packet_digest(canonical, length, digest);
}
static int allowed(ninlil_node *n)
{
    const ninlil_join_grant *g = &n->members[n->local_index].grant;
    return !n->peers[n->local_index].revoked &&
           g->role != NINLIL_ROLE_BATTERY_LEAF &&
           (g->capabilities & NINLIL_CAP_RELAY_CUSTODY);
}
int ninlil_node_forward_current(ninlil_node *n, const uint8_t *frame,
                                size_t length)
{
    uint8_t digest[16];
    int rc = frame_digest(frame, length, digest);
    if (rc != NINLIL_OK || !allowed(n))
        return NINLIL_ERR_UNAUTHORIZED;
    for (unsigned int i = 0u; i < NODE_FORWARD_MAX; i++)
        if (n->now_ms < n->forwarded[i].until_ms &&
            !memcmp(digest, n->forwarded[i].digest, 16u))
            return NINLIL_OK;
    return NINLIL_ERR_STATE;
}
int ninlil_node_forward_step(ninlil_node *n)
{
    uint8_t digest[16];
    int rc;
    if (!n->forward_length)
        return NINLIL_OK;
    if (n->now_ms >= n->forward_until || !allowed(n)) {
        n->forward_length = 0u;
        return NINLIL_OK;
    }
    if (n->now_ms < n->forward_at)
        return NINLIL_OK;
    rc = frame_digest(n->forward_pending, n->forward_length, digest);
    if (rc != NINLIL_OK)
        return rc;
    /* One volatile slot bridges short queue pressure without occupying the
     * scheduler's reserved local reply slots or taking durable custody. */
    rc = n->config.emit(n->config.emit_ctx,
                        (uint16_t)ninlil_node_get(n->forward_pending + 6, 2u),
                        NINLIL_TRAFFIC_NORMAL, n->forward_pending,
                        n->forward_length);
    n->forward_at = n->now_ms + 100u;
    if (rc == NINLIL_OK) {
        node_forward *entry = &n->forwarded[n->forward_cursor];
        memcpy(entry->digest, digest, 16u);
        entry->until_ms = n->now_ms + NINLIL_EDHOC_DEADLINE_MS;
        n->forward_cursor =
            (uint8_t)((n->forward_cursor + 1u) % NODE_FORWARD_MAX);
        n->forward_length = 0u;
    }
    return rc == NINLIL_ERR_BUSY || rc == NINLIL_ERR_CAPACITY ? NINLIL_OK : rc;
}
int ninlil_node_forward(ninlil_node *n, const uint8_t *frame, size_t length)
{
    uint8_t digest[16], pending[16], hops;
    int rc = frame_digest(frame, length, digest);
    if (rc != NINLIL_OK)
        return rc;
    hops = (uint8_t)(frame[3] >> 5);
    if (!allowed(n) || hops <= 1u)
        return NINLIL_OK;
    if (ninlil_node_forward_current(n, frame, length) == NINLIL_OK)
        return NINLIL_OK;
    if (n->forward_length && n->now_ms < n->forward_until) {
        rc = frame_digest(n->forward_pending, n->forward_length, pending);
        return rc == NINLIL_OK && !memcmp(digest, pending, 16u)
                   ? NINLIL_OK
                   : NINLIL_ERR_CAPACITY;
    }
    memcpy(n->forward_pending, frame, length);
    n->forward_pending[3] = (uint8_t)(((hops - 1u) << 5) | (frame[3] & 31u));
    n->forward_length = (uint16_t)length;
    n->forward_at = n->now_ms;
    n->forward_until = n->now_ms + 2000u;
    return ninlil_node_forward_step(n);
}
