#include "ninlil_internal.h"

#include <string.h>

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void put_be64(uint8_t *data, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; index++) {
        data[7u - index] = (uint8_t)value;
        value >>= 8;
    }
}

int ninlil_id_equal(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

int ninlil_evidence_satisfies(ninlil_evidence required, ninlil_evidence actual)
{
    if (required == NINLIL_EVIDENCE_REMOTE_STORED) {
        return actual == NINLIL_EVIDENCE_REMOTE_STORED ||
               actual == NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    }
    return required == NINLIL_EVIDENCE_APPLICATION_ACCEPTED &&
           actual == NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
}

int ninlil_clock_now(ninlil_runtime *runtime, uint64_t *now,
                     ninlil_time_quality *quality)
{
    uint64_t candidate_now;
    ninlil_time_quality candidate_quality;
    int rc;

    if (!runtime || !now || !quality || !runtime->config.clock.now)
        return NINLIL_ERR_NOT_FOUND;
    rc = runtime->config.clock.now(runtime->config.clock.ctx, &candidate_now,
                                   &candidate_quality);
    if (rc != NINLIL_OK)
        return rc;
    if (candidate_quality < NINLIL_TIME_UNAVAILABLE ||
        candidate_quality > NINLIL_TIME_RESTART_SAFE)
        return NINLIL_ERR_INVALID;
    *now = candidate_now;
    *quality = candidate_quality;
    return NINLIL_OK;
}

int ninlil_deadline_passed(ninlil_runtime *runtime, uint64_t deadline,
                           int *passed)
{
    uint64_t now;
    ninlil_time_quality quality;
    int rc;

    if (!passed)
        return NINLIL_ERR_INVALID;
    *passed = 0;
    if (deadline == 0u)
        return NINLIL_OK;
    rc = ninlil_clock_now(runtime, &now, &quality);
    if (rc == NINLIL_ERR_NOT_FOUND)
        return NINLIL_OK;
    if (rc != NINLIL_OK)
        return rc;
    if (quality == NINLIL_TIME_RESTART_SAFE && now >= deadline)
        *passed = 1;
    return NINLIL_OK;
}

int ninlil_append_record(ninlil_runtime *runtime, uint8_t type,
                         const uint8_t *payload, uint16_t length,
                         ninlil_journal_ref *reference)
{
    int rc;

    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    rc = ninlil_journal_append(runtime->journal, type, payload, length,
                               reference);
    if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
        runtime->fatal_error = rc;
    return rc;
}

int ninlil_read_payload(ninlil_runtime *runtime,
                        const ninlil_journal_ref *reference,
                        uint16_t payload_offset, uint8_t *payload,
                        uint16_t payload_len)
{
    int rc = ninlil_journal_read(runtime->journal, reference, payload_offset,
                                 payload, payload_len);

    if (rc != NINLIL_OK)
        runtime->fatal_error = rc;
    return rc;
}

ninlil_outbound_entry *ninlil_find_outbound(ninlil_runtime *runtime,
                                            const ninlil_id *id)
{
    uint16_t index;

    for (index = 0u; index < runtime->outbound_capacity; index++) {
        if (runtime->outbound[index].used &&
            ninlil_id_equal(&runtime->outbound[index].message_id, id))
            return &runtime->outbound[index];
    }
    return NULL;
}

ninlil_outbound_entry *ninlil_find_idempotency(ninlil_runtime *runtime,
                                               const ninlil_id *key)
{
    uint16_t index;

    for (index = 0u; index < runtime->outbound_capacity; index++) {
        if (runtime->outbound[index].used &&
            ninlil_id_equal(&runtime->outbound[index].idempotency_key, key))
            return &runtime->outbound[index];
    }
    return NULL;
}

ninlil_inbound_entry *ninlil_find_inbound(ninlil_runtime *runtime,
                                          const ninlil_id *id)
{
    uint16_t index;

    for (index = 0u; index < runtime->inbound_capacity; index++) {
        if (runtime->inbound[index].used &&
            ninlil_id_equal(&runtime->inbound[index].message_id, id))
            return &runtime->inbound[index];
    }
    return NULL;
}

ninlil_archive_entry *ninlil_find_archive_id(ninlil_runtime *runtime,
                                             const ninlil_id *id)
{
    uint16_t index;

    for (index = 0u; index < runtime->archive_capacity; index++) {
        if (runtime->archive[index].used &&
            ninlil_id_equal(&runtime->archive[index].message_id, id))
            return &runtime->archive[index];
    }
    return NULL;
}

ninlil_archive_entry *ninlil_find_archive_key(ninlil_runtime *runtime,
                                              const ninlil_id *key)
{
    uint16_t index;

    for (index = 0u; index < runtime->archive_capacity; index++) {
        if (runtime->archive[index].used &&
            runtime->archive[index].kind == NINLIL_ARCHIVE_OUTBOUND &&
            ninlil_id_equal(&runtime->archive[index].idempotency_key, key))
            return &runtime->archive[index];
    }
    return NULL;
}

int ninlil_id_in_use(ninlil_runtime *runtime, const ninlil_id *id)
{
    return ninlil_find_outbound(runtime, id) != NULL ||
           ninlil_find_inbound(runtime, id) != NULL ||
           ninlil_find_archive_id(runtime, id) != NULL;
}

static int archive_replaceable(const ninlil_archive_entry *entry)
{
    return !entry->used || !entry->need_receipt;
}

int ninlil_archive_admission(const ninlil_runtime *runtime, uint16_t *slot)
{
    uint16_t scanned;

    if (!slot)
        return NINLIL_ERR_INVALID;
    for (scanned = 0u; scanned < runtime->archive_capacity; scanned++) {
        uint16_t index =
            (uint16_t)((runtime->archive_replace_cursor + scanned) %
                       runtime->archive_capacity);
        const ninlil_archive_entry *entry = &runtime->archive[index];

        if (archive_replaceable(entry)) {
            *slot = index;
            return NINLIL_OK;
        }
    }
    return NINLIL_ERR_CAPACITY;
}

static ninlil_archive_entry *archive_slot(ninlil_runtime *runtime,
                                          uint16_t index)
{
    return index < runtime->archive_capacity ? &runtime->archive[index] : NULL;
}

int ninlil_archive_outbound(ninlil_runtime *runtime,
                            ninlil_outbound_entry *entry,
                            ninlil_outcome outcome, uint16_t selected_slot)
{
    ninlil_archive_entry *archive = archive_slot(runtime, selected_slot);
    ninlil_traffic_class traffic_class = entry->traffic_class;

    if (!archive)
        return runtime->replaying ? NINLIL_ERR_CORRUPT : NINLIL_ERR_CAPACITY;
    if (!archive_replaceable(archive))
        return runtime->replaying ? NINLIL_ERR_CORRUPT : NINLIL_ERR_CAPACITY;
    runtime->archive_replace_cursor =
        (uint16_t)((selected_slot + 1u) % runtime->archive_capacity);
    memset(archive, 0, sizeof(*archive));
    archive->used = 1u;
    archive->kind = NINLIL_ARCHIVE_OUTBOUND;
    archive->message_id = entry->message_id;
    archive->idempotency_key = entry->idempotency_key;
    archive->record_ref = entry->record_ref;
    archive->absolute_deadline_ms = entry->absolute_deadline_ms;
    archive->peer = entry->target;
    archive->service = entry->service;
    archive->payload_len = entry->payload_len;
    archive->ownership = entry->ownership;
    archive->required_evidence = entry->required_evidence;
    archive->latest_evidence = entry->latest_evidence;
    archive->traffic_class = traffic_class;
    archive->outcome = outcome;
    archive->attempted = entry->attempted;
    memset(entry, 0, sizeof(*entry));
    runtime->outbound_live--;
    runtime->live_by_class[(unsigned int)traffic_class]--;
    if (traffic_class == NINLIL_TRAFFIC_BULK)
        runtime->bulk_live--;
    return NINLIL_OK;
}

int ninlil_archive_inbound(ninlil_runtime *runtime, ninlil_inbound_entry *entry,
                           ninlil_evidence evidence, uint8_t need_receipt,
                           uint16_t selected_slot)
{
    ninlil_archive_entry *archive = archive_slot(runtime, selected_slot);
    uint8_t receipt_handoff_committed = entry->receipt_handoff_committed;
    uint8_t receipt_handoff_commit_pending =
        entry->receipt_handoff_commit_pending;

    if (!archive)
        return runtime->replaying ? NINLIL_ERR_CORRUPT : NINLIL_ERR_CAPACITY;
    if (!archive_replaceable(archive))
        return runtime->replaying ? NINLIL_ERR_CORRUPT : NINLIL_ERR_CAPACITY;
    runtime->archive_replace_cursor =
        (uint16_t)((selected_slot + 1u) % runtime->archive_capacity);
    memset(archive, 0, sizeof(*archive));
    archive->used = 1u;
    archive->kind = NINLIL_ARCHIVE_INBOUND;
    archive->message_id = entry->message_id;
    archive->record_ref = entry->record_ref;
    archive->absolute_deadline_ms = entry->absolute_deadline_ms;
    archive->peer = entry->source;
    archive->service = entry->service;
    archive->payload_len = entry->payload_len;
    archive->ownership = entry->ownership;
    archive->required_evidence = entry->required_evidence;
    archive->latest_evidence = evidence;
    archive->traffic_class = entry->traffic_class;
    archive->outcome = NINLIL_OUTCOME_SATISFIED;
    archive->need_receipt = need_receipt;
    archive->receipt_handoff_committed =
        entry->required_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED
            ? 0u
            : receipt_handoff_committed;
    archive->receipt_handoff_commit_pending =
        entry->required_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED
            ? 0u
            : receipt_handoff_commit_pending;
    memset(entry, 0, sizeof(*entry));
    runtime->inbound_live--;
    return NINLIL_OK;
}

int ninlil_outbound_admission(const ninlil_runtime *runtime,
                              ninlil_traffic_class traffic_class)
{
    uint16_t missing_critical;
    uint16_t missing_control;
    uint32_t required_free;
    const ninlil_role_profile *profile = &runtime->config.profile;

    if (runtime->outbound_live >= profile->max_outbound)
        return NINLIL_ERR_CAPACITY;
    missing_critical =
        runtime->live_by_class[NINLIL_TRAFFIC_CRITICAL] >=
                profile->critical_reserve
            ? 0u
            : (uint16_t)(profile->critical_reserve -
                         runtime->live_by_class[NINLIL_TRAFFIC_CRITICAL]);
    missing_control =
        runtime->live_by_class[NINLIL_TRAFFIC_CONTROL] >=
                profile->control_reserve
            ? 0u
            : (uint16_t)(profile->control_reserve -
                         runtime->live_by_class[NINLIL_TRAFFIC_CONTROL]);
    required_free = traffic_class == NINLIL_TRAFFIC_CRITICAL ? 0u
                    : traffic_class == NINLIL_TRAFFIC_CONTROL
                        ? missing_critical
                        : (uint32_t)missing_critical + missing_control;
    if ((uint32_t)runtime->outbound_live + 1u + required_free >
        profile->max_outbound)
        return NINLIL_ERR_CAPACITY;
    if (traffic_class == NINLIL_TRAFFIC_BULK &&
        runtime->bulk_live >= profile->bulk_maximum)
        return NINLIL_ERR_CAPACITY;
    return NINLIL_OK;
}

int ninlil_total_owned_available(const ninlil_runtime *runtime)
{
    uint16_t limit = runtime->config.profile.max_total_owned;

    return limit == 0u ||
           (uint32_t)runtime->outbound_live + runtime->inbound_live < limit;
}

int ninlil_log_outbound(ninlil_runtime *runtime,
                        const ninlil_outbound_entry *entry,
                        const uint8_t *payload, ninlil_journal_ref *reference)
{
    uint8_t record[NINLIL_JRN_OUT_HEADER + NINLIL_MAX_PAYLOAD];

    memset(record, 0, NINLIL_JRN_OUT_HEADER);
    record[0] = NINLIL_JRN_RECORD_VERSION;
    record[1] = (uint8_t)entry->ownership;
    record[2] = (uint8_t)entry->required_evidence;
    record[3] = (uint8_t)entry->traffic_class;
    record[4] = (uint8_t)(entry->absolute_deadline_ms == 0u
                              ? 0u
                              : NINLIL_JRN_DEADLINE_PRESENT);
    put_be16(record + 6, entry->target);
    put_be16(record + 8, entry->service);
    put_be16(record + 10, entry->payload_len);
    put_be64(record + 12, entry->absolute_deadline_ms);
    memcpy(record + 20, entry->message_id.bytes, NINLIL_ID_BYTES);
    memcpy(record + 36, entry->idempotency_key.bytes, NINLIL_ID_BYTES);
    if (entry->payload_len > 0u)
        memcpy(record + NINLIL_JRN_OUT_HEADER, payload, entry->payload_len);
    return ninlil_append_record(
        runtime, NINLIL_JRN_OUT_CREATE, record,
        (uint16_t)(NINLIL_JRN_OUT_HEADER + entry->payload_len), reference);
}

int ninlil_log_inbound(ninlil_runtime *runtime,
                       const ninlil_inbound_entry *entry,
                       const uint8_t *payload, ninlil_journal_ref *reference)
{
    uint8_t record[NINLIL_JRN_IN_HEADER + NINLIL_MAX_PAYLOAD];

    memset(record, 0, NINLIL_JRN_IN_HEADER);
    record[0] = NINLIL_JRN_RECORD_VERSION;
    record[1] = (uint8_t)entry->ownership;
    record[2] = (uint8_t)entry->required_evidence;
    record[3] = (uint8_t)entry->traffic_class;
    record[4] = (uint8_t)(entry->absolute_deadline_ms == 0u
                              ? 0u
                              : NINLIL_JRN_DEADLINE_PRESENT);
    put_be16(record + 6, entry->source);
    put_be16(record + 8, entry->service);
    put_be16(record + 10, entry->payload_len);
    put_be64(record + 12, entry->absolute_deadline_ms);
    memcpy(record + 20, entry->message_id.bytes, NINLIL_ID_BYTES);
    if (entry->payload_len > 0u)
        memcpy(record + NINLIL_JRN_IN_HEADER, payload, entry->payload_len);
    return ninlil_append_record(
        runtime, NINLIL_JRN_IN_ACCEPT, record,
        (uint16_t)(NINLIL_JRN_IN_HEADER + entry->payload_len), reference);
}

int ninlil_log_id(ninlil_runtime *runtime, uint8_t type, const ninlil_id *id)
{
    uint8_t record[1u + NINLIL_ID_BYTES];

    record[0] = NINLIL_JRN_RECORD_VERSION;
    memcpy(record + 1, id->bytes, NINLIL_ID_BYTES);
    return ninlil_append_record(runtime, type, record, sizeof(record), NULL);
}

int ninlil_log_evidence(ninlil_runtime *runtime, const ninlil_id *id,
                        ninlil_evidence evidence, uint16_t archive_slot)
{
    uint8_t record[4u + NINLIL_ID_BYTES];

    record[0] = NINLIL_JRN_RECORD_VERSION;
    memcpy(record + 1, id->bytes, NINLIL_ID_BYTES);
    record[17] = (uint8_t)evidence;
    put_be16(record + 18, archive_slot);
    return ninlil_append_record(runtime, NINLIL_JRN_OUT_EVIDENCE, record,
                                sizeof(record), NULL);
}

int ninlil_log_terminal(ninlil_runtime *runtime, const ninlil_id *id,
                        ninlil_outcome outcome, uint16_t archive_slot)
{
    uint8_t record[4u + NINLIL_ID_BYTES];

    record[0] = NINLIL_JRN_RECORD_VERSION;
    memcpy(record + 1, id->bytes, NINLIL_ID_BYTES);
    record[17] = (uint8_t)outcome;
    put_be16(record + 18, archive_slot);
    return ninlil_append_record(runtime, NINLIL_JRN_OUT_TERMINAL, record,
                                sizeof(record), NULL);
}

int ninlil_log_application_accept(ninlil_runtime *runtime, const ninlil_id *id,
                                  uint16_t archive_slot)
{
    uint8_t record[3u + NINLIL_ID_BYTES];

    record[0] = NINLIL_JRN_RECORD_VERSION;
    memcpy(record + 1, id->bytes, NINLIL_ID_BYTES);
    put_be16(record + 17, archive_slot);
    return ninlil_append_record(runtime, NINLIL_JRN_IN_APPLICATION_ACCEPT,
                                record, sizeof(record), NULL);
}
