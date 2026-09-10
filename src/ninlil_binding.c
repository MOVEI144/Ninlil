#include "ninlil_internal.h"
#include <string.h>

static int nonzero(const uint8_t *p, size_t length)
{
    uint8_t bits = 0u;
    for (size_t i = 0u; i < length; i++)
        bits |= p[i];
    return bits != 0u;
}
static void put64(uint8_t *p, uint64_t value)
{
    for (unsigned int i = 0u; i < 8u; i++) {
        p[7u - i] = (uint8_t)value;
        value >>= 8;
    }
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t value = 0u;
    for (unsigned int i = 0u; i < 8u; i++)
        value = (value << 8) | p[i];
    return value;
}
int ninlil_delivery_binding_valid(const ninlil_delivery_binding *b)
{
    return b && nonzero(b->source_identity, 32u) &&
           nonzero(b->peer_identity, 32u) && nonzero(b->authority, 16u) &&
           memcmp(b->source_identity, b->peer_identity, 32u) != 0 &&
           b->authority_epoch && b->membership_epoch && b->binding_epoch;
}
int ninlil_delivery_binding_equal(const ninlil_delivery_binding *a,
                                  const ninlil_delivery_binding *b)
{
    return a && b && !memcmp(a->source_identity, b->source_identity, 32u) &&
           !memcmp(a->peer_identity, b->peer_identity, 32u) &&
           !memcmp(a->authority, b->authority, 16u) &&
           a->authority_epoch == b->authority_epoch &&
           a->membership_epoch == b->membership_epoch &&
           a->binding_epoch == b->binding_epoch;
}
int ninlil_binding_decode(const uint8_t *p, uint16_t length, ninlil_id *id,
                          ninlil_delivery_binding *out)
{
    ninlil_delivery_binding b = {0};
    if (!p || !id || !out || length != NINLIL_JRN_BINDING_BYTES ||
        memcmp(p, "NDB\001", 4u) || !nonzero(p + 4, 16u))
        return NINLIL_ERR_CORRUPT;
    memcpy(b.source_identity, p + 20, 32u);
    memcpy(b.peer_identity, p + 52, 32u);
    memcpy(b.authority, p + 84, 16u);
    b.authority_epoch = get64(p + 100);
    b.membership_epoch = get64(p + 108);
    b.binding_epoch = get64(p + 116);
    if (!ninlil_delivery_binding_valid(&b))
        return NINLIL_ERR_CORRUPT;
    memcpy(id->bytes, p + 4, 16u);
    *out = b;
    return NINLIL_OK;
}
int ninlil_binding_log(ninlil_runtime *r, const ninlil_id *id,
                       const ninlil_delivery_binding *b, uint64_t *offset)
{
    uint8_t p[NINLIL_JRN_BINDING_BYTES], checked[sizeof(p)];
    ninlil_journal_ref ref;
    int rc;
    if (!r || !id || !offset || !nonzero(id->bytes, 16u) ||
        !ninlil_delivery_binding_valid(b) || !r->storage_bound ||
        memcmp(r->storage_identity, b->source_identity, 32u))
        return NINLIL_ERR_INVALID;
    memcpy(p, "NDB\001", 4u);
    memcpy(p + 4, id->bytes, 16u);
    memcpy(p + 20, b->source_identity, 32u);
    memcpy(p + 52, b->peer_identity, 32u);
    memcpy(p + 84, b->authority, 16u);
    put64(p + 100, b->authority_epoch);
    put64(p + 108, b->membership_epoch);
    put64(p + 116, b->binding_epoch);
    rc = ninlil_append_record(r, NINLIL_JRN_OUT_BINDING, p, sizeof(p), &ref);
    if (rc == NINLIL_OK)
        rc = ninlil_read_payload(r, &ref, 0u, checked, sizeof(checked));
    if (rc == NINLIL_OK && (!ref.offset || memcmp(p, checked, sizeof(p))))
        rc = r->fatal_error = NINLIL_ERR_CORRUPT;
    if (rc == NINLIL_OK)
        *offset = ref.offset;
    return rc;
}
int ninlil_binding_read(ninlil_runtime *r, const ninlil_id *id, uint64_t offset,
                        uint32_t generation, ninlil_delivery_binding *out)
{
    uint8_t p[NINLIL_JRN_BINDING_BYTES];
    ninlil_journal_ref ref = {offset, generation, sizeof(p),
                              NINLIL_JRN_OUT_BINDING};
    ninlil_delivery_binding b;
    ninlil_id saved;
    int rc;
    if (!r || !id || !out)
        return NINLIL_ERR_INVALID;
    if (r->fatal_error)
        return r->fatal_error;
    if (!offset)
        return NINLIL_ERR_NOT_FOUND;
    rc = ninlil_read_payload(r, &ref, 0u, p, sizeof(p));
    if (rc == NINLIL_OK)
        rc = ninlil_binding_decode(p, sizeof(p), &saved, &b);
    if (rc == NINLIL_OK &&
        (!r->storage_bound || !ninlil_id_equal(id, &saved) ||
         memcmp(r->storage_identity, b.source_identity, 32u)))
        rc = NINLIL_ERR_CORRUPT;
    if (rc != NINLIL_OK)
        return r->fatal_error = rc;
    *out = b;
    return NINLIL_OK;
}
int ninlil_binding_current(ninlil_runtime *r, uint16_t peer,
                           const ninlil_delivery_binding *expected)
{
    ninlil_delivery_binding current = {0};
    int rc;
    if (!r || !peer || peer == UINT16_MAX ||
        !ninlil_delivery_binding_valid(expected))
        return NINLIL_ERR_INVALID;
    if (r->fatal_error)
        return r->fatal_error;
    if (!r->storage_bound || !r->config.binding_lookup ||
        memcmp(r->storage_identity, expected->source_identity, 32u))
        return NINLIL_ERR_UNAUTHORIZED;
    rc = r->config.binding_lookup(r->config.binding_ctx, peer, &current);
    if (rc != NINLIL_OK)
        return rc < 0 ? rc : NINLIL_ERR_STATE;
    return ninlil_delivery_binding_valid(&current) &&
                   ninlil_delivery_binding_equal(&current, expected)
               ? NINLIL_OK
               : NINLIL_ERR_UNAUTHORIZED;
}
int ninlil_binding_check_outbound(ninlil_runtime *r,
                                  const ninlil_outbound_entry *entry)
{
    ninlil_delivery_binding b;
    int rc;
    if (!entry->binding_offset)
        return NINLIL_OK;
    rc = ninlil_binding_read(r, &entry->message_id, entry->binding_offset,
                             entry->record_ref.generation, &b);
    if (rc == NINLIL_OK)
        rc = ninlil_binding_current(r, entry->target, &b);
    /* This is an existing contract, not another service-slot admission. */
    if (rc == NINLIL_OK)
        rc = ninlil_authorize(r, entry->target, entry->service,
                              entry->payload_len, entry->traffic_class,
                              NINLIL_SERVICE_RECEIVE, 0u);
    return rc;
}
int ninlil_binding_match(ninlil_runtime *r, const ninlil_id *id,
                         uint64_t offset, uint32_t generation,
                         const ninlil_delivery_binding *expected)
{
    ninlil_delivery_binding saved;
    int rc;
    if (!offset)
        return expected ? NINLIL_ERR_CONFLICT : NINLIL_OK;
    rc = ninlil_binding_read(r, id, offset, generation, &saved);
    if (rc != NINLIL_OK)
        return rc;
    return ninlil_delivery_binding_equal(&saved, expected)
               ? NINLIL_OK
               : NINLIL_ERR_CONFLICT;
}
int ninlil_query_binding(ninlil_runtime *r, const ninlil_id *id,
                         ninlil_delivery_binding *out)
{
    ninlil_outbound_entry *entry;
    ninlil_archive_entry *archive;
    if (!r || !id || !out)
        return NINLIL_ERR_INVALID;
    if (r->fatal_error)
        return r->fatal_error;
    entry = ninlil_find_outbound(r, id);
    if (entry)
        return ninlil_binding_read(r, id, entry->binding_offset,
                                   entry->record_ref.generation, out);
    archive = ninlil_find_archive_id(r, id);
    if (archive && archive->kind == NINLIL_ARCHIVE_OUTBOUND)
        return ninlil_binding_read(r, id, archive->binding_offset,
                                   archive->record_ref.generation, out);
    return NINLIL_ERR_NOT_FOUND;
}
int ninlil_binding_replay(ninlil_runtime *r, const uint8_t *p, uint16_t length,
                          const ninlil_journal_ref *ref)
{
    ninlil_delivery_binding b;
    ninlil_id id;
    if (ninlil_binding_decode(p, length, &id, &b) != NINLIL_OK ||
        !r->storage_bound || !ref->offset ||
        memcmp(r->storage_identity, b.source_identity, 32u) ||
        ninlil_id_in_use(r, &id))
        return NINLIL_ERR_CORRUPT;
    r->pending_binding = *ref;
    r->pending_binding_id = id;
    return NINLIL_OK;
}
int ninlil_binding_replay_release(ninlil_runtime *r, const uint8_t *p,
                                  uint16_t length)
{
    ninlil_archive_entry *entry;
    ninlil_id id;
    if (length != 17u || p[0] != NINLIL_JRN_RECORD_VERSION)
        return NINLIL_ERR_CORRUPT;
    memcpy(id.bytes, p + 1, 16u);
    entry = ninlil_find_archive_id(r, &id);
    if (!entry || entry->kind != NINLIL_ARCHIVE_OUTBOUND ||
        !entry->binding_offset || entry->outcome == NINLIL_OUTCOME_ACTIVE)
        return NINLIL_ERR_CORRUPT;
    entry->binding_released = 1u;
    return NINLIL_OK;
}
int ninlil_release_bound(ninlil_runtime *r, const ninlil_id *id)
{
    ninlil_archive_entry *entry;
    ninlil_delivery_binding b;
    int rc;
    if (!r || !id)
        return NINLIL_ERR_INVALID;
    if (r->fatal_error)
        return r->fatal_error;
    entry = ninlil_find_archive_id(r, id);
    if (!entry || entry->kind != NINLIL_ARCHIVE_OUTBOUND ||
        !entry->binding_offset)
        return NINLIL_ERR_STATE;
    rc = ninlil_binding_read(r, id, entry->binding_offset,
                             entry->record_ref.generation, &b);
    if (rc != NINLIL_OK || entry->binding_released)
        return rc;
    rc = ninlil_log_id(r, NINLIL_JRN_BOUND_RELEASE, id);
    if (rc == NINLIL_OK)
        entry->binding_released = 1u;
    return rc;
}
int ninlil_submit_bound(ninlil_runtime *r, const ninlil_submission *submission,
                        const ninlil_delivery_binding *binding, ninlil_id *id)
{
    if (!ninlil_delivery_binding_valid(binding) || !submission ||
        submission->ownership != NINLIL_OWNERSHIP_DURABLE)
        return NINLIL_ERR_INVALID;
    return ninlil_submit_checked(r, submission, binding, id);
}
