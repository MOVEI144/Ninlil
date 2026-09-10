#include "ninlil_fanout_store_internal.h"
#include <string.h>

static int matched_read(ninlil_fanout_store *s, const ninlil_journal_ref *ref,
                        const uint8_t *expected, uint16_t size)
{
    uint8_t readback[NFS_MAX_RECORD];
    int rc;
    if (size > sizeof(readback) || ref->length != size)
        return ninlil_fanout_store_fail(s, NINLIL_ERR_CORRUPT);
    rc = ninlil_journal_read(s->journal, ref, 0u, readback, size);
    if (rc == NINLIL_OK && memcmp(readback, expected, size))
        rc = NINLIL_ERR_CORRUPT;
    if (rc != NINLIL_OK)
        (void)ninlil_fanout_store_fail(s, rc);
    return rc;
}
static int payload_hash(ninlil_fanout_store *s, const uint8_t *data)
{
    uint8_t digest[32];
    int rc =
        s->config.sha256(s->config.hash_ctx, data, s->payload_length, digest);
    if (rc == NINLIL_OK && memcmp(digest, s->contract.payload_digest, 32u))
        rc = NINLIL_ERR_CORRUPT;
    return rc;
}
int ninlil_fanout_store_load_payload(ninlil_fanout_store *s, uint8_t out[256])
{
    uint8_t data[NFS_MAX_RECORD];
    int rc;
    if (!s->has_payload ||
        s->payload.length != NFS_PAYLOAD_HEADER + s->payload_length)
        return ninlil_fanout_store_fail(s, NINLIL_ERR_CORRUPT);
    rc = ninlil_journal_read(s->journal, &s->payload, 0u, data,
                             s->payload.length);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_payload_decode(
            data, s->payload.length, &s->contract.operation, s->payload_length);
    if (rc == NINLIL_OK)
        rc = payload_hash(s, data + NFS_PAYLOAD_HEADER);
    if (rc != NINLIL_OK)
        return ninlil_fanout_store_fail(s, rc);
    if (s->payload_length)
        memcpy(out, data + NFS_PAYLOAD_HEADER, s->payload_length);
    return NINLIL_OK;
}
int ninlil_fanout_store_verify_target(ninlil_fanout_store *s, uint16_t index)
{
    uint8_t data[NFS_TARGET_SIZE];
    const ninlil_fanout_item *item;
    ninlil_fanout_record r = {0};
    int rc;
    if (index >= s->loaded)
        return NINLIL_ERR_INVALID;
    ninlil_fanout_store_target_encode(&s->contract.operation, index,
                                      &s->targets[index], data);
    rc = matched_read(s, &s->refs[index].target, data, NFS_TARGET_SIZE);
    if (rc != NINLIL_OK || !s->sealed)
        return rc;
    item = &s->items[index];
    if (item->phase == NINLIL_FANOUT_PENDING)
        return NINLIL_OK;
    r.contract = s->contract;
    r.sequence = s->refs[index].sequence;
    r.index = index;
    r.kind = item->phase == NINLIL_FANOUT_INTENT ? NINLIL_FANOUT_TARGET_INTENT
             : item->phase == NINLIL_FANOUT_ADMITTED
                 ? NINLIL_FANOUT_TARGET_ADMITTED
                 : NINLIL_FANOUT_TARGET_TERMINAL;
    r.message = item->message;
    r.outcome = item->outcome;
    r.evidence = item->latest;
    ninlil_fanout_store_delta_encode(&r, data);
    return matched_read(s, &s->refs[index].state, data, NFS_DELTA_SIZE);
}
int ninlil_fanout_store_verify(ninlil_fanout_store *s, int all)
{
    uint8_t data[NFS_HEADER_SIZE], payload[256];
    int rc;
    if (!s->has_header)
        return NINLIL_OK;
    ninlil_fanout_store_header_encode(&s->contract, s->count, s->payload_length,
                                      data);
    rc = matched_read(s, &s->header, data, NFS_HEADER_SIZE);
    if (rc == NINLIL_OK && s->has_payload)
        rc = ninlil_fanout_store_load_payload(s, payload);
    if (rc == NINLIL_OK && s->sealed) {
        ninlil_fanout_store_seal_encode(&s->contract.operation, s->count,
                                        s->payload_length, data);
        rc = matched_read(s, &s->seal, data, NFS_SEAL_SIZE);
    }
    for (uint16_t i = 0u; rc == NINLIL_OK && all && i < s->loaded; i++)
        rc = ninlil_fanout_store_verify_target(s, i);
    return rc;
}
static int read_header(ninlil_fanout_store *s, const uint8_t *data,
                       uint16_t length, const ninlil_journal_ref *ref)
{
    ninlil_fanout_contract c;
    uint16_t count, size;
    int rc;
    if (s->has_header)
        return NINLIL_ERR_CORRUPT;
    rc = ninlil_fanout_store_header_decode(data, length, &c, &count, &size);
    if (rc != NINLIL_OK)
        return rc;
    if (memcmp(c.source, s->config.source_identity, 32u) ||
        memcmp(c.operation.bytes, s->config.operation.bytes, 16u))
        return NINLIL_ERR_CONFLICT;
    if (count > s->config.target_capacity)
        return NINLIL_ERR_CAPACITY;
    s->contract = c;
    s->count = count;
    s->payload_length = size;
    s->header = *ref;
    s->has_header = 1u;
    return NINLIL_OK;
}
int ninlil_fanout_store_replay(void *ctx, uint8_t type, const uint8_t *data,
                               uint16_t length, const ninlil_journal_ref *ref)
{
    ninlil_fanout_store *s = ctx;
    ninlil_fanout_record r = {0};
    int rc;
    if (type == NFS_HEADER)
        return read_header(s, data, length, ref);
    if (!s->has_header)
        return NINLIL_ERR_CORRUPT;
    if (type == NFS_TARGET) {
        ninlil_fanout_target t;
        if (s->sealed || s->has_payload || s->loaded >= s->count)
            return NINLIL_ERR_CORRUPT;
        rc = ninlil_fanout_store_target_decode(
            data, length, &s->contract.operation, s->loaded, &t);
        if (rc != NINLIL_OK)
            return rc;
        if (!memcmp(t.identity, s->contract.source, 32u) ||
            (s->loaded &&
             memcmp(s->targets[s->loaded - 1u].identity, t.identity, 32u) >= 0))
            return NINLIL_ERR_CORRUPT;
        for (uint16_t i = 0u; i < s->loaded; i++)
            if (s->targets[i].address == t.address ||
                !memcmp(s->targets[i].idempotency_key.bytes,
                        t.idempotency_key.bytes, 16u))
                return NINLIL_ERR_CORRUPT;
        s->targets[s->loaded] = t;
        s->refs[s->loaded++].target = *ref;
        return NINLIL_OK;
    }
    if (type == NFS_PAYLOAD) {
        if (s->sealed || s->has_payload || s->loaded != s->count)
            return NINLIL_ERR_CORRUPT;
        rc = ninlil_fanout_store_payload_decode(
            data, length, &s->contract.operation, s->payload_length);
        if (rc == NINLIL_OK)
            rc = payload_hash(s, data + NFS_PAYLOAD_HEADER);
        if (rc != NINLIL_OK)
            return rc;
        s->payload = *ref;
        s->has_payload = 1u;
        return NINLIL_OK;
    }
    r.contract = s->contract;
    r.schema = NINLIL_FANOUT_SCHEMA;
    if (type == NFS_SEAL) {
        uint8_t seal[NFS_SEAL_SIZE];
        ninlil_fanout_store_seal_encode(&s->contract.operation, s->count,
                                        s->payload_length, seal);
        if (s->sealed || !s->has_payload || s->loaded != s->count ||
            length != sizeof(seal) || memcmp(seal, data, sizeof(seal)))
            return NINLIL_ERR_CORRUPT;
        r.kind = NINLIL_FANOUT_START;
        r.sequence = 1u;
        r.count = s->count;
        r.targets = s->targets;
        rc = ninlil_fanout_restore(&s->owner, &r);
        if (rc != NINLIL_OK)
            return rc;
        s->seal = *ref;
        s->sealed = 1u;
        return NINLIL_OK;
    }
    if (type != NFS_DELTA || !s->sealed)
        return NINLIL_ERR_CORRUPT;
    rc = ninlil_fanout_store_delta_decode(data, length, &s->contract, &r);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_restore(&s->owner, &r);
    if (rc != NINLIL_OK)
        return rc;
    s->refs[r.index].state = *ref;
    s->refs[r.index].sequence = r.sequence;
    return NINLIL_OK;
}
static int append_record(ninlil_fanout_store *s, uint8_t type,
                         const uint8_t *data, uint16_t size,
                         ninlil_journal_ref *ref)
{
    int rc = ninlil_journal_append(s->journal, type, data, size, ref);
    if (rc == NINLIL_OK)
        rc = matched_read(s, ref, data, size);
    /* CAPACITY here is a persistence failure, not permission to skip a target.
     */
    if (rc != NINLIL_OK)
        (void)ninlil_fanout_store_fail(s, rc);
    return rc;
}
static int persist_start(ninlil_fanout_store *s, const ninlil_fanout_record *r)
{
    uint8_t data[NFS_MAX_RECORD];
    int rc = ninlil_fanout_store_verify(s, 1);
    if (rc != NINLIL_OK)
        return rc;
    if (!s->has_header) {
        ninlil_fanout_store_header_encode(&r->contract, r->count,
                                          s->payload_length, data);
        rc = append_record(s, NFS_HEADER, data, NFS_HEADER_SIZE, &s->header);
        if (rc != NINLIL_OK)
            return rc;
        s->contract = r->contract;
        s->count = r->count;
        s->has_header = 1u;
    }
    for (uint16_t i = s->loaded; i < r->count; i++) {
        ninlil_fanout_store_target_encode(&r->contract.operation, i,
                                          &r->targets[i], data);
        rc = append_record(s, NFS_TARGET, data, NFS_TARGET_SIZE,
                           &s->refs[i].target);
        if (rc != NINLIL_OK)
            return rc;
        s->targets[i] = r->targets[i];
        s->loaded++;
    }
    if (!s->has_payload) {
        ninlil_fanout_store_payload_encode(&r->contract.operation,
                                           s->starting_payload,
                                           s->payload_length, data);
        rc = append_record(s, NFS_PAYLOAD, data,
                           (uint16_t)(NFS_PAYLOAD_HEADER + s->payload_length),
                           &s->payload);
        if (rc != NINLIL_OK)
            return rc;
        s->has_payload = 1u;
    }
    ninlil_fanout_store_seal_encode(&r->contract.operation, r->count,
                                    s->payload_length, data);
    rc = append_record(s, NFS_SEAL, data, NFS_SEAL_SIZE, &s->seal);
    if (rc == NINLIL_OK)
        s->sealed = 1u;
    return rc;
}
int ninlil_fanout_store_commit(void *ctx, const ninlil_fanout_record *r)
{
    ninlil_fanout_store *s = ctx;
    uint8_t data[NFS_DELTA_SIZE];
    ninlil_journal_ref ref;
    int rc;
    if (s->fault)
        return s->fault;
    rc = ninlil_fanout_record_check(&s->owner, r);
    if (rc != NINLIL_OK)
        return rc;
    if (r->kind == NINLIL_FANOUT_START)
        return persist_start(s, r);
    rc = ninlil_fanout_store_verify(s, 0);
    if (rc == NINLIL_OK)
        rc = ninlil_fanout_store_verify_target(s, r->index);
    if (rc != NINLIL_OK)
        return rc;
    ninlil_fanout_store_delta_encode(r, data);
    rc = append_record(s, NFS_DELTA, data, sizeof(data), &ref);
    if (rc == NINLIL_OK) {
        s->refs[r->index].state = ref;
        s->refs[r->index].sequence = r->sequence;
    }
    return rc;
}
