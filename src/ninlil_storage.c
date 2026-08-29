#include "ninlil_internal.h"

#include <string.h>

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static void put_be64(uint8_t *data, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; index++) {
        data[7u - index] = (uint8_t)value;
        value >>= 8;
    }
}

static uint64_t get_be64(const uint8_t *data)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; index++)
        value = (value << 8) | data[index];
    return value;
}

int ninlil_id_equal(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

int ninlil_evidence_satisfies(ninlil_evidence required,
                              ninlil_evidence actual)
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
    if (rc != NINLIL_OK)
        runtime->fatal_error = rc;
    return rc;
}

int ninlil_read_payload(ninlil_runtime *runtime,
                        const ninlil_journal_ref *reference,
                        uint16_t payload_offset, uint8_t *payload,
                        uint16_t payload_len)
{
    return ninlil_journal_read(runtime->journal, reference, payload_offset,
                               payload, payload_len);
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

ninlil_outbound_entry *
ninlil_find_idempotency(ninlil_runtime *runtime, const ninlil_id *key)
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

ninlil_archive_entry *
ninlil_find_archive_key(ninlil_runtime *runtime, const ninlil_id *key)
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

static ninlil_archive_entry *archive_slot(ninlil_runtime *runtime)
{
    uint16_t scanned;

    for (scanned = 0u; scanned < runtime->archive_capacity; scanned++) {
        uint16_t index = (uint16_t)((runtime->archive_replace_cursor + scanned) %
                                    runtime->archive_capacity);
        if (!runtime->archive[index].used) {
            runtime->archive_replace_cursor =
                (uint16_t)((index + 1u) % runtime->archive_capacity);
            return &runtime->archive[index];
        }
    }
    for (scanned = 0u; scanned < runtime->archive_capacity; scanned++) {
        uint16_t index = (uint16_t)((runtime->archive_replace_cursor + scanned) %
                                    runtime->archive_capacity);
        if (!runtime->archive[index].need_receipt) {
            runtime->archive_replace_cursor =
                (uint16_t)((index + 1u) % runtime->archive_capacity);
            return &runtime->archive[index];
        }
    }
    return &runtime->archive[runtime->archive_replace_cursor++ %
                             runtime->archive_capacity];
}

void ninlil_archive_outbound(ninlil_runtime *runtime,
                             ninlil_outbound_entry *entry,
                             ninlil_outcome outcome)
{
    ninlil_archive_entry *archive = archive_slot(runtime);
    ninlil_traffic_class traffic_class = entry->traffic_class;

    memset(archive, 0, sizeof(*archive));
    archive->used = 1u;
    archive->kind = NINLIL_ARCHIVE_OUTBOUND;
    archive->message_id = entry->message_id;
    archive->idempotency_key = entry->idempotency_key;
    archive->record_ref = entry->record_ref;
    archive->absolute_deadline_ms = entry->absolute_deadline_ms;
    archive->sequence = ++runtime->archive_sequence;
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
}

void ninlil_archive_inbound(ninlil_runtime *runtime,
                            ninlil_inbound_entry *entry,
                            ninlil_evidence evidence, uint8_t need_receipt)
{
    ninlil_archive_entry *archive = archive_slot(runtime);

    memset(archive, 0, sizeof(*archive));
    archive->used = 1u;
    archive->kind = NINLIL_ARCHIVE_INBOUND;
    archive->message_id = entry->message_id;
    archive->record_ref = entry->record_ref;
    archive->absolute_deadline_ms = entry->absolute_deadline_ms;
    archive->sequence = ++runtime->archive_sequence;
    archive->peer = entry->source;
    archive->service = entry->service;
    archive->payload_len = entry->payload_len;
    archive->ownership = entry->ownership;
    archive->required_evidence = entry->required_evidence;
    archive->latest_evidence = evidence;
    archive->traffic_class = entry->traffic_class;
    archive->outcome = NINLIL_OUTCOME_SATISFIED;
    archive->need_receipt = need_receipt;
    memset(entry, 0, sizeof(*entry));
    runtime->inbound_live--;
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
    missing_critical = runtime->live_by_class[NINLIL_TRAFFIC_CRITICAL] >=
                               profile->critical_reserve
                           ? 0u
                           : (uint16_t)(profile->critical_reserve -
                                        runtime->live_by_class
                                            [NINLIL_TRAFFIC_CRITICAL]);
    missing_control = runtime->live_by_class[NINLIL_TRAFFIC_CONTROL] >=
                              profile->control_reserve
                          ? 0u
                          : (uint16_t)(profile->control_reserve -
                                       runtime->live_by_class
                                           [NINLIL_TRAFFIC_CONTROL]);
    required_free = traffic_class == NINLIL_TRAFFIC_CRITICAL
                        ? 0u
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
                        const uint8_t *payload,
                        ninlil_journal_ref *reference)
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
                       const uint8_t *payload,
                       ninlil_journal_ref *reference)
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
                        ninlil_evidence evidence)
{
    uint8_t record[2u + NINLIL_ID_BYTES];

    record[0] = NINLIL_JRN_RECORD_VERSION;
    memcpy(record + 1, id->bytes, NINLIL_ID_BYTES);
    record[17] = (uint8_t)evidence;
    return ninlil_append_record(runtime, NINLIL_JRN_OUT_EVIDENCE, record,
                                sizeof(record), NULL);
}

int ninlil_log_terminal(ninlil_runtime *runtime, const ninlil_id *id,
                        ninlil_outcome outcome)
{
    uint8_t record[2u + NINLIL_ID_BYTES];

    record[0] = NINLIL_JRN_RECORD_VERSION;
    memcpy(record + 1, id->bytes, NINLIL_ID_BYTES);
    record[17] = (uint8_t)outcome;
    return ninlil_append_record(runtime, NINLIL_JRN_OUT_TERMINAL, record,
                                sizeof(record), NULL);
}

static int contract_valid(uint8_t ownership, uint8_t required, uint8_t traffic,
                          uint8_t flags, uint64_t deadline)
{
    return ownership == NINLIL_OWNERSHIP_DURABLE &&
           (required == NINLIL_EVIDENCE_REMOTE_STORED ||
            required == NINLIL_EVIDENCE_APPLICATION_ACCEPTED) &&
           traffic <= NINLIL_TRAFFIC_BULK &&
           (flags & (uint8_t)~NINLIL_JRN_DEADLINE_PRESENT) == 0u &&
           (((flags & NINLIL_JRN_DEADLINE_PRESENT) == 0u) == (deadline == 0u));
}

static ninlil_outbound_entry *free_outbound(ninlil_runtime *runtime)
{
    uint16_t index;

    for (index = 0u; index < runtime->outbound_capacity; index++) {
        if (!runtime->outbound[index].used)
            return &runtime->outbound[index];
    }
    return NULL;
}

static ninlil_inbound_entry *free_inbound(ninlil_runtime *runtime)
{
    uint16_t index;

    for (index = 0u; index < runtime->inbound_capacity; index++) {
        if (!runtime->inbound[index].used)
            return &runtime->inbound[index];
    }
    return NULL;
}

static int replay_out_create(ninlil_runtime *runtime, const uint8_t *payload,
                             uint16_t length,
                             const ninlil_journal_ref *reference)
{
    ninlil_outbound_entry candidate;
    ninlil_outbound_entry *slot;
    uint64_t deadline;
    uint16_t payload_len;

    if (length < NINLIL_JRN_OUT_HEADER || payload[0] != NINLIL_JRN_RECORD_VERSION ||
        payload[5] != 0u)
        return NINLIL_ERR_CORRUPT;
    payload_len = get_be16(payload + 10);
    deadline = get_be64(payload + 12);
    if (payload_len > NINLIL_MAX_PAYLOAD ||
        length != (uint16_t)(NINLIL_JRN_OUT_HEADER + payload_len) ||
        !contract_valid(payload[1], payload[2], payload[3], payload[4],
                        deadline))
        return NINLIL_ERR_CORRUPT;
    memset(&candidate, 0, sizeof(candidate));
    candidate.ownership = (ninlil_ownership)payload[1];
    candidate.required_evidence = (ninlil_evidence)payload[2];
    candidate.traffic_class = (ninlil_traffic_class)payload[3];
    candidate.target = get_be16(payload + 6);
    candidate.service = get_be16(payload + 8);
    candidate.payload_len = payload_len;
    candidate.absolute_deadline_ms = deadline;
    memcpy(candidate.message_id.bytes, payload + 20, NINLIL_ID_BYTES);
    memcpy(candidate.idempotency_key.bytes, payload + 36, NINLIL_ID_BYTES);
    if (candidate.service < NINLIL_APPLICATION_SERVICE_MIN ||
        ninlil_id_in_use(runtime, &candidate.message_id) ||
        ninlil_find_idempotency(runtime, &candidate.idempotency_key) ||
        ninlil_find_archive_key(runtime, &candidate.idempotency_key) ||
        ninlil_outbound_admission(runtime, candidate.traffic_class) !=
            NINLIL_OK ||
        !ninlil_total_owned_available(runtime))
        return NINLIL_ERR_CORRUPT;
    slot = free_outbound(runtime);
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    candidate.record_ref = *reference;
    candidate.used = 1u;
    *slot = candidate;
    runtime->outbound_live++;
    runtime->live_by_class[(unsigned int)candidate.traffic_class]++;
    if (candidate.traffic_class == NINLIL_TRAFFIC_BULK)
        runtime->bulk_live++;
    return NINLIL_OK;
}

static int replay_in_accept(ninlil_runtime *runtime, const uint8_t *payload,
                            uint16_t length,
                            const ninlil_journal_ref *reference)
{
    ninlil_inbound_entry candidate;
    ninlil_inbound_entry *slot;
    uint64_t deadline;
    uint16_t payload_len;

    if (length < NINLIL_JRN_IN_HEADER || payload[0] != NINLIL_JRN_RECORD_VERSION ||
        payload[5] != 0u)
        return NINLIL_ERR_CORRUPT;
    payload_len = get_be16(payload + 10);
    deadline = get_be64(payload + 12);
    if (payload_len > NINLIL_MAX_PAYLOAD ||
        length != (uint16_t)(NINLIL_JRN_IN_HEADER + payload_len) ||
        !contract_valid(payload[1], payload[2], payload[3], payload[4],
                        deadline) ||
        runtime->inbound_live >= runtime->config.profile.max_inbound ||
        !ninlil_total_owned_available(runtime))
        return NINLIL_ERR_CORRUPT;
    memset(&candidate, 0, sizeof(candidate));
    candidate.ownership = (ninlil_ownership)payload[1];
    candidate.required_evidence = (ninlil_evidence)payload[2];
    candidate.traffic_class = (ninlil_traffic_class)payload[3];
    candidate.source = get_be16(payload + 6);
    candidate.service = get_be16(payload + 8);
    candidate.payload_len = payload_len;
    candidate.absolute_deadline_ms = deadline;
    memcpy(candidate.message_id.bytes, payload + 20, NINLIL_ID_BYTES);
    if (candidate.service < NINLIL_APPLICATION_SERVICE_MIN ||
        ninlil_id_in_use(runtime, &candidate.message_id))
        return NINLIL_ERR_CORRUPT;
    slot = free_inbound(runtime);
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    candidate.record_ref = *reference;
    candidate.used = 1u;
    candidate.need_receipt = 1u;
    *slot = candidate;
    runtime->inbound_live++;
    return NINLIL_OK;
}

static int replay_out_update(ninlil_runtime *runtime, uint8_t type,
                             const uint8_t *payload, uint16_t length)
{
    ninlil_id id;
    ninlil_outbound_entry *entry;

    if (length < 1u + NINLIL_ID_BYTES ||
        payload[0] != NINLIL_JRN_RECORD_VERSION)
        return NINLIL_ERR_CORRUPT;
    memcpy(id.bytes, payload + 1, NINLIL_ID_BYTES);
    entry = ninlil_find_outbound(runtime, &id);
    if (!entry)
        return NINLIL_ERR_CORRUPT;
    if (type == NINLIL_JRN_OUT_ATTEMPT) {
        if (length != 1u + NINLIL_ID_BYTES || entry->attempted)
            return NINLIL_ERR_CORRUPT;
        entry->attempted = 1u;
        return NINLIL_OK;
    }
    if (length != 2u + NINLIL_ID_BYTES)
        return NINLIL_ERR_CORRUPT;
    if (type == NINLIL_JRN_OUT_EVIDENCE) {
        ninlil_evidence evidence = (ninlil_evidence)payload[17];

        if ((evidence != NINLIL_EVIDENCE_GATEWAY_CUSTODY &&
             evidence != NINLIL_EVIDENCE_REMOTE_STORED &&
             evidence != NINLIL_EVIDENCE_APPLICATION_ACCEPTED) ||
            evidence == entry->latest_evidence)
            return NINLIL_ERR_CORRUPT;
        entry->latest_evidence = evidence;
        if (ninlil_evidence_satisfies(entry->required_evidence, evidence))
            ninlil_archive_outbound(runtime, entry,
                                    NINLIL_OUTCOME_SATISFIED);
        return NINLIL_OK;
    }
    if (type == NINLIL_JRN_OUT_TERMINAL) {
        ninlil_outcome outcome = (ninlil_outcome)payload[17];

        if (outcome < NINLIL_OUTCOME_EXPIRED ||
            outcome > NINLIL_OUTCOME_UNKNOWN ||
            (outcome == NINLIL_OUTCOME_CANCELLED && entry->attempted) ||
            (outcome == NINLIL_OUTCOME_UNKNOWN && !entry->attempted))
            return NINLIL_ERR_CORRUPT;
        ninlil_archive_outbound(runtime, entry, outcome);
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}

int ninlil_replay_record(void *ctx, uint8_t type, const uint8_t *payload,
                         uint16_t length,
                         const ninlil_journal_ref *reference)
{
    ninlil_runtime *runtime = ctx;

    if (!payload || !reference || reference->length != length)
        return NINLIL_ERR_CORRUPT;
    if (type == NINLIL_JRN_OUT_CREATE)
        return replay_out_create(runtime, payload, length, reference);
    if (type == NINLIL_JRN_IN_ACCEPT)
        return replay_in_accept(runtime, payload, length, reference);
    if (type == NINLIL_JRN_OUT_ATTEMPT ||
        type == NINLIL_JRN_OUT_EVIDENCE ||
        type == NINLIL_JRN_OUT_TERMINAL)
        return replay_out_update(runtime, type, payload, length);
    if (type == NINLIL_JRN_IN_APPLICATION_ACCEPT) {
        ninlil_id id;
        ninlil_inbound_entry *entry;

        if (length != 1u + NINLIL_ID_BYTES ||
            payload[0] != NINLIL_JRN_RECORD_VERSION)
            return NINLIL_ERR_CORRUPT;
        memcpy(id.bytes, payload + 1, NINLIL_ID_BYTES);
        entry = ninlil_find_inbound(runtime, &id);
        if (!entry)
            return NINLIL_ERR_CORRUPT;
        ninlil_archive_inbound(
            runtime, entry, NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
            entry->required_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}
