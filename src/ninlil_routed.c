#include "ninlil_routed.h"
#include <string.h>

static uint16_t node_at(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static ninlil_secure_session *session(ninlil_routed *r, uint16_t peer, int hop)
{
    ninlil_peer_policy p, local;
    ninlil_secure_session *s;
    if (!r->config.policy || !r->config.session)
        return NULL;
    memset(&p, 0, sizeof(p));
    memset(&local, 0, sizeof(local));
    if (r->config.policy(r->config.policy_ctx, peer, &p) != NINLIL_OK ||
        p.membership_epoch == 0u ||
        p.membership_epoch != p.session_membership_epoch ||
        r->config.policy(r->config.policy_ctx, r->config.local, &local) !=
            NINLIL_OK ||
        !local.membership_epoch ||
        local.membership_epoch != local.session_membership_epoch)
        return NULL;
    s = r->config.session(r->config.session_ctx, peer, hop);
    return s && s->ready && s->local == r->config.local && s->peer == peer &&
                   s->local_membership_epoch == local.membership_epoch &&
                   s->peer_membership_epoch == p.membership_epoch
               ? s
               : NULL;
}

static int emit(ninlil_routed *r, uint16_t next, ninlil_traffic_class traffic,
                const ninlil_relay_record *record)
{
    uint8_t plain[NINLIL_RELAY_FRAME_MAX], cipher[NINLIL_SECURE_FRAME_MAX];
    size_t size = ninlil_relay_encode(record, plain, sizeof(plain)),
           length = 0u;
    ninlil_secure_session *s = session(r, next, 1);
    int rc;
    if (!size || !s)
        return NINLIL_ERR_BUSY;
    rc = ninlil_secure_seal(s, plain, size, cipher, sizeof(cipher), &length);
    if (rc == NINLIL_OK)
        rc = r->config.emit(r->config.emit_ctx, next, traffic, cipher, length);
    ninlil_secret_clear(plain, sizeof(plain));
    ninlil_secret_clear(cipher, sizeof(cipher));
    return rc;
}

static int send_packet(void *ctx, const uint8_t *data, size_t length)
{
    ninlil_routed *r = ctx;
    ninlil_relay_record record;
    ninlil_network_plan plan;
    ninlil_secure_session *s;
    size_t size;
    int rc;
    if (!data || length < 26u || memcmp(data, "NL\002", 3u) != 0 ||
        node_at(data + 4) != r->config.local ||
        !((data[3] == 1u && length >= 40u) || (data[3] == 2u && length == 26u)))
        return NINLIL_ERR_INVALID;
    if (length > NINLIL_RELAY_CIPHERTEXT_MAX - NINLIL_SECURE_OVERHEAD)
        return NINLIL_ERR_TOO_LARGE;
    s = session(r, node_at(data + 6), 0);
    if (!s)
        return NINLIL_ERR_BUSY;
    rc = r->config.route(r->config.route_ctx, r->config.local, s->peer,
                         r->now_ms, &plan);
    if (rc != NINLIL_OK)
        return rc;
    memset(&record, 0, sizeof(record));
    record.traffic =
        data[3] == 2u ? NINLIL_TRAFFIC_CONTROL : (ninlil_traffic_class)data[28];
    record.path = plan.path;
    record.route_epoch = plan.epoch;
    rc = ninlil_secure_seal(s, data, length, record.ciphertext,
                            sizeof(record.ciphertext), &size);
    if (rc != NINLIL_OK)
        return rc;
    record.length = (uint16_t)size;
    rc = r->config.digest(record.ciphertext, size, record.packet_id);
    if (rc != NINLIL_OK)
        return rc;
    return emit(r, record.path.nodes[1],
                data[3] == 2u ? NINLIL_TRAFFIC_CONTROL
                              : (ninlil_traffic_class)data[28],
                &record);
}

static int no_receive(void *ctx, uint8_t *buffer, size_t capacity,
                      size_t *length)
{
    (void)ctx;
    (void)buffer;
    (void)capacity;
    (void)length;
    return 0;
}

int ninlil_routed_open(ninlil_routed *r, const ninlil_routed_config *c,
                       ninlil_link *link)
{
    if (!r || !c || !link || !c->local || c->local == UINT16_MAX ||
        !c->policy || !c->session || !c->route || !c->emit || !c->digest ||
        (c->relay && c->relay->local != c->local))
        return NINLIL_ERR_INVALID;
    memset(r, 0, sizeof(*r));
    r->config = *c;
    link->send = send_packet;
    link->recv = no_receive;
    link->ctx = r;
    link->max_packet_size =
        NINLIL_RELAY_CIPHERTEXT_MAX - NINLIL_SECURE_OVERHEAD;
    return NINLIL_OK;
}

void ninlil_routed_attach(ninlil_routed *r, ninlil_runtime *core)
{
    if (r)
        r->core = core;
}

static int final_commit(ninlil_routed *r, const ninlil_relay_record *record)
{
    ninlil_secure_session *s = session(r, record->path.nodes[0], 0), candidate;
    uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
    size_t length = 0u;
    unsigned int i;
    int rc;
    if (!s || !r->core)
        return NINLIL_ERR_BUSY;
    for (i = 0u; i < NINLIL_ROUTED_ACK_CACHE; i++)
        if (memcmp(r->ack_cache[i], record->packet_id, 16u) == 0)
            return NINLIL_OK;
    /* Commit replay state only after durable Core commit. Capacity/IO cannot
     * make the sole retransmitted ciphertext permanently unusable. */
    candidate = *s;
    rc = ninlil_secure_unseal(&candidate, record->ciphertext, record->length,
                              plain, sizeof(plain), &length);
    if (rc == NINLIL_OK && (length < 26u || node_at(plain + 4) != s->peer ||
                            node_at(plain + 6) != r->config.local))
        rc = NINLIL_ERR_UNAUTHORIZED;
    if (rc == NINLIL_OK)
        rc = ninlil_ingest(r->core, plain, length);
    if (rc == NINLIL_OK) {
        s->rx_high = candidate.rx_high;
        s->rx_bitmap = candidate.rx_bitmap;
        memcpy(r->ack_cache[r->ack_cursor], record->packet_id, 16u);
        r->ack_cursor =
            (uint16_t)((r->ack_cursor + 1u) % NINLIL_ROUTED_ACK_CACHE);
    }
    ninlil_secret_clear(&candidate, sizeof(candidate));
    ninlil_secret_clear(plain, sizeof(plain));
    return rc;
}

static int receive_record(ninlil_routed *r, uint16_t sender,
                          ninlil_relay_record *record)
{
    ninlil_network_plan plan;
    uint8_t digest[16];
    unsigned int at;
    int rc;
    if (record->control)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = r->config.digest(record->ciphertext, record->length, digest);
    if (rc != NINLIL_OK || memcmp(digest, record->packet_id, 16u) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = r->config.route(r->config.route_ctx, record->path.nodes[0],
                         record->path.nodes[record->path.count - 1u], r->now_ms,
                         &plan);
    if (rc != NINLIL_OK || plan.epoch != record->route_epoch ||
        plan.path.count != record->path.count ||
        memcmp(plan.path.nodes, record->path.nodes,
               (size_t)plan.path.count * sizeof(uint16_t)) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    for (at = 0u; at < record->path.count; at++)
        if (record->path.nodes[at] == r->config.local)
            break;
    if (at == record->path.count)
        return NINLIL_ERR_UNAUTHORIZED;
    if (record->done) {
        if (at + 1u >= record->path.count ||
            record->path.nodes[at + 1u] != sender)
            return NINLIL_ERR_UNAUTHORIZED;
        /* A source hop ACK is transport progress, not E2E evidence. */
        return at == 0u ? NINLIL_OK
               : r->config.relay
                   ? ninlil_relay_ack(r->config.relay, sender,
                                      record->packet_id, record->route_epoch)
                   : NINLIL_ERR_STATE;
    }
    if (at == 0u || record->path.nodes[at - 1u] != sender)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = at + 1u == record->path.count ? final_commit(r, record)
         : r->config.relay
             ? ninlil_relay_receive(r->config.relay, sender, record, r->now_ms)
             : NINLIL_ERR_UNAUTHORIZED;
    if (rc != NINLIL_OK)
        return rc;
    record->done = 1u;
    return emit(r, sender, NINLIL_TRAFFIC_CONTROL, record);
}

int ninlil_routed_receive(ninlil_routed *r, const uint8_t *frame, size_t length,
                          uint64_t now)
{
    ninlil_secure_session *s;
    ninlil_relay_record record;
    uint8_t plain[NINLIL_SECURE_PLAINTEXT_MAX];
    size_t size;
    int rc;
    if (!r || !frame || length < NINLIL_SECURE_OVERHEAD ||
        length > NINLIL_SECURE_FRAME_MAX || now < r->now_ms)
        return NINLIL_ERR_INVALID;
    r->now_ms = now;
    s = session(r, node_at(frame + 4), 1);
    if (!s)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = ninlil_secure_unseal(s, frame, length, plain, sizeof(plain), &size);
    if (rc == NINLIL_OK)
        rc = ninlil_relay_decode(plain, size, &record);
    if (rc == NINLIL_OK)
        rc = receive_record(r, s->peer, &record);
    if (rc != NINLIL_OK && r->rejected != UINT32_MAX)
        r->rejected++;
    ninlil_secret_clear(plain, sizeof(plain));
    return rc;
}

int ninlil_routed_poll(ninlil_routed *r, uint64_t now)
{
    ninlil_relay_record record;
    uint16_t next;
    int rc;
    if (!r || now < r->now_ms)
        return NINLIL_ERR_INVALID;
    r->now_ms = now;
    if (!r->config.relay)
        return NINLIL_OK;
    rc = ninlil_relay_next(r->config.relay, now, &next, &record);
    return rc == NINLIL_ERR_EMPTY ? NINLIL_OK
           : rc != NINLIL_OK      ? rc
                                  : emit(r, next, record.traffic, &record);
}

int ninlil_routed_apply_rto(ninlil_routed *r, uint16_t target, uint32_t step_ms)
{
    ninlil_network_plan plan;
    uint32_t steps;
    int rc;
    if (!r || !r->core || step_ms == 0u || step_ms > 30000u)
        return NINLIL_ERR_INVALID;
    rc = r->config.route(r->config.route_ctx, r->config.local, target,
                         r->now_ms, &plan);
    if (rc != NINLIL_OK)
        return rc;
    steps = (plan.rto_ms + step_ms - 1u) / step_ms;
    if (steps > NINLIL_MAX_RETRY_INTERVAL_STEPS)
        return NINLIL_ERR_TOO_LARGE;
    return ninlil_set_retry_interval(r->core, steps);
}

int ninlil_routed_frame_current(ninlil_routed *r, const uint8_t *frame,
                                size_t length)
{
    ninlil_secure_session *s;
    if (!r || !frame || length < NINLIL_SECURE_OVERHEAD ||
        length > NINLIL_SECURE_FRAME_MAX || memcmp(frame, "NS\001", 3u) != 0 ||
        node_at(frame + 4) != r->config.local)
        return NINLIL_ERR_UNAUTHORIZED;
    s = session(r, node_at(frame + 6), 1);
    return s && memcmp(s->material.fingerprint, frame + 8, 16u) == 0
               ? NINLIL_OK
               : NINLIL_ERR_UNAUTHORIZED;
}
