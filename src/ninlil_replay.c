#include "ninlil_internal.h"

#include <string.h>

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint64_t get_be64(const uint8_t *data)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; index++)
        value = (value << 8) | data[index];
    return value;
}

static int bytes_nonzero(const uint8_t *bytes)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= bytes[index];
    return combined != 0u;
}

static int contract_valid(uint8_t ownership, uint8_t required, uint8_t traffic,
                          uint8_t flags, uint64_t deadline)
{
    return ownership == NINLIL_OWNERSHIP_DURABLE &&
           (required == NINLIL_EVIDENCE_REMOTE_STORED ||
            required == NINLIL_EVIDENCE_APPLICATION_ACCEPTED) &&
           traffic <= NINLIL_TRAFFIC_BULK &&
           (deadline == 0u || required == NINLIL_EVIDENCE_REMOTE_STORED) &&
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

static ninlil_rejection_entry *free_rejection(ninlil_runtime *runtime)
{
    uint16_t index;

    for (index = 0u; index < runtime->rejection_capacity; index++) {
        if (!runtime->rejections[index].used)
            return &runtime->rejections[index];
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

    if (length < NINLIL_JRN_OUT_HEADER ||
        payload[0] != NINLIL_JRN_RECORD_VERSION || payload[5] != 0u)
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
    if (candidate.target == 0u || candidate.target == UINT16_MAX ||
        candidate.service < NINLIL_APPLICATION_SERVICE_MIN ||
        !bytes_nonzero(candidate.message_id.bytes) ||
        !bytes_nonzero(candidate.idempotency_key.bytes) ||
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

    if (length < NINLIL_JRN_IN_HEADER ||
        payload[0] != NINLIL_JRN_RECORD_VERSION || payload[5] != 0u)
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
    if (candidate.source == 0u || candidate.source == UINT16_MAX ||
        candidate.service < NINLIL_APPLICATION_SERVICE_MIN ||
        !bytes_nonzero(candidate.message_id.bytes) ||
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

static int replay_in_rejection(ninlil_runtime *runtime, const uint8_t *payload,
                               uint16_t length,
                               const ninlil_journal_ref *reference)
{
    ninlil_rejection_entry candidate;
    ninlil_rejection_entry *slot;
    uint64_t deadline;
    uint16_t payload_len;

    if (length < NINLIL_JRN_IN_HEADER ||
        payload[0] != NINLIL_JRN_RECORD_VERSION ||
        payload[5] != NINLIL_RECEIPT_PERMANENT_REJECTION)
        return NINLIL_ERR_CORRUPT;
    payload_len = get_be16(payload + 10);
    deadline = get_be64(payload + 12);
    if (payload_len > NINLIL_MAX_PAYLOAD ||
        length != (uint16_t)(NINLIL_JRN_IN_HEADER + payload_len) ||
        !contract_valid(payload[1], payload[2], payload[3], payload[4],
                        deadline))
        return NINLIL_ERR_CORRUPT;
    memset(&candidate, 0, sizeof(candidate));
    candidate.ownership = (ninlil_ownership)payload[1];
    candidate.required_evidence = (ninlil_evidence)payload[2];
    candidate.traffic_class = (ninlil_traffic_class)payload[3];
    candidate.status = payload[5];
    candidate.target = get_be16(payload + 6);
    candidate.service = get_be16(payload + 8);
    candidate.payload_len = payload_len;
    candidate.absolute_deadline_ms = deadline;
    memcpy(candidate.message_id.bytes, payload + 20, NINLIL_ID_BYTES);
    if (candidate.target == 0u || candidate.target == UINT16_MAX ||
        candidate.service < NINLIL_APPLICATION_SERVICE_MIN ||
        !bytes_nonzero(candidate.message_id.bytes) ||
        ninlil_id_in_use(runtime, &candidate.message_id))
        return NINLIL_ERR_CORRUPT;
    slot = free_rejection(runtime);
    if (!slot)
        return NINLIL_ERR_CORRUPT;
    candidate.record_ref = *reference;
    candidate.pending = 1u;
    candidate.durable = 1u;
    candidate.used = 1u;
    *slot = candidate;
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
    if (length != 4u + NINLIL_ID_BYTES)
        return NINLIL_ERR_CORRUPT;
    if (type == NINLIL_JRN_OUT_EVIDENCE) {
        ninlil_evidence evidence = (ninlil_evidence)payload[17];
        uint16_t archive_slot = get_be16(payload + 18);
        int satisfies;

        if (!entry->attempted ||
            (evidence != NINLIL_EVIDENCE_GATEWAY_CUSTODY &&
             evidence != NINLIL_EVIDENCE_REMOTE_STORED &&
             evidence != NINLIL_EVIDENCE_APPLICATION_ACCEPTED) ||
            evidence <= entry->latest_evidence)
            return NINLIL_ERR_CORRUPT;
        satisfies =
            ninlil_evidence_satisfies(entry->required_evidence, evidence);
        if ((satisfies && archive_slot >= runtime->archive_capacity) ||
            (!satisfies && archive_slot != NINLIL_ARCHIVE_SLOT_NONE))
            return NINLIL_ERR_CORRUPT;
        entry->latest_evidence = evidence;
        if (satisfies)
            return ninlil_archive_outbound(
                runtime, entry, NINLIL_OUTCOME_SATISFIED, archive_slot);
        return NINLIL_OK;
    }
    if (type == NINLIL_JRN_OUT_TERMINAL) {
        ninlil_outcome outcome = (ninlil_outcome)payload[17];
        uint16_t archive_slot = get_be16(payload + 18);

        if (outcome < NINLIL_OUTCOME_EXPIRED ||
            outcome > NINLIL_OUTCOME_UNKNOWN ||
            archive_slot >= runtime->archive_capacity ||
            (outcome == NINLIL_OUTCOME_CANCELLED && entry->attempted) ||
            (outcome == NINLIL_OUTCOME_EXPIRED &&
             entry->absolute_deadline_ms == 0u) ||
            (outcome == NINLIL_OUTCOME_FAILED && !entry->attempted) ||
            (outcome == NINLIL_OUTCOME_UNKNOWN && !entry->attempted))
            return NINLIL_ERR_CORRUPT;
        if ((outcome == NINLIL_OUTCOME_FAILED ||
             outcome == NINLIL_OUTCOME_EXPIRED) &&
            ninlil_evidence_satisfies(entry->required_evidence,
                                      entry->latest_evidence))
            return NINLIL_ERR_CORRUPT;
        return ninlil_archive_outbound(runtime, entry, outcome, archive_slot);
    }
    return NINLIL_ERR_CORRUPT;
}

static int replay_in_receipt_handoff(ninlil_runtime *runtime,
                                     const uint8_t *payload, uint16_t length)
{
    ninlil_id id;
    ninlil_inbound_entry *inbound;
    ninlil_archive_entry *archive;

    if (length != 1u + NINLIL_ID_BYTES ||
        payload[0] != NINLIL_JRN_RECORD_VERSION)
        return NINLIL_ERR_CORRUPT;
    memcpy(id.bytes, payload + 1, NINLIL_ID_BYTES);
    inbound = ninlil_find_inbound(runtime, &id);
    archive = ninlil_find_archive_id(runtime, &id);
    if (inbound) {
        if (!inbound->need_receipt || inbound->receipt_handoff_committed)
            return NINLIL_ERR_CORRUPT;
        inbound->need_receipt = 0u;
        inbound->receipt_handoff_committed = 1u;
        return NINLIL_OK;
    }
    if (!archive || archive->kind != NINLIL_ARCHIVE_INBOUND ||
        !archive->need_receipt || archive->receipt_handoff_committed)
        return NINLIL_ERR_CORRUPT;
    archive->need_receipt = 0u;
    archive->receipt_handoff_committed = 1u;
    return NINLIL_OK;
}

int ninlil_replay_record(void *ctx, uint8_t type, const uint8_t *payload,
                         uint16_t length, const ninlil_journal_ref *reference)
{
    ninlil_runtime *runtime = ctx;

    if (!payload || !reference || reference->length != length)
        return NINLIL_ERR_CORRUPT;
    if (type == NINLIL_JRN_OUT_CREATE)
        return replay_out_create(runtime, payload, length, reference);
    if (type == NINLIL_JRN_IN_ACCEPT)
        return replay_in_accept(runtime, payload, length, reference);
    if (type == NINLIL_JRN_IN_REJECTION)
        return replay_in_rejection(runtime, payload, length, reference);
    if (type == NINLIL_JRN_OUT_ATTEMPT || type == NINLIL_JRN_OUT_EVIDENCE ||
        type == NINLIL_JRN_OUT_TERMINAL)
        return replay_out_update(runtime, type, payload, length);
    if (type == NINLIL_JRN_IN_RECEIPT_HANDOFF)
        return replay_in_receipt_handoff(runtime, payload, length);
    if (type == NINLIL_JRN_IN_APPLICATION_ACCEPT) {
        ninlil_id id;
        ninlil_inbound_entry *entry;
        uint8_t need_receipt;
        uint16_t archive_slot;

        if (length != 3u + NINLIL_ID_BYTES ||
            payload[0] != NINLIL_JRN_RECORD_VERSION)
            return NINLIL_ERR_CORRUPT;
        memcpy(id.bytes, payload + 1, NINLIL_ID_BYTES);
        archive_slot = get_be16(payload + 17);
        entry = ninlil_find_inbound(runtime, &id);
        if (!entry || archive_slot >= runtime->archive_capacity)
            return NINLIL_ERR_CORRUPT;
        need_receipt = (uint8_t)(entry->need_receipt ||
                                 entry->required_evidence ==
                                     NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
        return ninlil_archive_inbound(runtime, entry,
                                      NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                                      need_receipt, archive_slot);
    }
    return NINLIL_ERR_CORRUPT;
}
