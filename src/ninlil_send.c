#include "ninlil_internal.h"

#include <string.h>

static const ninlil_traffic_class schedule[NINLIL_SCHEDULE_SLOTS] = {
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL,
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL,
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CONTROL,
    NINLIL_TRAFFIC_CONTROL,  NINLIL_TRAFFIC_CONTROL,  NINLIL_TRAFFIC_CONTROL,
    NINLIL_TRAFFIC_NORMAL,   NINLIL_TRAFFIC_NORMAL,   NINLIL_TRAFFIC_NORMAL,
    NINLIL_TRAFFIC_BULK,
};

static int retry_ready(const ninlil_runtime *runtime,
                       const ninlil_outbound_entry *entry)
{
    return entry->last_sent_step == 0u ||
           (runtime->step_count >= entry->last_sent_step &&
            runtime->step_count - entry->last_sent_step >=
                runtime->config.retry_interval_steps);
}

static int deadline_sendable(ninlil_runtime *runtime,
                             const ninlil_outbound_entry *entry)
{
    int passed;

    if (entry->absolute_deadline_ms == 0u)
        return 1;
    if (ninlil_deadline_passed(runtime, entry->absolute_deadline_ms, &passed) !=
        NINLIL_OK)
        return 0;
    return !passed;
}

static ninlil_outbound_entry *find_class_entry(ninlil_runtime *runtime,
                                               ninlil_traffic_class class)
{
    uint16_t scanned;
    uint16_t start;

    if (!ninlil_traffic_class_valid(class))
        return NULL;
    start = runtime->outbound_cursor[(unsigned int)class];
    for (scanned = 0u; scanned < runtime->outbound_capacity; scanned++) {
        uint16_t index =
            (uint16_t)((start + scanned) % runtime->outbound_capacity);
        ninlil_outbound_entry *entry = &runtime->outbound[index];

        if (!entry->used || entry->traffic_class != class ||
            !retry_ready(runtime, entry) || !deadline_sendable(runtime, entry))
            continue;
        runtime->outbound_cursor[(unsigned int)class] =
            (uint16_t)((index + 1u) % runtime->outbound_capacity);
        return entry;
    }
    return NULL;
}

static ninlil_outbound_entry *select_outbound(ninlil_runtime *runtime)
{
    uint8_t scanned;

    for (scanned = 0u; scanned < NINLIL_SCHEDULE_SLOTS; scanned++) {
        uint8_t slot = (uint8_t)((runtime->schedule_cursor + scanned) %
                                 NINLIL_SCHEDULE_SLOTS);
        ninlil_outbound_entry *entry =
            find_class_entry(runtime, schedule[slot]);

        if (!entry)
            continue;
        runtime->schedule_cursor =
            (uint8_t)((slot + 1u) % NINLIL_SCHEDULE_SLOTS);
        return entry;
    }
    return NULL;
}

int ninlil_expire_outbound(ninlil_runtime *runtime, int *worked)
{
    uint16_t index;

    *worked = 0;
    for (index = 0u; index < runtime->outbound_capacity; index++) {
        ninlil_outbound_entry *entry = &runtime->outbound[index];
        int passed;
        int rc;

        if (!entry->used || entry->attempted ||
            entry->absolute_deadline_ms == 0u)
            continue;
        rc = ninlil_deadline_passed(runtime, entry->absolute_deadline_ms,
                                    &passed);
        if (rc != NINLIL_OK)
            continue;
        if (!passed)
            continue;
        *worked = 1;
        return ninlil_finish_outbound(runtime, entry, NINLIL_OUTCOME_EXPIRED);
    }
    return NINLIL_OK;
}

static void entry_submission(const ninlil_outbound_entry *entry,
                             const uint8_t *payload,
                             ninlil_submission *submission)
{
    memset(submission, 0, sizeof(*submission));
    submission->struct_version = NINLIL_API_VERSION;
    submission->idempotency_key = entry->idempotency_key;
    submission->target = entry->target;
    submission->service = entry->service;
    submission->ownership = entry->ownership;
    submission->required_evidence = entry->required_evidence;
    submission->traffic_class = entry->traffic_class;
    submission->absolute_deadline_ms = entry->absolute_deadline_ms;
    submission->payload = payload;
    submission->payload_len = entry->payload_len;
}

int ninlil_deadline_check(ninlil_runtime *r, uint64_t deadline)
{
    int passed, rc;
    if (!r)
        return NINLIL_ERR_INVALID;
    if (r->fatal_error != NINLIL_OK)
        return r->fatal_error;
    rc = ninlil_deadline_passed(r, deadline, &passed);
    return rc != NINLIL_OK ? rc : passed ? NINLIL_ERR_EXPIRED : NINLIL_OK;
}

int ninlil_transmit_check(ninlil_runtime *r, const uint8_t *packet,
                          size_t length)
{
    ninlil_wire_data_view view;
    ninlil_outbound_entry *entry;
    ninlil_submission submission;
    uint8_t payload[NINLIL_MAX_PAYLOAD], expected[NINLIL_WIRE_DATA_MAX];
    size_t encoded;
    int rc;
    if (!r || ninlil_wire_decode_data(packet, length, &view) != NINLIL_OK ||
        view.source != r->config.node_id)
        return NINLIL_ERR_INVALID;
    if (r->fatal_error != NINLIL_OK)
        return r->fatal_error;
    entry = ninlil_find_outbound(r, &view.message_id);
    if (!entry || !entry->attempted)
        return NINLIL_ERR_STATE;
    /* A pause must stop DATA already staged in the Link as well as new retries.
     * Receipts and durable ownership are unaffected. */
    if (r->service_wait &&
        r->service_wait[(size_t)(entry - r->outbound)].state ==
            NINLIL_SERVICE_PAUSED)
        return NINLIL_ERR_BUSY;
    rc = ninlil_read_payload(r, &entry->record_ref, NINLIL_JRN_OUT_HEADER,
                             payload, entry->payload_len);
    if (rc != NINLIL_OK)
        return rc;
    entry_submission(entry, payload, &submission);
    encoded = ninlil_wire_encode_data(expected, r->config.node_id, &submission,
                                      &entry->message_id, payload);
    if (encoded != length || memcmp(packet, expected, length) != 0)
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_binding_check_outbound(r, entry);
    return rc == NINLIL_OK
               ? ninlil_deadline_check(r, entry->absolute_deadline_ms)
               : rc;
}

static int send_entry(ninlil_runtime *runtime, ninlil_outbound_entry *entry)
{
    ninlil_submission submission;
    uint8_t payload[NINLIL_MAX_PAYLOAD];
    uint8_t packet[NINLIL_WIRE_DATA_MAX];
    size_t length;
    int rc;

    rc = ninlil_read_payload(runtime, &entry->record_ref, NINLIL_JRN_OUT_HEADER,
                             payload, entry->payload_len);
    if (rc != NINLIL_OK)
        return rc;
    if (entry->absolute_deadline_ms != 0u) {
        int passed;

        rc = ninlil_deadline_passed(runtime, entry->absolute_deadline_ms,
                                    &passed);
        if (rc != NINLIL_OK)
            return NINLIL_OK;
        if (passed) {
            if (entry->attempted)
                return NINLIL_OK;
            return ninlil_finish_outbound(runtime, entry,
                                          NINLIL_OUTCOME_EXPIRED);
        }
    }
    rc = ninlil_binding_check_outbound(runtime, entry);
    if (rc != NINLIL_OK)
        return rc;
    entry_submission(entry, payload, &submission);
    length = ninlil_wire_encode_data(packet, runtime->config.node_id,
                                     &submission, &entry->message_id, payload);
    if (!entry->attempted) {
        rc = ninlil_log_id(runtime, NINLIL_JRN_OUT_ATTEMPT, &entry->message_id);
        if (rc != NINLIL_OK)
            return rc;
        entry->attempted = 1u;
    }
    rc = runtime->config.link.send(runtime->config.link.ctx, packet, length);
    if (rc == NINLIL_OK)
        entry->last_sent_step = runtime->step_count;
    return rc;
}

int ninlil_process_outbound(ninlil_runtime *runtime, int *worked)
{
    ninlil_outbound_entry *entry;
    int rc = ninlil_expire_outbound(runtime, worked);
    if (rc != NINLIL_OK || *worked)
        return rc;
    entry = runtime->service_wait ? ninlil_service_select(runtime)
                                  : select_outbound(runtime);
    if (!entry)
        return NINLIL_OK;
    *worked = 1;
    if (runtime->service_wait && entry->absolute_deadline_ms) {
        rc = ninlil_deadline_check(runtime, entry->absolute_deadline_ms);
        if (rc != NINLIL_OK) {
            ninlil_service_result(runtime, entry, rc);
            return NINLIL_OK; /* No invented outcome for an attempted deadline.
                               */
        }
    }
    rc = send_entry(runtime, entry);
    if (entry->used)
        ninlil_service_result(runtime, entry, rc);
    return rc;
}
