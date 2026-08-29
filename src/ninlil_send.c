#include "ninlil_internal.h"

#include <string.h>

static const ninlil_traffic_class schedule[NINLIL_SCHEDULE_SLOTS] = {
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL,
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL,
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL,
    NINLIL_TRAFFIC_CRITICAL, NINLIL_TRAFFIC_CRITICAL,
    NINLIL_TRAFFIC_CONTROL,  NINLIL_TRAFFIC_CONTROL,
    NINLIL_TRAFFIC_CONTROL,  NINLIL_TRAFFIC_CONTROL,
    NINLIL_TRAFFIC_NORMAL,   NINLIL_TRAFFIC_NORMAL,
    NINLIL_TRAFFIC_NORMAL,   NINLIL_TRAFFIC_BULK,
};

static int retry_ready(const ninlil_runtime *runtime,
                       const ninlil_outbound_entry *entry)
{
    return entry->last_sent_step == 0u ||
           (runtime->step_count >= entry->last_sent_step &&
            runtime->step_count - entry->last_sent_step >=
                runtime->config.retry_interval_steps);
}

static ninlil_outbound_entry *find_class_entry(ninlil_runtime *runtime,
                                                ninlil_traffic_class class)
{
    uint16_t scanned;
    uint16_t start = runtime->outbound_cursor[(unsigned int)class];

    for (scanned = 0u; scanned < runtime->outbound_capacity; scanned++) {
        uint16_t index =
            (uint16_t)((start + scanned) % runtime->outbound_capacity);
        ninlil_outbound_entry *entry = &runtime->outbound[index];

        if (!entry->used || entry->traffic_class != class ||
            !retry_ready(runtime, entry))
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
        uint8_t slot =
            (uint8_t)((runtime->schedule_cursor + scanned) %
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
            return rc;
        if (!passed)
            continue;
        *worked = 1;
        return ninlil_finish_outbound(runtime, entry,
                                      NINLIL_OUTCOME_EXPIRED);
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

int ninlil_process_outbound(ninlil_runtime *runtime, int *worked)
{
    ninlil_outbound_entry *entry;
    ninlil_submission submission;
    uint8_t payload[NINLIL_MAX_PAYLOAD];
    uint8_t packet[NINLIL_WIRE_DATA_MAX];
    size_t length;
    int rc;

    rc = ninlil_expire_outbound(runtime, worked);
    if (rc != NINLIL_OK || *worked)
        return rc;
    entry = select_outbound(runtime);
    if (!entry)
        return NINLIL_OK;
    *worked = 1;
    rc = ninlil_read_payload(runtime, &entry->record_ref,
                             NINLIL_JRN_OUT_HEADER, payload,
                             entry->payload_len);
    if (rc != NINLIL_OK)
        return rc;
    entry_submission(entry, payload, &submission);
    length = ninlil_wire_encode_data(packet, runtime->config.node_id,
                                     &submission, &entry->message_id, payload);
    if (!entry->attempted) {
        rc = ninlil_log_id(runtime, NINLIL_JRN_OUT_ATTEMPT,
                           &entry->message_id);
        if (rc != NINLIL_OK)
            return rc;
        entry->attempted = 1u;
    }
    rc = runtime->config.link.send(runtime->config.link.ctx, packet, length);
    if (rc == NINLIL_OK)
        entry->last_sent_step = runtime->step_count;
    return rc;
}
