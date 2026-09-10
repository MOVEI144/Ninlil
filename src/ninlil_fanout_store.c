#include "ninlil_fanout_store_internal.h"
#include <stdlib.h>
#include <string.h>

static int guard(ninlil_fanout_store *s)
{
    if (!s)
        return NINLIL_ERR_INVALID;
    if (s->busy)
        return NINLIL_ERR_BUSY;
    if (s->fault || s->owner.poisoned)
        return NINLIL_ERR_STATE;
    s->busy = 1u;
    return NINLIL_OK;
}
static int finish(ninlil_fanout_store *s, int rc)
{
    if (s->owner.poisoned && !s->fault)
        s->fault = rc;
    s->busy = 0u;
    return rc;
}
static int before_callback(ninlil_fanout_store *s,
                           const ninlil_fanout_target *target)
{
    uint16_t index;
    int rc;
    for (index = 0u; index < s->count; index++)
        if (target == &s->targets[index])
            break;
    if (index == s->count)
        return ninlil_fanout_store_fail(s, NINLIL_ERR_CORRUPT);
    rc = ninlil_fanout_store_verify(s, 0);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_verify_target(s, index);
    return rc;
}
static int eligible(void *ctx, const ninlil_fanout_contract *c,
                    const ninlil_fanout_target *t)
{
    ninlil_fanout_store *s = ctx;
    int rc = before_callback(s, t);
    return rc == NINLIL_OK ? s->config.eligible(s->config.delivery_ctx, c, t)
                           : rc;
}
static int admit(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, ninlil_id *out)
{
    ninlil_fanout_store *s = ctx;
    uint8_t payload[256];
    int rc = before_callback(s, t);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_load_payload(s, payload);
    return rc == NINLIL_OK
               ? s->config.admit_bound(s->config.delivery_ctx, c, t, payload,
                                       s->payload_length, out)
               : rc;
}
static int query(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, const ninlil_id *message,
                 ninlil_info *info)
{
    ninlil_fanout_store *s = ctx;
    int rc = before_callback(s, t);
    if (rc == NINLIL_OK)
        rc = s->config.query_bound(s->config.delivery_ctx, c, t, message, info);
    if (rc == NINLIL_OK && info->payload_len != s->payload_length)
        return NINLIL_ERR_CORRUPT;
    return rc;
}
size_t ninlil_fanout_store_memory(uint16_t capacity)
{
    if (!capacity || capacity > NINLIL_FANOUT_TARGETS)
        return 0u;
    return sizeof(ninlil_fanout_store) +
           (size_t)capacity * (sizeof(ninlil_fanout_target) +
                               sizeof(ninlil_fanout_item) + sizeof(nfs_ref));
}
int ninlil_fanout_store_open(ninlil_fanout_store **out,
                             const ninlil_fanout_store_config *c,
                             ninlil_fanout_store_mode mode)
{
    ninlil_fanout_store *s;
    ninlil_fanout_callbacks cb = {ninlil_fanout_store_commit, eligible, admit,
                                  query, NULL};
    int rc;
    if (!out)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    if (!c || !c->location || !c->location[0] ||
        strlen(c->location) > NINLIL_JOURNAL_LOCATION_MAX ||
        !ninlil_fanout_store_memory(c->target_capacity) ||
        c->maximum_bytes < 512u ||
        c->maximum_bytes > NINLIL_FANOUT_STORE_MAX_BYTES || !c->sha256 ||
        !c->eligible || !c->admit_bound || !c->query_bound ||
        !memcmp(c->source_identity, (uint8_t[32]){0}, 32u) ||
        !memcmp(c->operation.bytes, (uint8_t[16]){0}, 16u) ||
        (mode != NINLIL_FANOUT_STORE_INITIALIZE &&
         mode != NINLIL_FANOUT_STORE_RESUME))
        return NINLIL_ERR_INVALID;
    s = calloc(1u, sizeof(*s));
    if (!s)
        return NINLIL_ERR_CAPACITY;
    s->config = *c;
    s->busy = 1u;
    s->targets = calloc(c->target_capacity, sizeof(*s->targets));
    s->items = calloc(c->target_capacity, sizeof(*s->items));
    s->refs = calloc(c->target_capacity, sizeof(*s->refs));
    if (!s->targets || !s->items || !s->refs)
        rc = NINLIL_ERR_CAPACITY;
    else {
        cb.ctx = s;
        rc = ninlil_fanout_open(&s->owner, s->targets, s->items,
                                c->target_capacity, &cb);
        if (rc == NINLIL_OK)
            rc = ninlil_journal_open(&s->journal, c->location, c->maximum_bytes,
                                     ninlil_fanout_store_replay, s);
        if (rc == NINLIL_OK && mode == NINLIL_FANOUT_STORE_RESUME && !s->sealed)
            rc = NINLIL_ERR_EMPTY;
    }
    s->busy = 0u;
    if (rc != NINLIL_OK) {
        ninlil_fanout_store_close(s);
        return rc;
    }
    *out = s;
    return NINLIL_OK;
}
void ninlil_fanout_store_close(ninlil_fanout_store *s)
{
    if (!s || s->busy)
        return; /* Public callbacks must never close their owner. */
    ninlil_journal_close(s->journal);
    free(s->targets);
    free(s->items);
    free(s->refs);
    free(s);
}
int ninlil_fanout_store_start(ninlil_fanout_store *s,
                              const ninlil_fanout_contract *c,
                              const ninlil_fanout_target *targets,
                              uint16_t count, const uint8_t *payload,
                              uint16_t length)
{
    uint8_t digest[32], saved[256];
    int rc;
    if (!s || !c || !targets || !count || count > s->config.target_capacity ||
        length > 256u || (length && !payload) ||
        !ninlil_fanout_contract_valid(c) ||
        !ninlil_fanout_snapshot_valid(targets, count, c->source) ||
        memcmp(c->source, s->config.source_identity, 32u) ||
        memcmp(c->operation.bytes, s->config.operation.bytes, 16u))
        return NINLIL_ERR_INVALID;
    rc = guard(s);
    if (rc != NINLIL_OK)
        return rc;
    rc = s->config.sha256(s->config.hash_ctx,
                          length ? payload : (uint8_t[1]){0}, length, digest);
    if (rc == NINLIL_OK && memcmp(digest, c->payload_digest, 32u))
        rc = NINLIL_ERR_CONFLICT;
    if (rc == NINLIL_OK && s->has_header &&
        (!ninlil_fanout_contract_equal(c, &s->contract) || count != s->count ||
         length != s->payload_length))
        rc = NINLIL_ERR_CONFLICT;
    for (uint16_t i = 0u; rc == NINLIL_OK && i < s->loaded; i++)
        if (!ninlil_fanout_target_equal(&targets[i], &s->targets[i]))
            rc = NINLIL_ERR_CONFLICT;
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_verify(s, 1);
    if (rc == NINLIL_OK && s->has_payload) {
        rc = ninlil_fanout_store_load_payload(s, saved);
        if (rc == NINLIL_OK && length && memcmp(saved, payload, length))
            rc = NINLIL_ERR_CONFLICT;
    }
    if (rc == NINLIL_OK) {
        s->payload_length = length;
        s->starting_payload = payload;
        rc = ninlil_fanout_start(&s->owner, c, targets, count);
        s->starting_payload = NULL;
    }
    return finish(s, rc);
}
int ninlil_fanout_store_step(ninlil_fanout_store *s, uint64_t now,
                             unsigned int work)
{
    int rc = guard(s);
    if (rc != NINLIL_OK)
        return rc;
    rc = s->sealed ? ninlil_fanout_store_verify(s, 0) : NINLIL_ERR_EMPTY;
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_step(&s->owner, now, work);
    return finish(s, rc);
}
int ninlil_fanout_store_inspect(ninlil_fanout_store *s,
                                ninlil_fanout_status *out)
{
    int rc;
    if (!out)
        return NINLIL_ERR_INVALID;
    rc = guard(s);
    if (rc != NINLIL_OK)
        return rc;
    rc = s->sealed ? ninlil_fanout_store_verify(s, 1) : NINLIL_ERR_EMPTY;
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_inspect(&s->owner, out);
    return finish(s, rc);
}
int ninlil_fanout_store_target(ninlil_fanout_store *s, uint16_t index,
                               ninlil_fanout_target *out,
                               ninlil_fanout_item *item)
{
    int rc;
    if (!s || !out || !item || index >= s->count)
        return NINLIL_ERR_INVALID;
    rc = guard(s);
    if (rc != NINLIL_OK)
        return rc;
    rc = s->sealed ? ninlil_fanout_store_verify(s, 0) : NINLIL_ERR_EMPTY;
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_verify_target(s, index);
    if (rc == NINLIL_OK) {
        *out = s->targets[index];
        *item = s->items[index];
    }
    return finish(s, rc);
}
int ninlil_fanout_store_payload(ninlil_fanout_store *s, uint8_t *out,
                                size_t capacity, uint16_t *written)
{
    uint8_t payload[256];
    uint16_t length;
    int rc;
    if (!s || !written || (!out && capacity))
        return NINLIL_ERR_INVALID;
    rc = guard(s);
    if (rc != NINLIL_OK)
        return rc;
    length = s->payload_length;
    if (!s->sealed)
        rc = NINLIL_ERR_EMPTY;
    else if (capacity < length)
        rc = NINLIL_ERR_TOO_LARGE;
    else if (length && !out)
        rc = NINLIL_ERR_INVALID;
    else
        rc = ninlil_fanout_store_verify(s, 0);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_load_payload(s, payload);
    if (rc == NINLIL_OK && length != s->payload_length)
        rc = ninlil_fanout_store_fail(s, NINLIL_ERR_CORRUPT);
    if (rc == NINLIL_OK) {
        if (length)
            memcpy(out, payload, length);
        *written = length;
    }
    return finish(s, rc);
}

int ninlil_fanout_store_contract(ninlil_fanout_store *s,
                                 ninlil_fanout_contract *out, uint16_t *count)
{
    int rc;
    if (!s || !out || !count)
        return NINLIL_ERR_INVALID;
    rc = guard(s);
    if (rc != NINLIL_OK)
        return rc;
    rc = s->sealed ? ninlil_fanout_store_verify(s, 0) : NINLIL_ERR_EMPTY;
    if (rc == NINLIL_OK) {
        *out = s->contract;
        *count = s->count;
    }
    return finish(s, rc);
}
