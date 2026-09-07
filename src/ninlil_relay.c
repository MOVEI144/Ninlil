#include "ninlil_relay.h"

#include <string.h>

static int position(const ninlil_network_path *p, uint16_t node)
{
    unsigned int i;
    for (i = 0u; i < p->count; i++)
        if (p->nodes[i] == node)
            return (int)i;
    return -1;
}

static int valid(const ninlil_relay_record *p)
{
    uint8_t nonzero = 0u;
    unsigned int i;
    if (p && p->control == 1u) {
        for (i = 0u; i < 16u; i++)
            if (p->packet_id[i])
                return 0;
        for (i = 0u; i < NINLIL_NETWORK_PATH_MAX; i++)
            if (p->path.nodes[i])
                return 0;
        return p->route_epoch != 0u && p->length == 0u && p->path.count == 0u &&
               p->done <= 1u && p->traffic == NINLIL_TRAFFIC_CRITICAL;
    }
    if (!p || p->control != 0u || !ninlil_network_path_valid(&p->path) ||
        p->route_epoch == 0u || p->length == 0u ||
        p->length > NINLIL_RELAY_CIPHERTEXT_MAX || p->done > 1u ||
        p->traffic < NINLIL_TRAFFIC_CRITICAL ||
        p->traffic > NINLIL_TRAFFIC_BULK)
        return 0;
    for (i = 0u; i < 16u; i++)
        nonzero |= p->packet_id[i];
    return nonzero != 0u;
}

static int equal(const ninlil_relay_record *a, const ninlil_relay_record *b)
{
    return a->traffic == b->traffic && a->route_epoch == b->route_epoch &&
           a->path.count == b->path.count &&
           memcmp(a->path.nodes, b->path.nodes,
                  (size_t)a->path.count * sizeof(uint16_t)) == 0 &&
           a->length == b->length &&
           memcmp(a->ciphertext, b->ciphertext, a->length) == 0;
}

static ninlil_relay_slot *find(ninlil_relay *r, const uint8_t id[16])
{
    uint16_t i;
    for (i = 0u; i < r->capacity; i++)
        if (r->slots[i].used &&
            memcmp(r->slots[i].record.packet_id, id, 16u) == 0)
            return &r->slots[i];
    return NULL;
}

static int authorized(ninlil_relay *r, uint16_t peer)
{
    ninlil_peer_policy p;
    memset(&p, 0, sizeof(p));
    return r->policy(r->policy_ctx, peer, &p) == NINLIL_OK &&
           p.membership_epoch != 0u &&
           p.membership_epoch == p.session_membership_epoch;
}

int ninlil_relay_open(ninlil_relay *r, ninlil_relay_slot *slots,
                      uint16_t capacity, uint16_t local, ninlil_role role,
                      uint32_t capabilities, uint32_t retry_ms,
                      ninlil_policy_lookup policy, void *policy_ctx,
                      ninlil_relay_commit_fn writer,
                      ninlil_relay_verify_fn verify, void *commit_ctx,
                      ninlil_relay_route_check route_check, void *route_ctx)
{
    if (!r || !slots || !policy || !writer || !verify || !route_check ||
        local == 0u || local == UINT16_MAX || capacity == 0u ||
        capacity > NINLIL_RELAY_PACKETS_MAX || retry_ms < 100u ||
        retry_ms > 30000u || role == NINLIL_ROLE_BATTERY_LEAF ||
        role < NINLIL_ROLE_POWERED_ENDPOINT ||
        role > NINLIL_ROLE_SITE_GATEWAY ||
        (capabilities & NINLIL_CAP_RELAY_CUSTODY) == 0u ||
        (capabilities & ~NINLIL_CAP_KNOWN_MASK) != 0u)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(r, 0, sizeof(*r));
    memset(slots, 0, sizeof(*slots) * capacity);
    r->slots = slots;
    r->capacity = capacity;
    r->local = local;
    r->retry_ms = retry_ms;
    r->policy = policy;
    r->policy_ctx = policy_ctx;
    r->commit = writer;
    r->commit_ctx = commit_ctx;
    r->verify = verify;
    r->route_check = route_check;
    r->route_ctx = route_ctx;
    return NINLIL_OK;
}

static int verify_owned(ninlil_relay *r, const ninlil_relay_record *record)
{
    int rc = r->verify(r->commit_ctx, record);
    if (rc != NINLIL_OK)
        r->poisoned = 1u;
    return rc;
}

int ninlil_relay_restore(ninlil_relay *r, const ninlil_relay_record *p)
{
    ninlil_relay_slot *s;
    uint16_t i;
    int at;
    if (!r || r->poisoned || !valid(p))
        return NINLIL_ERR_CORRUPT;
    if (p->control) {
        if (p->route_epoch <= r->drain_epoch)
            return NINLIL_ERR_CORRUPT;
        r->drain_epoch = p->route_epoch;
        r->draining = p->done;
        return NINLIL_OK;
    }
    at = position(&p->path, r->local);
    if (at <= 0 || (unsigned int)at + 1u >= p->path.count)
        return NINLIL_ERR_CORRUPT;
    s = find(r, p->packet_id);
    if (s && !equal(&s->record, p)) {
        const ninlil_relay_record *old = &s->record;
        if (p->done || p->route_epoch <= old->route_epoch ||
            p->length != old->length ||
            memcmp(p->ciphertext, old->ciphertext, p->length) != 0 ||
            p->path.nodes[0] != old->path.nodes[0] ||
            p->path.nodes[p->path.count - 1u] !=
                old->path.nodes[old->path.count - 1u])
            return NINLIL_ERR_CORRUPT;
    }
    if (p->done) {
        if (!s)
            return NINLIL_ERR_CORRUPT;
        memset(s, 0, sizeof(*s));
        return NINLIL_OK;
    }
    if (!s)
        for (i = 0u; i < r->capacity; i++)
            if (!r->slots[i].used) {
                s = &r->slots[i];
                break;
            }
    if (!s)
        return NINLIL_ERR_CAPACITY;
    s->used = 1u;
    s->record = *p;
    s->next_attempt_ms = 0u;
    s->attempts = 0u;
    return NINLIL_OK;
}

int ninlil_relay_receive(ninlil_relay *r, uint16_t sender,
                         const ninlil_relay_record *p, uint64_t now)
{
    ninlil_relay_slot *s;
    uint16_t i;
    int at, rc;
    if (!r || r->poisoned || !valid(p) || p->done || p->control)
        return NINLIL_ERR_INVALID;
    at = position(&p->path, r->local);
    if (at <= 0 || (unsigned int)at + 1u >= p->path.count ||
        p->path.nodes[(unsigned int)at - 1u] != sender ||
        !authorized(r, sender) ||
        !authorized(r, p->path.nodes[(unsigned int)at + 1u]) ||
        r->route_check(r->route_ctx, &p->path, p->route_epoch, now) !=
            NINLIL_OK)
        return NINLIL_ERR_UNAUTHORIZED;
    s = find(r, p->packet_id);
    if (s)
        return equal(&s->record, p) ? verify_owned(r, &s->record)
                                    : NINLIL_ERR_CONFLICT;
    if (r->draining)
        return NINLIL_ERR_BUSY;
    for (i = 0u; i < r->capacity; i++)
        if (!r->slots[i].used) {
            s = &r->slots[i];
            break;
        }
    if (!s)
        return NINLIL_ERR_CAPACITY;
    rc = r->commit(r->commit_ctx, p);
    if (rc != NINLIL_OK) {
        r->poisoned = 1u;
        return rc;
    }
    rc = verify_owned(r, p);
    if (rc != NINLIL_OK)
        return rc;
    s->record = *p;
    s->used = 1u;
    s->next_attempt_ms = now;
    // Only this committed OK authorizes the transport to emit hop custody ACK.
    return NINLIL_OK;
}

int ninlil_relay_next(ninlil_relay *r, uint64_t now, uint16_t *next,
                      ninlil_relay_record *out)
{
    uint16_t work;
    if (!r || r->poisoned || !out || !next)
        return NINLIL_ERR_STATE;
    for (work = 0u; work < r->capacity; work++) {
        ninlil_relay_slot *s = &r->slots[r->cursor];
        int at;
        r->cursor = (uint16_t)((r->cursor + 1u) % r->capacity);
        if (!s->used || now < s->next_attempt_ms)
            continue;
        at = position(&s->record.path, r->local);
        if (at < 0 || (unsigned int)at + 1u >= s->record.path.count)
            return NINLIL_ERR_CORRUPT;
        if (!authorized(r, s->record.path.nodes[(unsigned int)at + 1u]) ||
            r->route_check(r->route_ctx, &s->record.path, s->record.route_epoch,
                           now) != NINLIL_OK)
            continue;
        if (now > UINT64_MAX - r->retry_ms)
            return NINLIL_ERR_STATE;
        {
            int rc = verify_owned(r, &s->record);
            if (rc != NINLIL_OK)
                return rc;
        }
        s->next_attempt_ms = now + r->retry_ms;
        if (s->attempts != UINT32_MAX)
            s->attempts++;
        *next = s->record.path.nodes[(unsigned int)at + 1u];
        *out = s->record;
        return NINLIL_OK;
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_relay_ack(ninlil_relay *r, uint16_t sender, const uint8_t id[16],
                     uint64_t epoch)
{
    ninlil_relay_slot *s;
    ninlil_relay_record done;
    int at, rc;
    if (!r || r->poisoned || !id)
        return NINLIL_ERR_STATE;
    s = find(r, id);
    if (!s)
        return NINLIL_ERR_NOT_FOUND;
    at = position(&s->record.path, r->local);
    if (s->attempts == 0u || s->record.route_epoch != epoch || at < 0 ||
        (unsigned int)at + 1u >= s->record.path.count ||
        sender != s->record.path.nodes[(unsigned int)at + 1u] ||
        !authorized(r, sender))
        return NINLIL_ERR_UNAUTHORIZED;
    rc = verify_owned(r, &s->record);
    if (rc != NINLIL_OK)
        return rc;
    done = s->record;
    done.done = 1u;
    rc = r->commit(r->commit_ctx, &done);
    if (rc != NINLIL_OK) {
        r->poisoned = 1u;
        return rc;
    }
    memset(s, 0, sizeof(*s));
    return NINLIL_OK;
}

int ninlil_relay_drain(ninlil_relay *r, int draining)
{
    ninlil_relay_record state;
    int rc;
    if (!r || r->poisoned || r->drain_epoch == UINT64_MAX)
        return NINLIL_ERR_STATE;
    if (r->draining == (draining ? 1u : 0u))
        return NINLIL_OK;
    memset(&state, 0, sizeof(state));
    state.control = 1u;
    state.done = draining ? 1u : 0u;
    state.route_epoch = r->drain_epoch + 1u;
    rc = r->commit(r->commit_ctx, &state);
    if (rc != NINLIL_OK) {
        r->poisoned = 1u;
        return rc;
    }
    r->draining = state.done;
    r->drain_epoch = state.route_epoch;
    return NINLIL_OK;
}

int ninlil_relay_repair(ninlil_relay *r, const uint8_t id[16],
                        const ninlil_network_path *path, uint64_t epoch,
                        uint64_t now)
{
    ninlil_relay_slot *s;
    ninlil_relay_record next;
    int at, rc;
    if (!r || r->poisoned || !id || !ninlil_network_path_valid(path))
        return NINLIL_ERR_INVALID;
    s = find(r, id);
    if (!s)
        return NINLIL_ERR_NOT_FOUND;
    at = position(path, r->local);
    if (epoch <= s->record.route_epoch || at <= 0 ||
        (unsigned int)at + 1u >= path->count ||
        path->nodes[0] != s->record.path.nodes[0] ||
        path->nodes[path->count - 1u] !=
            s->record.path.nodes[s->record.path.count - 1u] ||
        !authorized(r, path->nodes[(unsigned int)at + 1u]) ||
        r->route_check(r->route_ctx, path, epoch, now) != NINLIL_OK)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = verify_owned(r, &s->record);
    if (rc != NINLIL_OK)
        return rc;
    next = s->record;
    next.path = *path;
    next.route_epoch = epoch;
    rc = r->commit(r->commit_ctx, &next);
    if (rc != NINLIL_OK) {
        r->poisoned = 1u;
        return rc;
    }
    s->record = next;
    s->next_attempt_ms = now;
    s->attempts = 0u;
    return NINLIL_OK;
}
int ninlil_relay_ready_remove(const ninlil_relay *r)
{
    uint16_t i;
    if (!r || r->poisoned || !r->draining)
        return 0;
    for (i = 0u; i < r->capacity; i++)
        if (r->slots[i].used)
            return 0;
    return 1;
}

size_t ninlil_relay_encode(const ninlil_relay_record *r, uint8_t *out,
                           size_t capacity)
{
    size_t i, size;
    uint8_t bytes[NINLIL_RELAY_FRAME_MAX];
    if (!valid(r) || !out)
        return 0u;
    size = NINLIL_RELAY_HEADER + r->length;
    if (capacity < size)
        return 0u;
    memset(bytes, 0, NINLIL_RELAY_HEADER);
    memcpy(bytes, "NR\001", 3u);
    bytes[3] = (uint8_t)(r->done | (r->control ? 0x80u : 0u));
    bytes[4] = r->path.count;
    bytes[5] = (uint8_t)r->traffic;
    bytes[6] = (uint8_t)(r->length >> 8);
    bytes[7] = (uint8_t)r->length;
    for (i = 0u; i < r->path.count; i++) {
        bytes[8u + i * 2u] = (uint8_t)(r->path.nodes[i] >> 8);
        bytes[9u + i * 2u] = (uint8_t)r->path.nodes[i];
    }
    for (i = 0u; i < 8u; i++)
        bytes[25u - i] = (uint8_t)(r->route_epoch >> (8u * i));
    memcpy(bytes + 26, r->packet_id, 16u);
    memcpy(bytes + NINLIL_RELAY_HEADER, r->ciphertext, r->length);
    memcpy(out, bytes, size);
    return size;
}

int ninlil_relay_decode(const uint8_t *p, size_t size, ninlil_relay_record *out)
{
    ninlil_relay_record r;
    size_t i;
    if (!p || !out || size < NINLIL_RELAY_HEADER ||
        size > NINLIL_RELAY_FRAME_MAX || memcmp(p, "NR\001", 3u) != 0 ||
        p[5] > NINLIL_TRAFFIC_BULK || p[4] > NINLIL_NETWORK_PATH_MAX)
        return NINLIL_ERR_INVALID;
    memset(&r, 0, sizeof(r));
    r.done = (uint8_t)(p[3] & 0x7Fu);
    r.control = (p[3] & 0x80u) != 0u ? 1u : 0u;
    r.path.count = p[4];
    r.traffic = (ninlil_traffic_class)p[5];
    r.length = (uint16_t)(((uint16_t)p[6] << 8) | p[7]);
    if (size != NINLIL_RELAY_HEADER + r.length)
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < NINLIL_NETWORK_PATH_MAX; i++) {
        uint16_t n =
            (uint16_t)(((uint16_t)p[8u + i * 2u] << 8) | p[9u + i * 2u]);
        if (i < r.path.count)
            r.path.nodes[i] = n;
        else if (n != 0u)
            return NINLIL_ERR_INVALID;
    }
    for (i = 18u; i < 26u; i++)
        r.route_epoch = (r.route_epoch << 8) | p[i];
    for (i = 42u; i < NINLIL_RELAY_HEADER; i++)
        if (p[i] != 0u)
            return NINLIL_ERR_INVALID;
    memcpy(r.packet_id, p + 26, 16u);
    memcpy(r.ciphertext, p + NINLIL_RELAY_HEADER, r.length);
    if (!valid(&r))
        return NINLIL_ERR_INVALID;
    *out = r;
    return NINLIL_OK;
}

int ninlil_relay_return_to_source(ninlil_relay *r, uint16_t source,
                                  uint16_t target, const uint8_t old[16],
                                  const uint8_t fresh[16])
{
    uint8_t nonzero = 0u;
    uint16_t i;
    if (!r || r->poisoned || !old || !fresh || !source || !target ||
        source == target || !authorized(r, source) ||
        memcmp(old, fresh, 16u) == 0)
        return NINLIL_ERR_UNAUTHORIZED;
    for (i = 0u; i < 16u; i++)
        nonzero |= fresh[i];
    if (!nonzero)
        return NINLIL_ERR_UNAUTHORIZED;
    for (i = 0u; i < r->capacity; i++) {
        ninlil_relay_slot *slot = &r->slots[i];
        ninlil_relay_record done;
        int rc;
        if (!slot->used || slot->record.path.nodes[0] != source ||
            slot->record.path.nodes[slot->record.path.count - 1u] != target ||
            slot->record.length < 40u ||
            memcmp(slot->record.ciphertext, "NS\001", 3u) != 0 ||
            memcmp(slot->record.ciphertext + 8, old, 16u) != 0)
            continue;
        rc = verify_owned(r, &slot->record);
        if (rc != NINLIL_OK)
            return rc;
        done = slot->record;
        done.done = 1u;
        rc = r->commit(r->commit_ctx, &done);
        if (rc != NINLIL_OK) {
            r->poisoned = 1u;
            return rc;
        }
        memset(slot, 0, sizeof(*slot));
        return NINLIL_OK;
    }
    return NINLIL_ERR_EMPTY;
}
