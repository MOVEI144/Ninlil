#include "ninlil_fanout_core.h"
#include "ninlil_fanout_internal.h"
#include <string.h>

static int binding(const ninlil_fanout_contract *c,
                   const ninlil_fanout_target *t, ninlil_delivery_binding *out)
{
    ninlil_delivery_binding b = {0};
    if (!ninlil_fanout_contract_valid(c) || !t || !t->address ||
        t->address == UINT16_MAX ||
        !memcmp(t->idempotency_key.bytes, (uint8_t[16]){0}, 16u))
        return NINLIL_ERR_INVALID;
    memcpy(b.source_identity, c->source, 32u);
    memcpy(b.peer_identity, t->identity, 32u);
    memcpy(b.authority, c->authority, 16u);
    b.authority_epoch = c->authority_epoch;
    b.membership_epoch = t->membership_epoch;
    b.binding_epoch = t->binding_epoch;
    if (!ninlil_delivery_binding_valid(&b))
        return NINLIL_ERR_INVALID;
    *out = b;
    return NINLIL_OK;
}
static int eligible(void *ctx, const ninlil_fanout_contract *c,
                    const ninlil_fanout_target *t)
{
    ninlil_fanout_core *a = ctx;
    ninlil_delivery_binding b;
    int rc = binding(c, t, &b);
    return rc == NINLIL_OK ? ninlil_binding_current(a->core, t->address, &b)
                           : rc;
}
static int admit(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, const uint8_t *payload,
                 uint16_t length, ninlil_id *out)
{
    ninlil_fanout_core *a = ctx;
    ninlil_delivery_binding b;
    ninlil_submission s;
    uint8_t digest[32];
    int rc = binding(c, t, &b);
    if (rc != NINLIL_OK || !out || length > NINLIL_MAX_PAYLOAD ||
        (length && !payload))
        return NINLIL_ERR_INVALID;
    rc = a->sha256(a->hash_ctx, length ? payload : (uint8_t[1]){0}, length,
                   digest);
    if (rc != NINLIL_OK)
        return rc;
    if (memcmp(digest, c->payload_digest, 32u))
        return NINLIL_ERR_CONFLICT;
    ninlil_submission_defaults(&s);
    s.idempotency_key = t->idempotency_key;
    s.target = t->address;
    s.service = c->service;
    s.ownership = NINLIL_OWNERSHIP_DURABLE;
    s.required_evidence = c->evidence;
    s.traffic_class = c->traffic;
    s.absolute_deadline_ms = c->deadline_ms;
    s.payload = payload;
    s.payload_len = length;
    return ninlil_submit_bound(a->core, &s, &b, out);
}
static int query(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, const ninlil_id *id,
                 ninlil_info *out)
{
    ninlil_fanout_core *a = ctx;
    ninlil_delivery_binding expected, saved;
    ninlil_info info;
    int rc = binding(c, t, &expected);
    if (rc != NINLIL_OK || !id || !out)
        return NINLIL_ERR_INVALID;
    rc = ninlil_query_binding(a->core, id, &saved);
    if (rc != NINLIL_OK)
        return rc;
    if (!ninlil_delivery_binding_equal(&saved, &expected))
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_query(a->core, id, &info);
    if (rc != NINLIL_OK)
        return rc;
    if (info.peer != t->address || info.service != c->service ||
        info.ownership != NINLIL_OWNERSHIP_DURABLE ||
        info.required_evidence != c->evidence ||
        info.traffic_class != c->traffic ||
        info.absolute_deadline_ms != c->deadline_ms)
        return NINLIL_ERR_CONFLICT;
    *out = info;
    return NINLIL_OK;
}
int ninlil_fanout_core_connect(ninlil_fanout_core *a, ninlil_runtime *core,
                               ninlil_fanout_store_config *c)
{
    if (!a || !core || !c || !c->sha256 || c->eligible || c->admit_bound ||
        c->query_bound)
        return NINLIL_ERR_INVALID;
    if (ninlil_health(core) != NINLIL_OK)
        return NINLIL_ERR_STATE;
    memset(a, 0, sizeof(*a));
    a->core = core;
    a->sha256 = c->sha256;
    a->hash_ctx = c->hash_ctx;
    c->eligible = eligible;
    c->admit_bound = admit;
    c->query_bound = query;
    c->delivery_ctx = a;
    return NINLIL_OK;
}
int ninlil_fanout_core_step(ninlil_fanout_core *a, ninlil_fanout_store *s,
                            uint64_t now, unsigned int work)
{
    ninlil_fanout_contract c;
    uint16_t count;
    int rc;
    if (!a || !a->core || !s || !work || work > NINLIL_FANOUT_WORK)
        return NINLIL_ERR_INVALID;
    rc = ninlil_fanout_store_step(s, now, work);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_contract(s, &c, &count);
    if (rc != NINLIL_OK)
        return rc;
    for (unsigned int i = 0u; i < work && i < count; i++) {
        ninlil_fanout_target t;
        ninlil_fanout_item item;
        ninlil_info info;
        uint16_t index = (uint16_t)(a->release_cursor % count);
        a->release_cursor = (uint16_t)((index + 1u) % count);
        rc = ninlil_fanout_store_target(s, index, &t, &item);
        if (rc != NINLIL_OK)
            return rc;
        if (item.phase != NINLIL_FANOUT_TERMINAL)
            continue;
        rc = query(a, &c, &t, &item.message, &info);
        if (rc == NINLIL_ERR_NOT_FOUND) {
            /* Only a verified TERMINAL target may outlive Core retention. */
            rc = ninlil_query(a->core, &item.message, &info);
            if (rc == NINLIL_ERR_NOT_FOUND)
                continue;
            return rc == NINLIL_OK ? NINLIL_ERR_CONFLICT : rc;
        }
        if (rc != NINLIL_OK)
            return rc;
        if (info.outcome != item.outcome || info.latest_evidence != item.latest)
            return NINLIL_ERR_CONFLICT;
        rc = ninlil_release_bound(a->core, &item.message);
        if (rc != NINLIL_OK)
            return rc;
    }
    return NINLIL_OK;
}
