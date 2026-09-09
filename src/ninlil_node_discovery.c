#include "ninlil_enrollment.h"
#include "ninlil_node_internal.h"
#include <stdlib.h>
#include <string.h>

#define DISCOVERY_CHUNK 220u
#define DISCOVERY_LIFETIME 30000u
typedef struct advert {
    uint8_t bytes[NINLIL_ADMISSION_MAX];
    uint64_t token, until;
    uint16_t source, size, cursor;
    uint8_t hops;
} advert;
struct ninlil_discovery {
    advert own, forward, receive;
    uint64_t own_at, emit_at, verify_at, seen_until;
    uint8_t seen[16];
};
int ninlil_node_discovery_open(ninlil_node *n)
{
    if (n->config.dynamic_enrollment > 1u)
        return NINLIL_ERR_INVALID;
    if (n->config.dynamic_enrollment) {
        n->discovery = calloc(1u, sizeof(*n->discovery));
        if (!n->discovery)
            return NINLIL_ERR_CAPACITY;
    }
    return NINLIL_OK;
}
void ninlil_node_discovery_close(ninlil_node *n)
{
    free(n->discovery);
    n->discovery = NULL;
}
int ninlil_node_advertise(ninlil_node *n, const uint8_t *data, size_t length)
{
    ninlil_node_member m;
    uint8_t expected[NINLIL_MEMBER_RECORD_MAX],
        actual[NINLIL_MEMBER_RECORD_MAX];
    size_t size, other;
    int rc;
    advert *a;
    if (!n || !n->discovery || n->status.fault ||
        (n->config.local == n->config.root && !n->config.authority_key))
        return NINLIL_ERR_STATE;
    rc =
        ninlil_admission_verify(ninlil_node_admission_key(n), data, length, &m);
    if (rc != NINLIL_OK)
        return rc;
    size = ninlil_member_encode(&n->members[n->local_index], expected,
                                sizeof(expected));
    other = ninlil_member_encode(&m, actual, sizeof(actual));
    if (!size || size != other || memcmp(expected, actual, size))
        return NINLIL_ERR_UNAUTHORIZED;
    a = &n->discovery->own;
    memset(a, 0, sizeof(*a));
    memcpy(a->bytes, data, length);
    a->size = (uint16_t)length;
    a->source = n->config.local;
    a->hops = NINLIL_NETWORK_HOPS_MAX;
    n->discovery->own_at = n->now_ms;
    return NINLIL_OK;
}
static int relay(ninlil_node *n)
{
    return !n->peers[n->local_index].revoked &&
           n->members[n->local_index].grant.role != NINLIL_ROLE_BATTERY_LEAF &&
           (n->members[n->local_index].grant.capabilities &
            NINLIL_CAP_RELAY_CUSTODY);
}
static int shape(ninlil_node *n, const uint8_t *f, size_t length, advert *a)
{
    size_t chunk;
    if (!f || length <= 20u || length > 240u || memcmp(f, "NB\001", 3u) ||
        (f[3] & 31u) != 6u || !n->discovery)
        return NINLIL_ERR_INVALID;
    a->hops = (uint8_t)(f[3] >> 5);
    a->source = (uint16_t)ninlil_node_get(f + 4, 2u);
    a->token = ninlil_node_get(f + 8, 8u);
    a->size = (uint16_t)ninlil_node_get(f + 16, 2u);
    a->cursor = (uint16_t)ninlil_node_get(f + 18, 2u);
    if (!a->hops || a->hops > NINLIL_NETWORK_HOPS_MAX || !a->source ||
        a->source == UINT16_MAX || !a->token || a->size < 235u ||
        a->size > NINLIL_ADMISSION_MAX || a->cursor >= a->size ||
        a->cursor % DISCOVERY_CHUNK ||
        ninlil_node_get(f + 6, 2u) != n->config.root)
        return NINLIL_ERR_INVALID;
    chunk = (size_t)a->size - a->cursor;
    if (chunk > DISCOVERY_CHUNK)
        chunk = DISCOVERY_CHUNK;
    return length == chunk + 20u ? NINLIL_OK : NINLIL_ERR_INVALID;
}
int ninlil_node_discovery_receive(ninlil_node *n, const uint8_t *f,
                                  size_t length)
{
    advert incoming = {0}, *r;
    ninlil_node_member m;
    uint8_t digest[32];
    size_t chunk, hashed = 0u;
    int rc = shape(n, f, length, &incoming);
    struct ninlil_discovery *d = n->discovery;
    if (rc != NINLIL_OK || n->peers[n->local_index].revoked)
        return NINLIL_ERR_UNAUTHORIZED;
    if ((incoming.source == n->config.local &&
         incoming.source != n->config.root) ||
        (incoming.source == n->config.root && !n->config.authority_key))
        return NINLIL_ERR_EMPTY;
    r = &d->receive;
    if (r->until <= n->now_ms) {
        if (incoming.cursor || n->now_ms < d->verify_at)
            return NINLIL_ERR_BUSY;
        *r = incoming;
        r->until = n->now_ms + DISCOVERY_LIFETIME;
    }
    if (r->source != incoming.source || r->token != incoming.token ||
        r->size != incoming.size || r->hops != incoming.hops)
        return NINLIL_ERR_BUSY;
    chunk = length - 20u;
    if (incoming.cursor < r->cursor)
        return memcmp(r->bytes + incoming.cursor, f + 20, chunk)
                   ? NINLIL_ERR_CONFLICT
                   : NINLIL_OK;
    if (incoming.cursor != r->cursor)
        return NINLIL_ERR_BUSY;
    memcpy(r->bytes + r->cursor, f + 20, chunk);
    r->cursor = (uint16_t)(r->cursor + chunk);
    if (r->cursor != r->size)
        return NINLIL_OK;
    r->until = 0u;
    if (psa_hash_compute(PSA_ALG_SHA_256, r->bytes, r->size, digest,
                         sizeof(digest), &hashed) != PSA_SUCCESS ||
        hashed != sizeof(digest))
        return NINLIL_ERR_IO;
    if (n->now_ms < d->seen_until && !memcmp(digest, d->seen, sizeof(d->seen)))
        return NINLIL_OK;
    d->verify_at = n->now_ms + 1000u;
    rc = ninlil_admission_verify(ninlil_node_admission_key(n), r->bytes,
                                 r->size, &m);
    if (rc != NINLIL_OK || m.grant.node != r->source)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = ninlil_node_enroll(n, &m);
    if (rc != NINLIL_OK)
        return rc;
    if (n->peers[ninlil_node_index(n, r->source)].revoked)
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(d->seen, digest, sizeof(d->seen));
    d->seen_until = n->now_ms + 10000u;
    if (n->config.local != n->config.root && r->hops > 1u && relay(n) &&
        (d->forward.until <= n->now_ms ||
         d->forward.cursor == d->forward.size)) {
        d->forward = *r;
        d->forward.hops--;
        d->forward.cursor = 0u;
        d->forward.until = n->now_ms + DISCOVERY_LIFETIME;
    }
    return NINLIL_OK;
}
int ninlil_node_discovery_current(ninlil_node *n, const uint8_t *f,
                                  size_t length)
{
    advert incoming = {0}, *a;
    int rc = shape(n, f, length, &incoming);
    if (rc != NINLIL_OK || n->peers[n->local_index].revoked)
        return NINLIL_ERR_UNAUTHORIZED;
    a = incoming.source == n->config.local ? &n->discovery->own
                                           : &n->discovery->forward;
    return a->until > n->now_ms && a->source == incoming.source &&
                   a->size == incoming.size && a->token == incoming.token &&
                   a->hops == incoming.hops &&
                   (a == &n->discovery->own || relay(n)) &&
                   !memcmp(a->bytes + incoming.cursor, f + 20, length - 20u)
               ? NINLIL_OK
               : NINLIL_ERR_STATE;
}
int ninlil_node_discovery_step(ninlil_node *n)
{
    struct ninlil_discovery *d = n->discovery;
    advert *a;
    uint8_t frame[240];
    size_t chunk;
    int rc;
    if (!d || n->peers[n->local_index].revoked || n->now_ms < d->emit_at)
        return NINLIL_OK;
    a = &d->forward;
    if (a->until <= n->now_ms || a->cursor >= a->size) {
        a = &d->own;
        if (!a->size)
            return NINLIL_OK;
        if (n->now_ms >= d->own_at &&
            (a->cursor >= a->size || !a->token || a->until <= n->now_ms)) {
            uint8_t token[8];
            rc = n->config.random.fill(n->config.random.ctx, token,
                                       sizeof(token));
            if (rc != NINLIL_OK)
                return rc;
            a->token = ninlil_node_get(token, sizeof(token));
            if (!a->token)
                return NINLIL_ERR_IO;
            a->cursor = 0u;
            a->until = n->now_ms + DISCOVERY_LIFETIME;
            d->own_at =
                n->now_ms + (n->joined ? 60000u : 12000u) + a->token % 3001u;
        }
        if (a->cursor >= a->size || a->until <= n->now_ms || !a->token)
            return NINLIL_OK;
    }
    memcpy(frame, "NB\001", 3u);
    frame[3] = (uint8_t)(((unsigned int)a->hops << 5) | 6u);
    ninlil_node_put(frame + 4, a->source, 2u);
    ninlil_node_put(frame + 6, n->config.root, 2u);
    ninlil_node_put(frame + 8, a->token, 8u);
    ninlil_node_put(frame + 16, a->size, 2u);
    ninlil_node_put(frame + 18, a->cursor, 2u);
    chunk = (size_t)a->size - a->cursor;
    if (chunk > DISCOVERY_CHUNK)
        chunk = DISCOVERY_CHUNK;
    memcpy(frame + 20, a->bytes + a->cursor, chunk);
    rc = n->config.emit(n->config.emit_ctx, n->config.root,
                        NINLIL_TRAFFIC_NORMAL, frame, chunk + 20u);
    d->emit_at = n->now_ms + 1000u;
    if (rc == NINLIL_OK)
        a->cursor = (uint16_t)(a->cursor + chunk);
    return rc;
}
