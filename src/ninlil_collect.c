#include "ninlil_internal.h"
#include <string.h>

typedef struct collection {
    ninlil_runtime *runtime;
    ninlil_journal *replacement;
    size_t expected, copied;
} collection;
static ninlil_journal_ref *owned(ninlil_runtime *r, const ninlil_id *id)
{
    ninlil_outbound_entry *out = ninlil_find_outbound(r, id);
    ninlil_inbound_entry *in = ninlil_find_inbound(r, id);
    ninlil_archive_entry *archive = ninlil_find_archive_id(r, id);
    ninlil_rejection_entry *rejection = ninlil_find_rejection(r, id);
    return out                               ? &out->record_ref
           : in                              ? &in->record_ref
           : archive                         ? &archive->record_ref
           : rejection && rejection->durable ? &rejection->record_ref
                                             : NULL;
}
static uint64_t *binding_offset(ninlil_runtime *r, const ninlil_id *id)
{
    ninlil_outbound_entry *out = ninlil_find_outbound(r, id);
    ninlil_archive_entry *archive = ninlil_find_archive_id(r, id);
    return out ? &out->binding_offset
           : archive && archive->kind == NINLIL_ARCHIVE_OUTBOUND
               ? &archive->binding_offset
               : NULL;
}
static int binding_record(collection *c, const uint8_t *data, uint16_t length,
                          const ninlil_journal_ref *ref, int relocate)
{
    ninlil_id id;
    ninlil_delivery_binding binding;
    uint64_t *offset;
    ninlil_journal_ref *live;
    if (ninlil_binding_decode(data, length, &id, &binding) != NINLIL_OK ||
        !c->runtime->storage_bound ||
        memcmp(binding.source_identity, c->runtime->storage_identity, 32u))
        return NINLIL_ERR_CORRUPT;
    offset = binding_offset(c->runtime, &id);
    live = owned(c->runtime, &id);
    if (!offset || !*offset || !live || (!relocate && ref->offset != *offset))
        return NINLIL_OK; /* Unaccepted prefix or reclaimed history. */
    if ((relocate && live->generation == ref->generation) ||
        (!relocate && live->generation != ref->generation))
        return NINLIL_ERR_CORRUPT;
    c->copied++;
    if (relocate) {
        *offset = ref->offset;
        return NINLIL_OK;
    }
    return ninlil_journal_append(c->replacement, NINLIL_JRN_OUT_BINDING, data,
                                 length, NULL);
}
static int current_record(collection *c, uint8_t type, const uint8_t *data,
                          uint16_t length, const ninlil_journal_ref *ref,
                          int relocate)
{
    ninlil_id id;
    ninlil_journal_ref *live;
    int create = type == NINLIL_JRN_OUT_CREATE ||
                 type == NINLIL_JRN_IN_ACCEPT ||
                 type == NINLIL_JRN_IN_REJECTION;
    size_t offset = create ? 20u : 1u;
    if (type == NINLIL_JRN_STORAGE_BINDING) {
        if (!c->runtime->storage_bound || length != 33u || data[0] != 1u ||
            memcmp(data + 1, c->runtime->storage_identity, 32u) != 0)
            return NINLIL_ERR_CORRUPT;
        return relocate ? NINLIL_OK
                        : ninlil_journal_append(c->replacement, type, data,
                                                length, NULL);
    }
    if (type == NINLIL_JRN_OUT_BINDING)
        return binding_record(c, data, length, ref, relocate);
    if ((type < NINLIL_JRN_OUT_CREATE || type > NINLIL_JRN_IN_EXPIRED) &&
        type != NINLIL_JRN_BOUND_RELEASE)
        return NINLIL_ERR_CORRUPT;
    if (length < offset + NINLIL_ID_BYTES ||
        (data[0] != NINLIL_JRN_RECORD_VERSION &&
         !(type == NINLIL_JRN_OUT_CREATE &&
           data[0] == NINLIL_JRN_BOUND_CREATE_VERSION)))
        return NINLIL_ERR_CORRUPT;
    memcpy(id.bytes, data + offset, NINLIL_ID_BYTES);
    live = owned(c->runtime, &id);
    if (!live || (!relocate && ref->offset < live->offset))
        return NINLIL_OK;
    if (relocate) {
        if (create) {
            if (live->generation == ref->generation || live->type != type ||
                live->length != length)
                return NINLIL_ERR_CORRUPT;
            *live = *ref;
            c->copied++;
        }
        return NINLIL_OK;
    }
    if (ref->generation != live->generation)
        return NINLIL_ERR_CORRUPT;
    if (ref->offset == live->offset) {
        if (!create || live->type != type || live->length != length)
            return NINLIL_ERR_CORRUPT;
        c->copied++;
    }
    return ninlil_journal_append(c->replacement, type, data, length, NULL);
}
static int copy_current(void *ctx, uint8_t type, const uint8_t *data,
                        uint16_t length, const ninlil_journal_ref *ref)
{
    return current_record(ctx, type, data, length, ref, 0);
}
static int relocate_current(void *ctx, uint8_t type, const uint8_t *data,
                            uint16_t length, const ninlil_journal_ref *ref)
{
    return current_record(ctx, type, data, length, ref, 1);
}
static int snapshot(void *ctx, ninlil_journal *replacement)
{
    collection *c = ctx;
    int rc;
    c->replacement = replacement;
    rc = ninlil_journal_visit(c->runtime->journal, copy_current, c);
    return rc != NINLIL_OK            ? rc
           : c->copied == c->expected ? NINLIL_OK
                                      : NINLIL_ERR_CORRUPT;
}
int ninlil_collect(ninlil_runtime *r)
{
    collection c = {0};
    uint64_t used, capacity;
    int rc = ninlil_health(r);
    if (rc != NINLIL_OK)
        return rc;
    c.runtime = r;
    c.expected = (size_t)r->outbound_live + r->inbound_live;
    for (uint16_t i = 0u; i < r->outbound_capacity; i++)
        c.expected +=
            r->outbound[i].used && r->outbound[i].binding_offset ? 1u : 0u;
    for (uint16_t i = 0u; i < r->archive_capacity; i++)
        c.expected +=
            r->archive[i].used ? (r->archive[i].binding_offset ? 2u : 1u) : 0u;
    for (uint16_t i = 0u; i < r->rejection_capacity; i++)
        c.expected +=
            r->rejections[i].used && r->rejections[i].durable ? 1u : 0u;
    rc = ninlil_journal_rewrite(r->journal, snapshot, &c);
    if (rc == NINLIL_OK) {
        /* Publication is durable. Rebuild RAM references before any effect;
         * a read failure poisons this instance and restart replays the new
         * bank. */
        c.copied = 0u;
        rc = ninlil_journal_visit(r->journal, relocate_current, &c);
        if (rc == NINLIL_OK && c.copied != c.expected)
            rc = NINLIL_ERR_CORRUPT;
        if (rc == NINLIL_OK)
            rc = ninlil_verify_retained(r);
        if (rc == NINLIL_OK)
            rc = ninlil_journal_usage(r->journal, &used, &capacity);
        if (rc == NINLIL_OK)
            r->collected_bytes = used;
    }
    if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY &&
        rc != NINLIL_ERR_NOT_FOUND)
        r->fatal_error = rc;
    return rc;
}
int ninlil_set_collection(ninlil_runtime *r, int automatic)
{
    if (!r || (automatic != 0 && automatic != 1))
        return NINLIL_ERR_INVALID;
    r->manual_collection = (uint8_t)(automatic ? 0u : 1u);
    return NINLIL_OK;
}
int ninlil_collect_if_needed(ninlil_runtime *r)
{
    uint64_t used, capacity;
    int rc;
    if (r->manual_collection)
        return NINLIL_OK;
    rc = ninlil_journal_usage(r->journal, &used, &capacity);
    if (rc != NINLIL_OK)
        return r->fatal_error = rc;
    if (used < capacity - capacity / 4u || used == r->collected_bytes)
        return NINLIL_OK;
    rc = ninlil_collect(r);
    if (rc == NINLIL_ERR_NOT_FOUND)
        r->collected_bytes = used;
    return rc == NINLIL_ERR_NOT_FOUND || rc == NINLIL_ERR_CAPACITY ? NINLIL_OK
                                                                   : rc;
}
