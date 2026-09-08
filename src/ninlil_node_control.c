#include "ninlil_node_internal.h"
#include <string.h>

ninlil_join_peer *ninlil_node_authority_peer(ninlil_node *n, uint16_t index)
{
    unsigned int i;
    for (i = 0u; i < n->authority.capacity; i++)
        if (n->join_peers[i].used &&
            memcmp(n->join_peers[i].identity, n->members[index].grant.identity,
                   32u) == 0)
            return &n->join_peers[i];
    return NULL;
}
void ninlil_node_membership_changed(ninlil_node *n)
{
    unsigned int i;
    if (n->membership_generation == UINT64_MAX) {
        n->status.fault = NINLIL_ERR_STATE;
        return;
    }
    n->membership_generation++;
    for (i = 0u; i < n->config.member_count; i++)
        n->peers[i].member_acks = 0u;
}
int ninlil_node_record_matches(ninlil_node *n, uint16_t index,
                               const ninlil_join_record *record)
{
    ninlil_join_record expected = *record;
    uint8_t a[NINLIL_JOIN_RECORD_MAX], b[NINLIL_JOIN_RECORD_MAX];
    size_t length;
    expected.grant = n->members[index].grant;
    length = ninlil_join_encode(record, a, sizeof(a));
    return length && ninlil_join_encode(&expected, b, sizeof(b)) == length &&
           memcmp(a, b, length) == 0;
}

int ninlil_node_control_send(ninlil_node *n, uint16_t peer,
                             node_control_kind kind, const uint8_t *data,
                             size_t length)
{
    uint8_t plain[NINLIL_NODE_BOOTSTRAP_PAYLOAD - NINLIL_SECURE_OVERHEAD];
    uint8_t frame[NINLIL_NODE_BOOTSTRAP_PAYLOAD];
    size_t written = 0u;
    int index = ninlil_node_index(n, peer), rc;
    if (index < 0 || length >= sizeof(plain) || (!data && length) ||
        kind < NODE_JOIN_REQUEST || kind > NODE_REMOVE_READY_ACK)
        return NINLIL_ERR_INVALID;
    if (!n->peers[index].sessions[0].ready)
        return NINLIL_ERR_BUSY;
    plain[0] = (uint8_t)kind;
    if (length)
        memcpy(plain + 1, data, length);
    rc =
        ninlil_secure_seal_control(&n->peers[index].sessions[0], plain,
                                   length + 1u, frame, sizeof(frame), &written);
    if (rc == NINLIL_OK)
        rc = ninlil_node_bootstrap_send(
            n, peer, 5u, ninlil_node_get(frame + 24, 5u) + 1u, frame, written);
    ninlil_secret_clear(plain, sizeof(plain));
    return rc;
}
static int send_record(ninlil_node *n, uint16_t index, node_control_kind kind,
                       const ninlil_join_record *record)
{
    uint8_t bytes[NINLIL_JOIN_RECORD_MAX];
    size_t size = ninlil_join_encode(record, bytes, sizeof(bytes));
    return size ? ninlil_node_control_send(n, n->members[index].grant.node,
                                           kind, bytes, size)
                : NINLIL_ERR_INVALID;
}

static int join_root(ninlil_node *n, uint16_t index, node_control_kind kind,
                     const uint8_t *data, size_t length)
{
    ninlil_join_peer *p = ninlil_node_authority_peer(n, index);
    ninlil_join_record record;
    const uint8_t *context = n->peers[index].sessions[0].material.fingerprint;
    int rc;
    if (!p || n->peers[index].revoked || index == n->local_index ||
        (length && ninlil_join_decode(data, length, &record) != NINLIL_OK))
        return NINLIL_ERR_INVALID;
    if (kind == NODE_JOIN_ACK) {
        if (!length || !ninlil_node_record_matches(n, index, &record))
            return NINLIL_ERR_UNAUTHORIZED;
        rc = ninlil_join_confirm(&n->authority, &record, context, n->now_ms);
    } else if (kind == NODE_JOIN_REQUEST) {
        if (p->session_ready) {
            n->peers[index].control_at = n->now_ms + 2000u;
            return send_record(n, index, NODE_JOIN_ACTIVE, &p->record);
        }
        rc = NINLIL_ERR_UNAUTHORIZED;
        if (length && ninlil_node_record_matches(n, index, &record)) {
            /* A lost ACK is retried as the participant's saved ACTIVE record.
             * Confirm the current pending transaction without requiring yet
             * another ACCEPT/ACK round trip; old contexts use normal resume. */
            rc = p->record.state == NINLIL_JOIN_PENDING &&
                         memcmp(record.transaction, context, 16u) == 0
                     ? ninlil_join_confirm(&n->authority, &record, context,
                                           n->now_ms)
                     : ninlil_join_resume(&n->authority, &record, context,
                                          n->now_ms);
        }
        if (rc == NINLIL_ERR_UNAUTHORIZED) {
            rc = ninlil_join_prepare(&n->authority,
                                     n->members[index].grant.identity,
                                     n->now_ms, &record);
            if (rc == NINLIL_ERR_STATE && !p->phase)
                ninlil_node_disconnect(n, index);
            return rc == NINLIL_OK
                       ? send_record(n, index, NODE_JOIN_ACCEPT, &record)
                       : rc;
        }
    } else
        return NINLIL_ERR_UNAUTHORIZED;
    if (rc == NINLIL_OK) {
        if (!n->peers[index].member_active)
            ninlil_node_membership_changed(n);
        n->peers[index].member_active = 1u;
        n->peers[index].control_at = n->now_ms + 2000u;
        return send_record(n, index, NODE_JOIN_ACTIVE, &p->record);
    }
    return rc;
}

static int join_peer(ninlil_node *n, node_control_kind kind,
                     const uint8_t *data, size_t length)
{
    ninlil_join_record record, ack;
    const uint8_t *context =
        n->peers[n->root_index].sessions[0].material.fingerprint;
    int rc;
    if (n->peers[n->local_index].revoked ||
        ninlil_join_decode(data, length, &record) != NINLIL_OK ||
        !ninlil_node_record_matches(n, n->local_index, &record) ||
        memcmp(record.transaction, context, 16u) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    if (kind == NODE_JOIN_ACCEPT) {
        rc = ninlil_join_endpoint_accept(&n->endpoint, &record, context, &ack);
        return rc == NINLIL_OK
                   ? send_record(n, n->root_index, NODE_JOIN_ACK, &ack)
                   : rc;
    }
    if (kind != NODE_JOIN_ACTIVE || record.state != NINLIL_JOIN_ACTIVE)
        return NINLIL_ERR_UNAUTHORIZED;
    if (!n->endpoint.persisted ||
        n->endpoint.record.state != NINLIL_JOIN_ACTIVE ||
        memcmp(n->endpoint.record.transaction, context, 16u) != 0) {
        rc = ninlil_node_join_commit(n, &record);
        if (rc == NINLIL_OK)
            rc = ninlil_join_endpoint_restore(&n->endpoint, &record);
        if (rc != NINLIL_OK)
            return rc;
    }
    n->joined = n->peers[n->local_index].member_active =
        n->peers[n->root_index].member_active = 1u;
    return ninlil_node_control_send(n, n->config.root, NODE_JOIN_READY, NULL,
                                    0u);
}

static void generation(ninlil_node *n, uint64_t value)
{
    unsigned int i;
    if (value <= n->membership_generation)
        return;
    n->membership_generation = value;
    for (i = 0u; i < n->config.member_count; i++)
        if (i != n->local_index && i != n->root_index)
            n->peers[i].member_active = 0u;
}
static int member(ninlil_node *n, const uint8_t *data, size_t length)
{
    ninlil_join_record record;
    uint8_t ack[10], flag;
    uint64_t version;
    int index;
    if (length < 10u ||
        ninlil_join_decode(data + 9, length - 9u, &record) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    version = ninlil_node_get(data, 8u);
    flag = data[8];
    index = ninlil_node_index(n, record.grant.node);
    if (!version || version < n->membership_generation || flag > 2u ||
        index < 0 || index == (int)n->local_index ||
        index == (int)n->root_index ||
        !ninlil_node_record_matches(n, (uint16_t)index, &record) ||
        (flag == 1u && record.state != NINLIL_JOIN_ACTIVE) ||
        (flag == 2u && record.state != NINLIL_JOIN_REVOKED))
        return NINLIL_ERR_UNAUTHORIZED;
    generation(n, version);
    if (flag == 2u)
        n->peers[index].revoked = 1u;
    n->peers[index].member_active =
        flag == 1u && !n->peers[index].revoked ? 1u : 0u;
    if (flag != 1u)
        ninlil_node_disconnect(n, (uint16_t)index);
    ninlil_node_put(ack, version, 8u);
    ninlil_node_put(ack + 8, record.grant.node, 2u);
    return ninlil_node_control_send(n, n->config.root, NODE_MEMBER_ACK, ack,
                                    sizeof(ack));
}

int ninlil_node_control_receive(ninlil_node *n, uint16_t peer,
                                const uint8_t *data, size_t length,
                                int neighbor)
{
    int index = ninlil_node_index(n, peer);
    node_control_kind kind;
    if (index < 0 || !data || !length)
        return NINLIL_ERR_INVALID;
    kind = (node_control_kind)data[0];
    data++;
    length--;
    if (neighbor)
        return (kind == NODE_PROBE || kind == NODE_PROBE_REPLY)
                   ? ninlil_node_plan_receive(n, peer, kind, data, length)
                   : NINLIL_ERR_UNAUTHORIZED;
    /* Authenticated status-only sessions allow durable revocation delivery
     * even after normal membership was disabled. */
    if (kind == NODE_REVOKE_NOTICE || kind == NODE_REVOKE_ACK)
        return ninlil_node_lifecycle_receive(n, peer, kind, data, length);
    if (kind >= NODE_JOIN_REQUEST && kind <= NODE_JOIN_ACTIVE) {
        return n->config.local == n->config.root
                   ? join_root(n, (uint16_t)index, kind, data, length)
               : peer == n->config.root ? join_peer(n, kind, data, length)
                                        : NINLIL_ERR_UNAUTHORIZED;
    }
    if (!n->joined || !n->peers[index].member_active)
        return NINLIL_ERR_STATE;
    if (kind == NODE_JOIN_READY && !length &&
        n->config.local == n->config.root) {
        ninlil_join_peer *p = ninlil_node_authority_peer(n, (uint16_t)index);
        if (!p || !p->session_ready)
            return NINLIL_ERR_STATE;
        n->peers[index].member_ready = 1u;
        return NINLIL_OK;
    }
    if (kind == NODE_CLOCK_REQUEST && n->config.local == n->config.root &&
        length == 8u) {
        uint8_t reply[24];
        uint64_t now;
        int rc = ninlil_node_lease(n, &now);
        if (rc != NINLIL_OK)
            return rc;
        memcpy(reply, data, 8u);
        ninlil_node_put(reply + 8, now, 8u);
        ninlil_node_put(reply + 16, n->membership_generation, 8u);
        return ninlil_node_control_send(n, peer, NODE_CLOCK_REPLY, reply,
                                        sizeof(reply));
    }
    if (kind == NODE_CLOCK_REPLY && peer == n->config.root && length == 24u) {
        int rc = ninlil_lease_accept(&n->clock, ninlil_node_get(data, 8u),
                                     ninlil_node_get(data + 8, 8u), n->now_ms);
        if (rc == NINLIL_OK)
            generation(n, ninlil_node_get(data + 16, 8u));
        return rc;
    }
    if (kind == NODE_MEMBER && peer == n->config.root)
        return member(n, data, length);
    if (kind == NODE_MEMBER_ACK && n->config.local == n->config.root &&
        length == 10u) {
        int described =
            ninlil_node_index(n, (uint16_t)ninlil_node_get(data + 8, 2u));
        if (described < 0 ||
            ninlil_node_get(data, 8u) != n->membership_generation)
            return NINLIL_ERR_STATE;
        n->peers[index].member_acks |=
            (uint16_t)(1u << (unsigned int)described);
        return NINLIL_OK;
    }
    return ninlil_node_plan_receive(n, peer, kind, data, length);
}

static int broadcast_members(ninlil_node *n)
{
    unsigned int i;
    uint16_t total =
        (uint16_t)(n->config.member_count * n->config.member_count);
    for (i = 0u; i < total; i++) {
        uint16_t slot = n->broadcast_cursor;
        uint16_t target = (uint16_t)(slot / n->config.member_count);
        uint16_t described = (uint16_t)(slot % n->config.member_count);
        ninlil_join_peer *p;
        uint8_t bytes[9u + NINLIL_JOIN_RECORD_MAX];
        size_t size;
        n->broadcast_cursor = (uint16_t)((slot + 1u) % total);
        if (target == n->local_index || described == n->local_index ||
            target == described || !n->peers[target].member_active ||
            !n->peers[target].member_ready ||
            n->now_ms < n->peers[target].control_at ||
            !n->peers[target].sessions[0].ready ||
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

int ninlil_node_control_step(ninlil_node *n)
{
    node_peer *root = &n->peers[n->root_index];
    if (n->now_ms < n->control_at)
        return NINLIL_OK;
    n->control_at = n->now_ms + NODE_CONTROL_MS;
    if (n->config.local == n->config.root) {
        for (uint16_t i = 0u; i < n->config.member_count; i++) {
            node_peer *p = &n->peers[i];
            ninlil_join_peer *j = ninlil_node_authority_peer(n, i);
            if (i != n->local_index && p->member_active && !p->member_ready &&
                p->sessions[0].ready && j && j->session_ready &&
                n->now_ms >= p->control_at) {
                p->control_at = n->now_ms + 2000u;
                return send_record(n, i, NODE_JOIN_ACTIVE, &j->record);
            }
        }
        return broadcast_members(n);
    }
    if (!root->sessions[0].ready)
        return NINLIL_OK;
    if (n->peers[n->local_index].revoked)
        return NINLIL_OK;
    if (!n->joined) {
        /* Bootstrap must leave airtime for the larger Join response and its
         * acknowledgement, including a forwarded hop. */
        n->control_at = n->now_ms + 2000u + (n->config.local % 11u) * 73u;
        return n->endpoint.persisted &&
                       n->endpoint.record.state == NINLIL_JOIN_ACTIVE
                   ? send_record(n, n->root_index, NODE_JOIN_REQUEST,
                                 &n->endpoint.record)
                   : ninlil_node_control_send(n, n->config.root,
                                              NODE_JOIN_REQUEST, NULL, 0u);
    }
    if (n->now_ms >= root->control_at) {
        uint8_t request[8];
        uint64_t token;
        int rc = n->config.random.fill(n->config.random.ctx, request,
                                       sizeof(request));
        if (rc != NINLIL_OK)
            return rc;
        token = ninlil_node_get(request, sizeof(request));
        rc = ninlil_lease_request(&n->clock, token, n->now_ms);
        if (rc == NINLIL_OK)
            rc = ninlil_node_control_send(n, n->config.root, NODE_CLOCK_REQUEST,
                                          request, sizeof(request));
        root->control_at = n->now_ms + NINLIL_LEASE_SYNC_MAX_RTT_MS;
        return rc;
    }
    return NINLIL_OK;
}
