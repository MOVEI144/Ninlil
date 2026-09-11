#include "ninlil_enrollment.h"
#include "ninlil_maintenance.h"
#include "ninlil_node_internal.h"
#include <string.h>

size_t ninlil_member_encode(const ninlil_node_member *m, uint8_t *out,
                            size_t capacity)
{
    ninlil_join_record r = {0};
    uint8_t bytes[NINLIL_MEMBER_RECORD_MAX];
    size_t size;
    if (!m || !out || m->public_key[0] != 4u)
        return 0u;
    r.grant = m->grant;
    r.state = NINLIL_JOIN_PENDING;
    r.transaction[0] = 1u;
    memcpy(bytes, "NM\001", 3u);
    memcpy(bytes + 3, m->public_key, 65u);
    size = ninlil_join_encode(&r, bytes + 68, sizeof(bytes) - 68u);
    if (!size || capacity < size + 68u)
        return 0u;
    memcpy(out, bytes, size + 68u);
    return size + 68u;
}

int ninlil_member_decode(const uint8_t *data, size_t length,
                         ninlil_node_member *out)
{
    ninlil_join_record r;
    ninlil_node_member m = {0};
    const uint8_t transaction[16] = {1u};
    if (!data || !out || length < 160u || length > NINLIL_MEMBER_RECORD_MAX ||
        memcmp(data, "NM\001", 3u) != 0 || data[3] != 4u ||
        ninlil_join_decode(data + 68, length - 68u, &r) != NINLIL_OK ||
        r.state != NINLIL_JOIN_PENDING ||
        memcmp(r.transaction, transaction, sizeof(transaction)) != 0)
        return NINLIL_ERR_INVALID;
    m.grant = r.grant;
    memcpy(m.public_key, data + 3, 65u);
    *out = m;
    return NINLIL_OK;
}

int ninlil_node_member_check(ninlil_node *n, const ninlil_node_member *m,
                             uint16_t count)
{
    ninlil_identity_peer peer;
    ninlil_edhoc_config handshake;
    const ninlil_join_grant *g = &m->grant;
    if (ninlil_join_grant_valid(g) != NINLIL_OK ||
        memcmp(g->authority, n->members[n->root_index].grant.authority, 16u))
        return NINLIL_ERR_INVALID;
    if (g->node != n->config.local &&
        ninlil_identity_credentials(&peer, n->config.identity, n->config.local,
                                    g->node, m->public_key, g->identity, 1,
                                    &handshake) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    for (unsigned int i = 0u; i < g->service_count; i++)
        if (g->services[i].maximum_payload_bytes > 64u)
            return NINLIL_ERR_TOO_LARGE;
    for (unsigned int i = 0u; i < count; i++) {
        const ninlil_node_member *old = &n->members[i];
        if (g->node == old->grant.node ||
            !memcmp(g->identity, old->grant.identity, 32u) ||
            !memcmp(m->public_key, old->public_key, 65u))
            return NINLIL_ERR_CONFLICT;
    }
    return NINLIL_OK;
}

static int apply(ninlil_node *n, const ninlil_node_member *m, int persist)
{
    uint8_t bytes[NINLIL_MEMBER_RECORD_MAX], prior[NINLIL_MEMBER_RECORD_MAX];
    size_t length = ninlil_member_encode(m, bytes, sizeof(bytes));
    int index = ninlil_node_index(n, m->grant.node), rc;
    if (!length)
        return NINLIL_ERR_INVALID;
    if (index >= 0) {
        const ninlil_node_member *old = &n->members[index];
        size_t size = ninlil_member_encode(old, prior, sizeof(prior));
        if (length == size && !memcmp(bytes, prior, size))
            return NINLIL_OK;
        if ((uint16_t)index == n->root_index && n->config.authority_key)
            return ninlil_node_replace_root(n, m, persist);
        if ((uint16_t)index == n->local_index ||
            (uint16_t)index == n->root_index ||
            m->grant.membership_epoch <= old->grant.membership_epoch ||
            m->grant.binding_epoch < old->grant.binding_epoch)
            return NINLIL_ERR_CONFLICT;
        if (memcmp(m->grant.identity, old->grant.identity, 32u)) {
            if (!n->config.authority_key ||
                m->grant.binding_epoch <= old->grant.binding_epoch)
                return NINLIL_ERR_CONFLICT;
            if (persist &&
                (rc = ninlil_node_address_idle(n, m->grant.node)) != NINLIL_OK)
                return rc;
        }
        /* Validate replacement independently of its old address/key entry. */
        if (ninlil_join_grant_valid(&m->grant) != NINLIL_OK ||
            memcmp(m->grant.authority, old->grant.authority, 16u))
            return NINLIL_ERR_INVALID;
        {
            ninlil_identity_peer peer;
            ninlil_edhoc_config config;
            if (ninlil_identity_credentials(
                    &peer, n->config.identity, n->config.local, m->grant.node,
                    m->public_key, m->grant.identity, 1, &config) != NINLIL_OK)
                return NINLIL_ERR_INVALID;
        }
        for (unsigned int i = 0u; i < n->config.member_count; i++)
            if ((int)i != index &&
                (!memcmp(m->public_key, n->members[i].public_key, 65u) ||
                 !memcmp(m->grant.identity, n->members[i].grant.identity, 32u)))
                return NINLIL_ERR_CONFLICT;
        for (unsigned int i = 0u; i < m->grant.service_count; i++)
            if (m->grant.services[i].maximum_payload_bytes > 64u)
                return NINLIL_ERR_TOO_LARGE;
    } else {
        if (n->config.member_count >= NINLIL_NODE_MEMBERS_MAX ||
            (!n->config.dynamic_enrollment &&
             n->config.member_count > n->config.resources.active_peers))
            return NINLIL_ERR_CAPACITY;
        rc = ninlil_node_member_check(n, m, n->config.member_count);
        if (rc != NINLIL_OK)
            return rc;
        index = (int)n->config.member_count;
    }
    if (persist) {
        rc = ninlil_control_log_member(n->log, bytes, (uint16_t)length);
        if (rc != NINLIL_OK) {
            if (rc != NINLIL_ERR_CAPACITY)
                n->status.fault = rc;
            return rc;
        }
        if ((uint16_t)index < n->config.member_count) {
            if (n->handshake.opened && n->handshake_peer == (uint16_t)index)
                ninlil_edhoc_close(&n->handshake);
            ninlil_node_disconnect(n, (uint16_t)index);
        }
    }
    if ((uint16_t)index < n->config.member_count &&
        memcmp(n->members[index].grant.identity, m->grant.identity, 32u)) {
        ninlil_join_peer *old = ninlil_node_authority_peer(n, (uint16_t)index);
        if (old)
            memset(old, 0, sizeof(*old));
    }
    n->members[index] = *m;
    n->dynamic_members |= (uint16_t)(1u << (unsigned int)index);
    if ((uint16_t)index == n->config.member_count)
        n->config.member_count++;
    n->peers[index].revoked = 0u;
    return NINLIL_OK;
}

int ninlil_node_member_restore(void *ctx, const uint8_t *data, uint16_t length)
{
    ninlil_node_member m;
    int rc = ninlil_member_decode(data, length, &m);
    return rc == NINLIL_OK ? apply(ctx, &m, 0) : rc;
}
int ninlil_node_enroll(ninlil_node *n, const ninlil_node_member *m)
{
    return n && m && !n->status.fault ? apply(n, m, 1) : NINLIL_ERR_STATE;
}
int ninlil_node_member_inspect(ninlil_node *n, uint16_t address,
                               ninlil_node_member *out)
{
    int index;
    if (!n || !out || n->status.fault)
        return NINLIL_ERR_STATE;
    index = ninlil_node_index(n, address);
    if (index < 0)
        return NINLIL_ERR_NOT_FOUND;
    *out = n->members[index];
    return NINLIL_OK;
}
int ninlil_node_member_snapshot(ninlil_node *n, ninlil_control_log *out)
{
    for (unsigned int i = 0u; i < n->config.member_count; i++) {
        uint8_t bytes[NINLIL_MEMBER_RECORD_MAX];
        if (!(n->dynamic_members & (uint16_t)(1u << i)))
            continue;
        size_t length =
            ninlil_member_encode(&n->members[i], bytes, sizeof(bytes));
        int rc = length
                     ? ninlil_control_log_member(out, bytes, (uint16_t)length)
                     : NINLIL_ERR_CORRUPT;
        if (rc != NINLIL_OK)
            return rc;
    }
    return NINLIL_OK;
}
int ninlil_node_preserve_members(ninlil_node *n)
{
    if (!n || n->status.fault)
        return NINLIL_ERR_STATE;
    for (unsigned int i = 0u; i < n->config.member_count; i++)
        if (i != n->local_index && i != n->root_index)
            n->dynamic_members |= (uint16_t)(1u << i);
    return ninlil_node_collect(n);
}
