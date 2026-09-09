#include "ninlil_node_internal.h"
#include <string.h>

static int core_send(void *ctx, const uint8_t *data, size_t length)
{
    ninlil_node *n = ctx;
    ninlil_peer_policy policy;
    uint16_t peer;
    if (!data || length != 26u || memcmp(data, "NL\002\002", 4u))
        return n->data_link.send(n->data_link.ctx, data, length);
    peer = (uint16_t)ninlil_node_get(data + 6, 2u);
    if (ninlil_node_get(data + 4, 2u) != n->config.local ||
        ninlil_node_policy(n, peer, &policy) != NINLIL_OK ||
        !policy.session_membership_epoch)
        return NINLIL_ERR_BUSY;
    return ninlil_node_control_send(n, peer, NODE_CORE_RECEIPT, data, length);
}
static int core_receive(void *ctx, uint8_t *bytes, size_t capacity,
                        size_t *length)
{
    ninlil_node *n = ctx;
    return n->data_link.recv(n->data_link.ctx, bytes, capacity, length);
}
void ninlil_node_delivery_link(ninlil_node *n, ninlil_link *link)
{
    if (n->config.dynamic_enrollment) {
        n->data_link = *link;
        link->send = core_send;
        link->recv = core_receive;
        link->ctx = n;
    }
}
int ninlil_node_core_receipt(ninlil_node *n, uint16_t peer, const uint8_t *data,
                             size_t length)
{
    if (!n->config.dynamic_enrollment || !data || length != 26u ||
        memcmp(data, "NL\002\002", 4u) ||
        ninlil_node_get(data + 4, 2u) != peer ||
        ninlil_node_get(data + 6, 2u) != n->config.local)
        return NINLIL_ERR_UNAUTHORIZED;
    /* Called only after current E2E authentication and active membership.
     * Core validates the exact outstanding contract and persists evidence.
     * Neither DATA nor a Relay custody acknowledgement enters this path. */
    return ninlil_ingest(n->core, data, length);
}

int ninlil_node_frame_equal(ninlil_node *n, const uint8_t *a, const uint8_t *b,
                            size_t length)
{
    uint8_t x[NINLIL_SECURE_PLAINTEXT_MAX], y[sizeof(x)];
    size_t offset = 0u, nx = 0u, ny = 0u;
    int index, hop = 1, equal;
    ninlil_secure_session *s;
    if (!n || !a || !b || length <= 16u || length > NINLIL_SECURE_FRAME_MAX)
        return 0;
    if (!memcmp(a, "NB\001", 3u) && !memcmp(b, "NB\001", 3u)) {
        if (ninlil_node_get(a + 4, 2u) != n->config.local)
            return !memcmp(a, b, length); /* Opaque forwarded copies. */
        if ((a[3] & 31u) != 5u || (b[3] & 31u) != 5u)
            return !memcmp(a, b, length);
        offset = 16u;
        hop = 0;
    }
    a += offset;
    b += offset;
    length -= offset;
    if (length <= NINLIL_SECURE_OVERHEAD || memcmp(a, "NS\001", 3u) ||
        memcmp(b, "NS\001", 3u) || memcmp(a + 4, b + 4, 20u) ||
        ninlil_node_get(a + 4, 2u) != n->config.local || a[31] != b[31])
        return 0;
    index = ninlil_node_index(n, (uint16_t)ninlil_node_get(a + 6, 2u));
    if (index < 0)
        return 0;
    s = &n->peers[index].sessions[hop];
    equal = ninlil_secure_inspect_tx(s, a, length, x, sizeof(x), &nx) ==
                NINLIL_OK &&
            ninlil_secure_inspect_tx(s, b, length, y, sizeof(y), &ny) ==
                NINLIL_OK &&
            nx == ny && !memcmp(x, y, nx);
    ninlil_secret_clear(x, sizeof(x));
    ninlil_secret_clear(y, sizeof(y));
    return equal;
}

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

int ninlil_node_observe_radio(ninlil_node *n,
                               const ninlil_node_radio_observer *observer)
{
    const ninlil_node_radio_observer *old;
    if (!n || !observer || !observer->reply || !observer->suspend ||
        !observer->ctx || n->status.fault || n->sleeping)
        return NINLIL_ERR_INVALID;
    old = &n->config.radio_observer;
    if ((old->reply || old->suspend || old->ctx) &&
        (old->reply != observer->reply || old->suspend != observer->suspend ||
         old->ctx != observer->ctx))
        return NINLIL_ERR_CONFLICT;
    n->config.radio_observer = *observer;
    return NINLIL_OK;
}

int ninlil_node_radio_tx_context(ninlil_node *n, const uint8_t *frame,
                                  size_t length, ninlil_node_radio_tx *out)
{
    ninlil_node_radio_tx context = {0};
    node_peer *p;
    int index;
    if (!n || !frame || !out || length > NINLIL_SECURE_FRAME_MAX)
        return NINLIL_ERR_INVALID;
    if (length <= NINLIL_SECURE_OVERHEAD || memcmp(frame, "NS\001", 3u) ||
        (frame[31] != 0u && frame[31] != 2u) ||
        ninlil_node_get(frame + 4, 2u) != n->config.local)
        return NINLIL_ERR_EMPTY;
    context.peer = (uint16_t)ninlil_node_get(frame + 6, 2u);
    index = ninlil_node_index(n, context.peer);
    if (index < 0 || !n->joined || n->sleeping || n->status.fault ||
        n->peers[n->local_index].revoked)
        return NINLIL_ERR_STATE;
    p = &n->peers[index];
    if (!p->member_active || p->revoked || !p->sessions[1].ready ||
        memcmp(frame + 8, p->sessions[1].material.fingerprint, 16u))
        return NINLIL_ERR_UNAUTHORIZED;
    context.profile = n->config.permitted_profile;
    memcpy(context.session, p->sessions[1].material.fingerprint, 16u);
    if (frame[31] == 2u) {
        uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
        size_t size = 0u;
        int rc = ninlil_secure_inspect_tx(&p->sessions[1], frame, length,
                                          plain, sizeof(plain), &size);
        if (rc == NINLIL_OK && size == sizeof(plain) &&
            plain[0] == NODE_PROBE && !p->probe_sent &&
            ninlil_node_get(plain + 1, 8u) == p->probe_token)
            context.probe_token = p->probe_token;
        ninlil_secret_clear(plain, sizeof(plain));
        if (rc != NINLIL_OK)
            return rc;
    }
    *out = context;
    return NINLIL_OK;
}
