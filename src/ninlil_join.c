#include "ninlil_join.h"

#include <string.h>

static int nonzero(const uint8_t *bytes, size_t size)
{
    uint8_t value = 0u;
    size_t i;
    for (i = 0u; i < size; i++)
        value |= bytes[i];
    return value != 0u;
}

int ninlil_join_grant_valid(const ninlil_join_grant *g)
{
    unsigned int i, j;
    if (!g || !nonzero(g->identity, 32u) || !nonzero(g->authority, 16u) ||
        g->node == 0u || g->node == UINT16_MAX || g->membership_epoch == 0u ||
        g->binding_epoch == 0u ||
        (g->capabilities & ~NINLIL_CAP_KNOWN_MASK) != 0u ||
        g->role < NINLIL_ROLE_BATTERY_LEAF ||
        g->role > NINLIL_ROLE_SITE_GATEWAY ||
        g->service_count > NINLIL_JOIN_SERVICES_MAX ||
        (g->role == NINLIL_ROLE_BATTERY_LEAF &&
         (g->capabilities & NINLIL_CAP_RELAY_CUSTODY) != 0u))
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < g->service_count; i++) {
        const ninlil_service_grant *s = &g->services[i];
        if (s->service_id < NINLIL_APPLICATION_SERVICE_MIN ||
            s->maximum_payload_bytes > NINLIL_MAX_PAYLOAD ||
            s->maximum_live_messages == 0u || s->directions == 0u ||
            (s->directions & ~NINLIL_SERVICE_BOTH) != 0u ||
            s->traffic_class_mask == 0u ||
            (s->traffic_class_mask & 0xF0u) != 0u)
            return NINLIL_ERR_INVALID;
        for (j = 0u; j < i; j++)
            if (g->services[j].service_id == s->service_id)
                return NINLIL_ERR_INVALID;
    }
    return NINLIL_OK;
}

static int equal_grant(const ninlil_join_grant *a, const ninlil_join_grant *b)
{
    unsigned int i;
    if (memcmp(a->identity, b->identity, 32u) != 0 ||
        memcmp(a->authority, b->authority, 16u) != 0 || a->node != b->node ||
        a->membership_epoch != b->membership_epoch ||
        a->binding_epoch != b->binding_epoch ||
        a->capabilities != b->capabilities || a->role != b->role ||
        a->service_count != b->service_count)
        return 0;
    for (i = 0u; i < a->service_count; i++) {
        const ninlil_service_grant *x = &a->services[i], *y = &b->services[i];
        if (x->service_id != y->service_id ||
            x->maximum_payload_bytes != y->maximum_payload_bytes ||
            x->maximum_live_messages != y->maximum_live_messages ||
            x->directions != y->directions ||
            x->traffic_class_mask != y->traffic_class_mask)
            return 0;
    }
    return 1;
}

static ninlil_join_peer *find(ninlil_join_authority *a, const uint8_t id[32])
{
    uint16_t i;
    for (i = 0u; i < a->capacity; i++)
        if (a->peers[i].used && memcmp(a->peers[i].identity, id, 32u) == 0)
            return &a->peers[i];
    return NULL;
}

static int usable(ninlil_join_authority *a)
{
    return a && a->peers && a->commit && !a->poisoned;
}

static int fresh(ninlil_join_peer *p, uint64_t now)
{
    return p && p->phase >= 2u && now >= p->began_ms &&
           now - p->began_ms <= NINLIL_JOIN_TIMEOUT_MS;
}

static int node_free(ninlil_join_authority *a, const ninlil_join_grant *g)
{
    uint16_t i;
    for (i = 0u; i < a->capacity; i++) {
        ninlil_join_peer *p = &a->peers[i];
        if (p->persisted && p->record.grant.node == g->node &&
            memcmp(p->identity, g->identity, 32u) != 0)
            return 0;
    }
    // Revoked addresses remain reserved until an explicit retirement protocol.
    return 1;
}

static int commit(ninlil_join_authority *a, ninlil_join_peer *p,
                  const ninlil_join_record *record)
{
    int rc = a->commit(a->commit_ctx, record);
    if (rc != NINLIL_OK) {
        a->poisoned = 1u;
        return rc;
    }
    p->record = *record;
    p->persisted = 1u;
    return NINLIL_OK;
}

int ninlil_join_open(ninlil_join_authority *a, ninlil_join_peer *peers,
                     uint16_t capacity, const uint8_t authority[16],
                     ninlil_join_commit_fn writer, void *commit_ctx,
                     ninlil_join_approve_fn approve, void *approve_ctx)
{
    if (!a || !peers || !authority || !nonzero(authority, 16u) || !writer ||
        !approve || capacity == 0u || capacity > NINLIL_JOIN_PEERS_MAX)
        return NINLIL_ERR_INVALID;
    memset(a, 0, sizeof(*a));
    memset(peers, 0, sizeof(*peers) * capacity);
    a->peers = peers;
    a->capacity = capacity;
    a->commit = writer;
    a->commit_ctx = commit_ctx;
    a->approve = approve;
    a->approve_ctx = approve_ctx;
    memcpy(a->authority, authority, 16u);
    return NINLIL_OK;
}

static int replacement(const ninlil_join_record *old,
                       const ninlil_join_record *next)
{
    if (memcmp(old->grant.identity, next->grant.identity, 32u) != 0 ||
        memcmp(old->grant.authority, next->grant.authority, 16u) != 0 ||
        next->grant.binding_epoch < old->grant.binding_epoch ||
        next->grant.membership_epoch < old->grant.membership_epoch)
        return 0;
    if (next->grant.membership_epoch > old->grant.membership_epoch)
        return next->state == NINLIL_JOIN_PENDING;
    if (!equal_grant(&old->grant, &next->grant))
        return 0;
    if (old->state == NINLIL_JOIN_REVOKED)
        return next->state == NINLIL_JOIN_REVOKED;
    return 1;
}

int ninlil_join_restore(ninlil_join_authority *a, const ninlil_join_record *r)
{
    ninlil_join_peer *p;
    uint16_t i;
    if (!usable(a) || !r || ninlil_join_grant_valid(&r->grant) != NINLIL_OK ||
        memcmp(a->authority, r->grant.authority, 16u) != 0 ||
        r->state < NINLIL_JOIN_PENDING || r->state > NINLIL_JOIN_REVOKED ||
        !nonzero(r->transaction, 16u) || !node_free(a, &r->grant))
        return NINLIL_ERR_CORRUPT;
    p = find(a, r->grant.identity);
    if (p && p->persisted && !replacement(&p->record, r))
        return NINLIL_ERR_CORRUPT;
    if (!p)
        for (i = 0u; i < a->capacity; i++)
            if (!a->peers[i].used) {
                p = &a->peers[i];
                break;
            }
    if (!p)
        return NINLIL_ERR_CAPACITY;
    memset(p, 0, sizeof(*p));
    p->used = 1u;
    p->persisted = 1u;
    p->record = *r;
    memcpy(p->identity, r->grant.identity, 32u);
    return NINLIL_OK;
}

int ninlil_join_begin(ninlil_join_authority *a, const uint8_t id[32],
                      uint64_t now)
{
    ninlil_join_peer *p;
    uint16_t i, provisional = 0u;
    if (!usable(a) || !id || !nonzero(id, 32u))
        return NINLIL_ERR_INVALID;
    p = find(a, id);
    if (p && (p->session_ready || p->phase != 0u))
        return NINLIL_ERR_BUSY;
    for (i = 0u; i < a->capacity; i++)
        if (a->peers[i].phase != 0u)
            provisional++;
    if (provisional >= NINLIL_JOIN_PROVISIONAL_MAX)
        return NINLIL_ERR_CAPACITY;
    if (!p)
        for (i = 0u; i < a->capacity; i++)
            if (!a->peers[i].used) {
                p = &a->peers[i];
                break;
            }
    if (!p)
        return NINLIL_ERR_CAPACITY;
    p->used = 1u;
    p->phase = 1u;
    p->began_ms = now;
    memcpy(p->identity, id, 32u);
    return NINLIL_OK;
}

int ninlil_join_authenticated(ninlil_join_authority *a, const uint8_t id[32],
                              const uint8_t session[16], uint64_t now)
{
    ninlil_join_peer *p;
    if (!usable(a) || !id || !session || !nonzero(session, 16u))
        return NINLIL_ERR_INVALID;
    p = find(a, id);
    if (!p || p->phase != 1u || now < p->began_ms ||
        now - p->began_ms > NINLIL_JOIN_TIMEOUT_MS)
        return NINLIL_ERR_STATE;
    if (p->persisted && memcmp(p->record.transaction, session, 16u) == 0)
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(p->session, session, 16u);
    p->phase = 2u;
    return NINLIL_OK;
}

int ninlil_join_prepare(ninlil_join_authority *a, const uint8_t id[32],
                        uint64_t now, ninlil_join_record *accept)
{
    ninlil_join_peer *p;
    ninlil_join_record next;
    int rc;
    if (!usable(a) || !id || !accept)
        return NINLIL_ERR_INVALID;
    p = find(a, id);
    if (!fresh(p, now))
        return NINLIL_ERR_STATE;
    if (p->phase == 3u) {
        *accept = p->record;
        return NINLIL_OK;
    }
    memset(&next, 0, sizeof(next));
    rc = a->approve(a->approve_ctx, id, &next.grant);
    if (rc != NINLIL_OK)
        return rc;
    next.state = NINLIL_JOIN_PENDING;
    memcpy(next.transaction, p->session, 16u);
    if (ninlil_join_grant_valid(&next.grant) != NINLIL_OK ||
        memcmp(next.grant.identity, id, 32u) != 0 ||
        memcmp(next.grant.authority, a->authority, 16u) != 0 ||
        !node_free(a, &next.grant) ||
        (p->persisted && !replacement(&p->record, &next)))
        return NINLIL_ERR_UNAUTHORIZED;
    rc = commit(a, p, &next);
    if (rc == NINLIL_OK) {
        p->phase = 3u;
        *accept = next;
    }
    return rc;
}

int ninlil_join_endpoint_commit(const ninlil_join_record *accept,
                                const ninlil_join_record *previous,
                                const uint8_t id[32],
                                const uint8_t authority[16],
                                const uint8_t session[16],
                                ninlil_join_commit_fn writer, void *ctx,
                                ninlil_join_record *ack)
{
    ninlil_join_record active;
    int rc;
    if (!accept || !id || !authority || !session || !writer || !ack ||
        ninlil_join_grant_valid(&accept->grant) != NINLIL_OK ||
        accept->state != NINLIL_JOIN_PENDING || !nonzero(session, 16u) ||
        memcmp(id, accept->grant.identity, 32u) != 0 ||
        memcmp(authority, accept->grant.authority, 16u) != 0 ||
        memcmp(session, accept->transaction, 16u) != 0 ||
        (previous && !replacement(previous, accept)))
        return NINLIL_ERR_UNAUTHORIZED;
    active = *accept;
    active.state = NINLIL_JOIN_ACTIVE;
    if (previous && previous->state == NINLIL_JOIN_ACTIVE &&
        equal_grant(&previous->grant, &active.grant) &&
        memcmp(previous->transaction, active.transaction, 16u) == 0) {
        *ack = *previous;
        return NINLIL_OK;
    }
    rc = writer(ctx, &active);
    if (rc == NINLIL_OK)
        *ack = active;
    return rc;
}

int ninlil_join_confirm(ninlil_join_authority *a, const ninlil_join_record *ack,
                        const uint8_t session[16], uint64_t now)
{
    ninlil_join_peer *p;
    int rc;
    if (!usable(a) || !ack || !session ||
        ninlil_join_grant_valid(&ack->grant) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    p = find(a, ack->grant.identity);
    if (!p || !p->persisted || ack->state != NINLIL_JOIN_ACTIVE ||
        !equal_grant(&p->record.grant, &ack->grant) ||
        memcmp(p->session, session, 16u) != 0 ||
        memcmp(ack->transaction, session, 16u) != 0 ||
        p->record.state == NINLIL_JOIN_REVOKED)
        return NINLIL_ERR_UNAUTHORIZED;
    if (p->session_ready)
        return NINLIL_OK;
    if (!fresh(p, now) || p->phase != 3u)
        return NINLIL_ERR_STATE;
    rc = commit(a, p, ack);
    if (rc == NINLIL_OK) {
        p->session_ready = 1u;
        p->phase = 0u;
    }
    return rc;
}

int ninlil_join_resume(ninlil_join_authority *a,
                       const ninlil_join_record *saved,
                       const uint8_t session[16], uint64_t now)
{
    ninlil_join_peer *p;
    ninlil_join_record active;
    int rc;
    if (!usable(a) || !saved || !session ||
        ninlil_join_grant_valid(&saved->grant) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    p = find(a, saved->grant.identity);
    if (!fresh(p, now) || !p->persisted ||
        p->record.state != NINLIL_JOIN_ACTIVE ||
        saved->state != NINLIL_JOIN_ACTIVE ||
        !equal_grant(&p->record.grant, &saved->grant) ||
        memcmp(p->session, session, 16u) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    active = p->record;
    memcpy(active.transaction, session, 16u);
    rc = commit(a, p, &active);
    if (rc == NINLIL_OK) {
        p->session_ready = 1u;
        p->phase = 0u;
    }
    return rc;
}

int ninlil_join_revoke(ninlil_join_authority *a, const uint8_t id[32])
{
    ninlil_join_peer *p;
    ninlil_join_record revoked;
    if (!usable(a) || !id)
        return NINLIL_ERR_INVALID;
    p = find(a, id);
    if (!p || !p->persisted)
        return NINLIL_ERR_NOT_FOUND;
    p->session_ready = 0u;
    p->phase = 0u;
    memset(p->session, 0, 16u);
    if (p->record.state == NINLIL_JOIN_REVOKED)
        return NINLIL_OK;
    revoked = p->record;
    revoked.state = NINLIL_JOIN_REVOKED;
    return commit(a, p, &revoked);
}

void ninlil_join_disconnect(ninlil_join_authority *a, const uint8_t id[32])
{
    ninlil_join_peer *p;
    if (!usable(a) || !id)
        return;
    p = find(a, id);
    if (!p)
        return;
    if (!p->persisted) {
        memset(p, 0, sizeof(*p));
        return;
    }
    p->session_ready = 0u;
    p->phase = 0u;
    memset(p->session, 0, 16u);
}

void ninlil_join_expire(ninlil_join_authority *a, uint64_t now)
{
    uint16_t i;
    if (!usable(a))
        return;
    for (i = 0u; i < a->capacity; i++) {
        ninlil_join_peer *p = &a->peers[i];
        if (p->phase != 0u &&
            (now < p->began_ms || now - p->began_ms > NINLIL_JOIN_TIMEOUT_MS))
            ninlil_join_disconnect(a, p->identity);
    }
}

int ninlil_join_policy(void *ctx, uint16_t node, ninlil_peer_policy *policy)
{
    ninlil_join_authority *a = ctx;
    uint16_t i;
    if (!usable(a) || !policy)
        return NINLIL_ERR_UNAUTHORIZED;
    for (i = 0u; i < a->capacity; i++) {
        ninlil_join_peer *p = &a->peers[i];
        if (!p->session_ready || !p->persisted ||
            p->record.state != NINLIL_JOIN_ACTIVE ||
            p->record.grant.node != node)
            continue;
        memset(policy, 0, sizeof(*policy));
        policy->role = p->record.grant.role;
        policy->capabilities = p->record.grant.capabilities;
        policy->membership_epoch = p->record.grant.membership_epoch;
        policy->session_membership_epoch = policy->membership_epoch;
        policy->grants = p->record.grant.services;
        policy->grant_count = p->record.grant.service_count;
        return NINLIL_OK;
    }
    return NINLIL_ERR_UNAUTHORIZED;
}

int ninlil_join_endpoint_open(ninlil_join_endpoint *e, const uint8_t id[32],
                              const uint8_t authority[16],
                              ninlil_join_commit_fn writer, void *ctx)
{
    if (!e || !id || !authority || !writer)
        return NINLIL_ERR_INVALID;
    memset(e, 0, sizeof(*e));
    memcpy(e->identity, id, 32u);
    memcpy(e->authority, authority, 16u);
    e->commit = writer;
    e->commit_ctx = ctx;
    return NINLIL_OK;
}

int ninlil_join_endpoint_restore(ninlil_join_endpoint *e,
                                 const ninlil_join_record *record)
{
    ninlil_join_record candidate;
    if (!record)
        return NINLIL_ERR_CORRUPT;
    candidate = *record;
    if (candidate.state == NINLIL_JOIN_ACTIVE)
        candidate.state = NINLIL_JOIN_PENDING;
    if (!e || !e->commit || e->poisoned || !record ||
        ninlil_join_grant_valid(&record->grant) != NINLIL_OK ||
        !nonzero(record->transaction, 16u) ||
        (record->state != NINLIL_JOIN_ACTIVE &&
         record->state != NINLIL_JOIN_REVOKED) ||
        memcmp(e->identity, record->grant.identity, 32u) != 0 ||
        memcmp(e->authority, record->grant.authority, 16u) != 0 ||
        (e->persisted && !replacement(&e->record, &candidate)))
        return NINLIL_ERR_CORRUPT;
    e->record = *record;
    e->persisted = 1u;
    return NINLIL_OK;
}

static int endpoint_write(void *ctx, const ninlil_join_record *record)
{
    ninlil_join_endpoint *e = ctx;
    int rc = e->commit(e->commit_ctx, record);
    if (rc != NINLIL_OK)
        e->poisoned = 1u;
    return rc;
}

int ninlil_join_endpoint_accept(ninlil_join_endpoint *e,
                                const ninlil_join_record *accept,
                                const uint8_t session[16],
                                ninlil_join_record *ack)
{
    int rc;
    if (!e || !e->commit || e->poisoned)
        return NINLIL_ERR_STATE;
    rc = ninlil_join_endpoint_commit(accept, e->persisted ? &e->record : NULL,
                                     e->identity, e->authority, session,
                                     endpoint_write, e, ack);
    if (rc == NINLIL_OK) {
        e->record = *ack;
        e->persisted = 1u;
    } else if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_CORRUPT ||
               rc == NINLIL_ERR_FAULT) {
        e->poisoned = 1u;
    }
    return rc;
}
