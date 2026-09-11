#include "ninlil_node_internal.h"

/* A missing descriptor ACK must not block other descriptors for that peer.
 * In particular a new Relay does not know all retired/absent participants. */
int ninlil_node_membership_step(ninlil_node *n)
{
    unsigned int i;
    uint16_t total =
        (uint16_t)(n->config.member_count * n->config.member_count);
    for (i = 0u; i < total; i++) {
        uint16_t slot = n->broadcast_cursor;
        uint16_t target = (uint16_t)(slot / n->config.member_count);
        uint16_t described;
        ninlil_join_peer *p;
        uint8_t bytes[9u + NINLIL_JOIN_RECORD_MAX];
        size_t size;
        n->broadcast_cursor = (uint16_t)((slot + 1u) % total);
        if (target == n->local_index || !n->peers[target].member_active ||
            !n->peers[target].member_ready ||
            n->now_ms < n->peers[target].control_at ||
            !n->peers[target].sessions[0].ready)
            continue;
        described = n->peers[target].membership_cursor;
        n->peers[target].membership_cursor =
            (uint8_t)((described + 1u) % n->config.member_count);
        if (described == n->local_index || described == target ||
            (n->peers[target].member_acks & (uint16_t)(1u << described)))
            continue;
        p = ninlil_node_authority_peer(n, described);
        if (!p || !p->persisted)
            continue;
        ninlil_node_put(bytes, n->membership_generation, 8u);
        bytes[8] = p->record.state == NINLIL_JOIN_REVOKED ? 2u
                   : p->session_ready                     ? 1u
                                                          : 0u;
        size = ninlil_join_encode(&p->record, bytes + 9, sizeof(bytes) - 9u);
        if (!size)
            return NINLIL_ERR_CORRUPT;
        n->peers[target].control_at = n->now_ms + 2000u;
        return ninlil_node_control_send(n, n->members[target].grant.node,
                                        NODE_MEMBER, bytes, size + 9u);
    }
    return NINLIL_OK;
}
