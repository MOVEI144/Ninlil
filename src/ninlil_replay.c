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
            evidence <= entry->latest_evidence)
            return NINLIL_ERR_CORRUPT;
        entry->latest_evidence = evidence;
        if (ninlil_evidence_satisfies(entry->required_evidence, evidence))
            ninlil_archive_outbound(runtime, entry, NINLIL_OUTCOME_SATISFIED);
        return NINLIL_OK;
    }
    if (type == NINLIL_JRN_OUT_TERMINAL) {
        ninlil_outcome outcome = (ninlil_outcome)payload[17];

        if (outcome < NINLIL_OUTCOME_EXPIRED ||
            outcome > NINLIL_OUTCOME_UNKNOWN ||
            (outcome == NINLIL_OUTCOME_CANCELLED && entry->attempted) ||
            (outcome == NINLIL_OUTCOME_FAILED && !entry->attempted) ||
            (outcome == NINLIL_OUTCOME_UNKNOWN && !entry->attempted))
            return NINLIL_ERR_CORRUPT;
        ninlil_archive_outbound(runtime, entry, outcome);
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
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
    if (type == NINLIL_JRN_OUT_ATTEMPT || type == NINLIL_JRN_OUT_EVIDENCE ||
        type == NINLIL_JRN_OUT_TERMINAL)
        return replay_out_update(runtime, type, payload, length);
    if (type == NINLIL_JRN_IN_APPLICATION_ACCEPT) {
        ninlil_id id;
        ninlil_inbound_entry *entry;
        uint8_t need_receipt;

        if (length != 1u + NINLIL_ID_BYTES ||
            payload[0] != NINLIL_JRN_RECORD_VERSION)
            return NINLIL_ERR_CORRUPT;
        memcpy(id.bytes, payload + 1, NINLIL_ID_BYTES);
        entry = ninlil_find_inbound(runtime, &id);
        if (!entry)
            return NINLIL_ERR_CORRUPT;
        need_receipt = (uint8_t)(entry->required_evidence ==
                                 NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
        ninlil_archive_inbound(
            runtime, entry, NINLIL_EVIDENCE_APPLICATION_ACCEPTED, need_receipt);
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}
