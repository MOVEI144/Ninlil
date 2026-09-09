#include "ninlil_secure_link.h"

#include <string.h>

static uint16_t node_at(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static ninlil_secure_session *lookup(ninlil_secure_mux *m, uint16_t node)
{
    ninlil_peer_policy policy, local;
    uint16_t i;
    memset(&policy, 0, sizeof(policy));
    memset(&local, 0, sizeof(local));
    if (m->policy(m->policy_ctx, node, &policy) != NINLIL_OK ||
        policy.membership_epoch == 0u ||
        policy.membership_epoch != policy.session_membership_epoch ||
        m->policy(m->policy_ctx, m->local, &local) != NINLIL_OK ||
        !local.membership_epoch ||
        local.membership_epoch != local.session_membership_epoch)
        return NULL;
    for (i = 0u; i < m->capacity; i++)
        if (m->peers[i].node == node && m->peers[i].session &&
            m->peers[i].session->ready &&
            m->peers[i].session->local_membership_epoch ==
                local.membership_epoch &&
            m->peers[i].session->peer_membership_epoch ==
                policy.membership_epoch)
            return m->peers[i].session;
    return NULL;
}
static int core_frame(const uint8_t *p, size_t size)
{
    return size >= 26u && memcmp(p, "NL\002", 3u) == 0 &&
           ((p[3] == 1u && size >= 40u) || (p[3] == 2u && size == 26u));
}

static int send_packet(void *ctx, const uint8_t *data, size_t length)
{
    ninlil_secure_mux *m = ctx;
    ninlil_secure_session *session;
    uint8_t frame[NINLIL_SECURE_FRAME_MAX];
    size_t written;
    int rc;
    if (!data || !core_frame(data, length) || node_at(data + 4) != m->local)
        return NINLIL_ERR_INVALID;
    if (length > NINLIL_SECURE_PLAINTEXT_MAX ||
        length + NINLIL_SECURE_OVERHEAD > m->bearer.max_packet_size)
        return NINLIL_ERR_TOO_LARGE;
    session = lookup(m, node_at(data + 6));
    if (!session)
        return NINLIL_ERR_BUSY;
    rc = ninlil_secure_seal(session, data, length, frame, sizeof(frame),
                            &written);
    if (rc == NINLIL_OK)
        rc = m->bearer.send(m->bearer.ctx, frame, written);
    ninlil_secret_clear(frame, sizeof(frame));
    return rc;
}

static int receive_packet(void *ctx, uint8_t *output, size_t capacity,
                          size_t *written)
{
    ninlil_secure_mux *m = ctx;
    uint8_t frame[NINLIL_SECURE_FRAME_MAX], plain[NINLIL_SECURE_PLAINTEXT_MAX];
    unsigned int work;
    int result = 0;
    if (!output || !written)
        return NINLIL_ERR_INVALID;
    for (work = 0u; work < 8u; work++) {
        ninlil_secure_session *session;
        size_t length = 0u, decrypted = 0u;
        int rc = m->bearer.recv(m->bearer.ctx, frame, sizeof(frame), &length);
        if (rc != 1) {
            result = rc;
            break;
        }
        if (length < NINLIL_SECURE_OVERHEAD || length > sizeof(frame))
            goto reject;
        session = lookup(m, node_at(frame + 4));
        if (!session)
            goto reject;
        rc = ninlil_secure_unseal(
            session, frame, length, plain,
            capacity < sizeof(plain) ? capacity : sizeof(plain), &decrypted);
        if (rc != NINLIL_OK || !core_frame(plain, decrypted) ||
            node_at(plain + 4) != session->peer ||
            node_at(plain + 6) != m->local)
            goto reject;
        memcpy(output, plain, decrypted);
        *written = decrypted;
        result = 1;
        break;
    reject:
        if (m->rejected != UINT32_MAX)
            m->rejected++;
    }
    ninlil_secret_clear(frame, sizeof(frame));
    ninlil_secret_clear(plain, sizeof(plain));
    return result;
}

int ninlil_secure_mux_open(ninlil_secure_mux *m, ninlil_link bearer,
                           ninlil_secure_peer *peers, uint16_t capacity,
                           uint16_t local, ninlil_policy_lookup policy,
                           void *policy_ctx, ninlil_link *link)
{
    if (!m || !peers || !link || !policy || !bearer.send || !bearer.recv ||
        capacity == 0u || capacity > NINLIL_SECURE_PEERS_MAX || local == 0u ||
        local == UINT16_MAX || bearer.max_packet_size < 80u)
        return NINLIL_ERR_INVALID;
    memset(m, 0, sizeof(*m));
    memset(peers, 0, sizeof(*peers) * capacity);
    m->bearer = bearer;
    m->peers = peers;
    m->capacity = capacity;
    m->local = local;
    m->policy = policy;
    m->policy_ctx = policy_ctx;
    link->send = send_packet;
    link->recv = receive_packet;
    link->ctx = m;
    link->max_packet_size = bearer.max_packet_size - NINLIL_SECURE_OVERHEAD;
    if (link->max_packet_size > NINLIL_SECURE_PLAINTEXT_MAX)
        link->max_packet_size = NINLIL_SECURE_PLAINTEXT_MAX;
    return NINLIL_OK;
}

int ninlil_secure_mux_add(ninlil_secure_mux *m, uint16_t node,
                          ninlil_secure_session *s)
{
    uint16_t i;
    ninlil_secure_peer *empty = NULL;
    if (!m || !m->peers || !s || !s->ready || s->peer != node ||
        s->local != m->local)
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < m->capacity; i++) {
        if (m->peers[i].node == node)
            return NINLIL_ERR_CONFLICT;
        if (m->peers[i].node == 0u)
            empty = &m->peers[i];
    }
    if (!empty)
        return NINLIL_ERR_CAPACITY;
    {
        ninlil_peer_policy local = {0}, remote = {0};
        if (m->policy(m->policy_ctx, m->local, &local) != NINLIL_OK ||
            m->policy(m->policy_ctx, node, &remote) != NINLIL_OK ||
            !local.membership_epoch || !remote.membership_epoch ||
            local.membership_epoch != local.session_membership_epoch ||
            remote.membership_epoch != remote.session_membership_epoch ||
            ninlil_secure_bind_membership(s, local.membership_epoch,
                                          remote.membership_epoch) != NINLIL_OK)
            return NINLIL_ERR_UNAUTHORIZED;
    }
    empty->node = node;
    empty->session = s;
    return NINLIL_OK;
}

void ninlil_secure_mux_remove(ninlil_secure_mux *m, uint16_t node)
{
    uint16_t i;
    if (!m || !m->peers)
        return;
    for (i = 0u; i < m->capacity; i++)
        if (m->peers[i].node == node) {
            ninlil_secure_close(m->peers[i].session);
            memset(&m->peers[i], 0, sizeof(m->peers[i]));
        }
}
