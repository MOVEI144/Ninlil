#include "ninlil_node_internal.h"
#include <string.h>

uint64_t ninlil_node_get(const uint8_t *p, size_t length)
{
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < length; i++)
        value = (value << 8) | p[i];
    return value;
}
void ninlil_node_put(uint8_t *p, uint64_t value, size_t length)
{
    while (length) {
        p[--length] = (uint8_t)value;
        value >>= 8;
    }
}

int ninlil_node_bootstrap_send(ninlil_node *n, uint16_t peer, uint8_t kind,
                               uint64_t token, const uint8_t *data,
                               size_t length)
{
    uint8_t frame[NINLIL_SECURE_FRAME_MAX];
    if (!token || !data || !length || length > NINLIL_NODE_BOOTSTRAP_PAYLOAD ||
        kind < 1u || kind > 5u)
        return NINLIL_ERR_INVALID;
    memcpy(frame, "NB\001", 3u);
    frame[3] = (uint8_t)((NINLIL_NETWORK_HOPS_MAX << 5) | kind);
    ninlil_node_put(frame + 4, n->config.local, 2u);
    ninlil_node_put(frame + 6, peer, 2u);
    ninlil_node_put(frame + 8, token, 8u);
    memcpy(frame + NINLIL_NODE_BOOTSTRAP_HEADER, data, length);
    return n->config.emit(n->config.emit_ctx, peer, NINLIL_TRAFFIC_CONTROL,
                          frame, length + NINLIL_NODE_BOOTSTRAP_HEADER);
}

void ninlil_node_disconnect(ninlil_node *n, uint16_t index)
{
    node_peer *p = &n->peers[index];
    unsigned int hop, i;
    for (hop = 0u; hop < 2u; hop++) {
        ninlil_secure_close(&p->sessions[hop]);
        ninlil_counter_close(&p->counters[hop]);
    }
    p->probe_token = 0u;
    p->member_ready = 0u;
    p->remove_ack_epoch = 0u;
    p->attempts = p->delivered = 0u;
    p->probe_window = p->probe_report = p->probe_sent = 0u;
    p->probe_at = 0u;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
        if (ninlil_node_plan_position(&n->local_plans[i],
                                      n->members[index].grant.node) >= 0) {
            n->local_ready[i] = 0u;
            ninlil_node_need_reconcile(
                n, n->local_plans[i].path.nodes[0],
                n->local_plans[i]
                    .path.nodes[n->local_plans[i].path.count - 1u]);
        }
    n->proof_epoch = 0u;
    n->proof_mask = n->released_mask = 0u;
    if (n->config.local == n->config.root) {
        for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++)
            for (unsigned int at = 0u; at < NINLIL_NETWORK_PATH_MAX; at++)
                if (n->request_peer[i][at] == n->members[index].grant.node)
                    n->request_seen[i][at] = 0u;
        if (p->member_active)
            ninlil_node_membership_changed(n);
        ninlil_join_disconnect(&n->authority, n->members[index].grant.identity);
        ninlil_coordinator_disconnect(&n->coordinator,
                                      n->members[index].grant.node);
        p->member_active = 0u;
    } else if (index == n->root_index) {
        n->drain_ack_epoch = 0u;
        n->removal_ready = 0u;
        ninlil_lease_invalidate(&n->clock);
        n->joined = 0u;
        n->membership_generation = 0u;
        memset(n->local_ready, 0, sizeof(n->local_ready));
        for (i = 0u; i < n->config.member_count; i++)
            n->peers[i].member_active = 0u;
    }
}

static int install_session(ninlil_node *n)
{
    node_peer *p = &n->peers[n->handshake_peer];
    ninlil_session_material material[2];
    uint8_t identity[32];
    uint8_t direction = (uint8_t)(n->handshake.config.initiator ? 0u : 1u);
    unsigned int hop;
    int rc = ninlil_edhoc_material(&n->handshake, &material[0], identity);
    if (rc == NINLIL_OK)
        rc = ninlil_edhoc_hop_material(&n->handshake, &material[1]);
    if (rc != NINLIL_OK) {
        ninlil_secret_clear(material, sizeof(material));
        return rc;
    }
    ninlil_node_disconnect(n, n->handshake_peer);
    for (hop = 0u; hop < 2u && rc == NINLIL_OK; hop++) {
        ninlil_security_io io;
        ninlil_counter_config counter = {0};
        memcpy(counter.session_fingerprint, material[hop].fingerprint, 16u);
        counter.direction = direction;
        counter.reservation_size = 1024u;
        counter.max_counter_exclusive = NINLIL_SECURITY_COUNTER_MAX_EXCLUSIVE;
        rc =
            n->config.counter_io(n->config.counter_ctx,
                                 (uint16_t)(n->handshake_peer * 2u + hop), &io);
        if (rc == NINLIL_OK)
            rc = ninlil_security_format(&io);
        if (rc == NINLIL_OK)
            rc = ninlil_counter_open(&p->counters[hop], &io,
                                     NINLIL_COUNTER_CREATE_NEW, &counter);
        if (rc == NINLIL_OK)
            rc = ninlil_secure_open(
                &p->sessions[hop], &material[hop], &p->counters[hop],
                ninlil_psa_aead(), n->config.local,
                n->members[n->handshake_peer].grant.node, direction);
        if (rc == NINLIL_OK)
            rc = ninlil_secure_bind_membership(
                &p->sessions[hop],
                n->members[n->local_index].grant.membership_epoch,
                n->members[n->handshake_peer].grant.membership_epoch);
    }
    ninlil_secret_clear(material, sizeof(material));
    if (rc != NINLIL_OK) {
        ninlil_node_disconnect(n, n->handshake_peer);
        n->status.fault = rc;
        return rc;
    }
    if (n->config.local == n->config.root && !p->revoked) {
        rc = ninlil_join_begin(&n->authority, identity, n->now_ms);
        if (rc == NINLIL_OK)
            rc = ninlil_join_authenticated(&n->authority, identity,
                                           p->sessions[0].material.fingerprint,
                                           n->now_ms);
    }
    if (rc == NINLIL_OK) {
        n->handshake_installed = 1u;
        if (n->status.handshakes != UINT32_MAX)
            n->status.handshakes++;
    }
    return rc;
}

static int exchange(ninlil_node *n, const uint8_t *input, size_t length)
{
    uint8_t output[NINLIL_EDHOC_MESSAGE_MAX];
    size_t written = 0u;
    int rc = ninlil_edhoc_exchange(&n->handshake, input, length, n->now_ms,
                                   output, sizeof(output), &written);
    if (rc == NINLIL_OK && n->handshake.authenticated &&
        !n->handshake_installed)
        rc = install_session(n);
    if (rc == NINLIL_OK && written) {
        uint8_t kind = n->handshake.config.initiator
                           ? (n->handshake.step == 1u ? 1u : 3u)
                           : (n->handshake.step == 1u ? 2u : 4u);
        rc = ninlil_node_bootstrap_send(
            n, n->members[n->handshake_peer].grant.node, kind,
            n->exchange_token, output, written);
    }
    /* Different, bounded retry periods prevent simultaneously booted radios
     * from repeatedly transmitting over each other's handshake. The public
     * exchange token is already randomly generated; no new crypto is used. */
    n->handshake_retry_at =
        n->now_ms + NODE_RETRY_MS +
        ((n->exchange_token >> ((n->handshake.step % 3u) * 8u)) % 701u) +
        (n->config.local % 31u) * 7u;
    ninlil_secret_clear(output, sizeof(output));
    return rc;
}

static int begin(ninlil_node *n, uint16_t index, uint64_t token, int initiator)
{
    ninlil_edhoc_config config;
    int rc;
    ninlil_edhoc_close(&n->handshake);
    n->handshake_installed = 0u;
    n->handshake_peer = index;
    n->exchange_token = token;
    n->peers[index].retry_at = n->now_ms + NODE_RETRY_MS;
    rc = ninlil_identity_credentials(
        &n->credential, n->config.identity, n->config.local,
        n->members[index].grant.node, n->members[index].public_key,
        n->members[index].grant.identity, initiator, &config);
    if (rc == NINLIL_OK)
        rc = ninlil_edhoc_open(&n->handshake, &config, n->now_ms);
    return rc;
}

static int auth_allowed(ninlil_node *n, uint16_t index)
{
    if (n->peers[n->local_index].revoked)
        return index == n->root_index;
    return !n->peers[index].revoked || (n->config.local == n->config.root &&
                                        !n->peers[index].revocation_applied);
}

int ninlil_node_auth_step(ninlil_node *n)
{
    unsigned int i;
    int forwarded = ninlil_node_forward_step(n);
    if (forwarded != NINLIL_OK)
        return forwarded;
    if (n->handshake.opened) {
        if (n->now_ms < n->handshake_retry_at)
            return NINLIL_OK;
        if (n->handshake_installed && n->handshake.config.initiator) {
            ninlil_edhoc_close(&n->handshake);
            return NINLIL_OK;
        }
        return exchange(n, NULL, 0u);
    }
    for (i = 0u; i < n->config.member_count; i++) {
        uint16_t index = n->next_peer;
        uint8_t random[8];
        uint64_t token;
        int rc;
        n->next_peer = (uint16_t)((index + 1u) % n->config.member_count);
        if (index == n->local_index || !auth_allowed(n, index) ||
            n->peers[index].sessions[0].ready ||
            n->now_ms < n->peers[index].retry_at ||
            (!n->joined && n->config.local != n->config.root &&
             index != n->root_index))
            continue;
        rc =
            n->config.random.fill(n->config.random.ctx, random, sizeof(random));
        if (rc != NINLIL_OK)
            return rc;
        token = ninlil_node_get(random, sizeof(random));
        if (!token)
            return NINLIL_ERR_IO;
        rc = begin(n, index, token, 1);
        return rc == NINLIL_OK ? exchange(n, NULL, 0u) : rc;
    }
    return NINLIL_OK;
}

int ninlil_node_bootstrap(ninlil_node *n, const uint8_t *frame, size_t length)
{
    uint16_t source, target;
    uint64_t token;
    uint8_t kind, hops;
    int index, rc;
    if (!frame || length <= NINLIL_NODE_BOOTSTRAP_HEADER ||
        length > NINLIL_SECURE_FRAME_MAX || memcmp(frame, "NB\001", 3u) != 0)
        return NINLIL_ERR_INVALID;
    kind = (uint8_t)(frame[3] & 31u);
    if (kind == 6u)
        return ninlil_node_discovery_receive(n, frame, length);
    hops = (uint8_t)(frame[3] >> 5);
    source = (uint16_t)ninlil_node_get(frame + 4, 2u);
    target = (uint16_t)ninlil_node_get(frame + 6, 2u);
    token = ninlil_node_get(frame + 8, 8u);
    index = ninlil_node_index(n, source);
    if (index < 0 || source == target || source == n->config.local ||
        ninlil_node_index(n, target) < 0 || !token || !hops ||
        hops > NINLIL_NETWORK_HOPS_MAX || kind < 1u || kind > 5u ||
        !auth_allowed(n, (uint16_t)index))
        return NINLIL_ERR_UNAUTHORIZED;
    if (kind == 5u &&
        (length <= 16u + NINLIL_SECURE_OVERHEAD ||
         memcmp(frame + 16, "NS\001", 3u) != 0 || frame[47] != 1u ||
         ninlil_node_get(frame + 20, 2u) != source ||
         ninlil_node_get(frame + 22, 2u) != target))
        return NINLIL_ERR_INVALID;
    if (target != n->config.local)
        return ninlil_node_forward(n, frame, length);
    if (kind == 5u) {
        uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
        size_t size = 0u;
        rc = ninlil_secure_unseal_control(&n->peers[index].sessions[0],
                                          frame + 16, length - 16u, plain,
                                          sizeof(plain), &size);
        if (rc == NINLIL_OK)
            rc = ninlil_node_control_receive(n, source, plain, size, 0);
        if (rc == NINLIL_OK && n->handshake_installed &&
            n->handshake_peer == (uint16_t)index)
            ninlil_edhoc_close(&n->handshake);
        ninlil_secret_clear(plain, sizeof(plain));
        return rc;
    }
    if (kind == 1u &&
        (!n->handshake.opened || n->handshake_peer != (uint16_t)index ||
         n->exchange_token != token)) {
        if (n->handshake.opened &&
            (!n->handshake.config.initiator ||
             n->handshake_peer != (uint16_t)index || n->config.local < source))
            return NINLIL_ERR_BUSY;
        if (!n->handshake.opened && n->now_ms < n->peers[index].retry_at)
            return NINLIL_ERR_BUSY;
        rc = begin(n, (uint16_t)index, token, 0);
        if (rc != NINLIL_OK)
            return rc;
    }
    if (!n->handshake.opened || n->handshake_peer != (uint16_t)index ||
        n->exchange_token != token ||
        (n->handshake.config.initiator &&
         kind != (n->handshake.step == 1u ? 2u : 4u)) ||
        (!n->handshake.config.initiator &&
         kind != (n->handshake.step == 0u ? 1u : 3u)))
        return NINLIL_ERR_STATE;
    return exchange(n, frame + 16, length - 16u);
}

static int control_current(ninlil_node *n, uint16_t index, const uint8_t *plain,
                           size_t size)
{
    uint64_t lease;
    if (!size)
        return NINLIL_ERR_INVALID;
    if ((n->peers[index].revoked || n->peers[n->local_index].revoked) &&
        plain[0] != NODE_REVOKE_NOTICE && plain[0] != NODE_REVOKE_ACK)
        return NINLIL_ERR_UNAUTHORIZED;
    if (plain[0] == NODE_CLOCK_REQUEST)
        return size == 9u &&
                       ninlil_node_get(plain + 1, 8u) == n->clock.challenge &&
                       n->now_ms - n->clock.request_ms <=
                           NINLIL_LEASE_SYNC_MAX_RTT_MS
                   ? NINLIL_OK
                   : NINLIL_ERR_STATE;
    if (plain[0] == NODE_CLOCK_REPLY) {
        uint64_t stamp = size == 25u ? ninlil_node_get(plain + 9, 8u) : 0u;
        return stamp && ninlil_node_lease(n, &lease) == NINLIL_OK &&
                       lease >= stamp &&
                       lease - stamp <= NINLIL_LEASE_SYNC_MAX_RTT_MS
                   ? NINLIL_OK
                   : NINLIL_ERR_STATE;
    }
    if (plain[0] == NODE_MEMBER)
        return size > 9u && ninlil_node_get(plain + 1, 8u) ==
                                n->membership_generation
                   ? NINLIL_OK
                   : NINLIL_ERR_STATE;
    if ((plain[0] == NODE_JOIN_REQUEST || plain[0] == NODE_JOIN_ACK) &&
        n->joined)
        return NINLIL_ERR_STATE;
    if (plain[0] == NODE_JOIN_ACCEPT || plain[0] == NODE_JOIN_ACTIVE) {
        ninlil_join_peer *p = ninlil_node_authority_peer(n, index);
        ninlil_join_record r;
        return p && ninlil_join_decode(plain + 1, size - 1u, &r) == NINLIL_OK &&
                       r.state == p->record.state &&
                       memcmp(r.transaction, p->record.transaction, 16u) == 0
                   ? NINLIL_OK
                   : NINLIL_ERR_STATE;
    }
    return ninlil_node_plan_frame_current(n, n->members[index].grant.node,
                                          plain, size);
}

int ninlil_node_auth_current(ninlil_node *n, const uint8_t *frame,
                             size_t length)
{
    uint16_t source, target;
    int index;
    if (!frame || length <= 16u || length > NINLIL_SECURE_FRAME_MAX ||
        memcmp(frame, "NB\001", 3u) != 0)
        return NINLIL_ERR_INVALID;
    if ((frame[3] & 31u) == 6u)
        return ninlil_node_discovery_current(n, frame, length);
    source = (uint16_t)ninlil_node_get(frame + 4, 2u);
    target = (uint16_t)ninlil_node_get(frame + 6, 2u);
    if (source != n->config.local)
        return ninlil_node_forward_current(n, frame, length);
    index = ninlil_node_index(n, target);
    if (index < 0)
        return NINLIL_ERR_UNAUTHORIZED;
    if ((frame[3] & 31u) == 5u) {
        uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
        size_t size = 0u;
        int rc =
            ninlil_secure_inspect_tx(&n->peers[index].sessions[0], frame + 16,
                                     length - 16u, plain, sizeof(plain), &size);
        if (rc == NINLIL_OK)
            rc = control_current(n, (uint16_t)index, plain, size);
        ninlil_secret_clear(plain, sizeof(plain));
        return rc;
    }
    return n->handshake.opened && n->handshake_peer == (uint16_t)index &&
                   ninlil_node_get(frame + 8, 8u) == n->exchange_token &&
                   n->now_ms - n->handshake.started_ms <=
                       NINLIL_EDHOC_DEADLINE_MS
               ? NINLIL_OK
               : NINLIL_ERR_STATE;
}
