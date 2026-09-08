#include "ninlil_node_internal.h"
int ninlil_node_link_quality(ninlil_node *n, uint16_t peer, uint64_t now,
                             uint64_t *observed, uint16_t *delivered)
{
    int index;
    uint8_t bits;
    node_peer *p;
    if (!n || !observed || !delivered || now < n->now_ms)
        return NINLIL_ERR_INVALID;
    index = ninlil_node_index(n, peer);
    if (index < 0)
        return NINLIL_ERR_NOT_FOUND;
    p = &n->peers[index];
    if (!n->joined || p->revoked || !p->member_active || !p->sessions[1].ready)
        return NINLIL_ERR_STATE;
    if (p->attempts != 8u || !p->probe_sent || now < p->probe_sent_at ||
        now - p->probe_sent_at < 3000u)
        return NINLIL_ERR_EMPTY;
    *observed = p->observed_at;
    *delivered = 0u;
    bits = p->probe_window;
    while (bits) {
        *delivered += (uint16_t)(bits & 1u);
        bits = (uint8_t)(bits >> 1);
    }
    return NINLIL_OK;
}
