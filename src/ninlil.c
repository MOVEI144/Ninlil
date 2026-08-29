#include "ninlil_internal.h"

#include <stdlib.h>
#include <string.h>

#define MESSAGE_ID_ATTEMPTS 16u

static int location_valid(const char *location)
{
    size_t length;

    if (!location)
        return 0;
    for (length = 0u; length <= NINLIL_JOURNAL_LOCATION_MAX; length++) {
        if (location[length] == '\0')
            return length > 0u;
    }
    return 0;
}

static int config_valid(const ninlil_config *config)
{
    return config && location_valid(config->journal_location) &&
           config->node_id > 0u && config->node_id < UINT16_MAX &&
           config->link.send && config->link.recv && config->random.fill &&
           config->max_work_per_step <= NINLIL_MAX_STEP_WORK &&
           config->retry_interval_steps <= NINLIL_MAX_RETRY_INTERVAL_STEPS &&
           ninlil_role_profile_validate(&config->profile) == NINLIL_OK;
}

static size_t runtime_ram_bytes(const ninlil_runtime *runtime)
{
    return sizeof(*runtime) +
           (size_t)runtime->outbound_capacity * sizeof(*runtime->outbound) +
           (size_t)runtime->inbound_capacity * sizeof(*runtime->inbound) +
           (size_t)runtime->archive_capacity * sizeof(*runtime->archive) +
           (size_t)runtime->rejection_capacity * sizeof(*runtime->rejections);
}

void ninlil_submission_defaults(ninlil_submission *submission)
{
    if (!submission)
        return;
    memset(submission, 0, sizeof(*submission));
    submission->struct_version = NINLIL_API_VERSION;
    submission->ownership = NINLIL_OWNERSHIP_DURABLE;
    submission->required_evidence = NINLIL_EVIDENCE_REMOTE_STORED;
    submission->traffic_class = NINLIL_TRAFFIC_NORMAL;
}

int ninlil_open(ninlil_runtime **out, const ninlil_config *config)
{
    ninlil_runtime *runtime;
    int rc;

    if (!out)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    if (!config_valid(config))
        return NINLIL_ERR_INVALID;
    if (config->profile.role == NINLIL_ROLE_POWERED_RELAY_CANDIDATE)
        return NINLIL_ERR_STATE;
    if (config->link.max_packet_size != 0u &&
        config->link.max_packet_size < NINLIL_WIRE_RECEIPT_SIZE)
        return NINLIL_ERR_INVALID;
    runtime = calloc(1u, sizeof(*runtime));
    if (!runtime)
        return NINLIL_ERR_IO;
    runtime->config = *config;
    if (runtime->config.link.max_packet_size == 0u)
        runtime->config.link.max_packet_size = NINLIL_WIRE_PACKET_MAX;
    if (runtime->config.retry_interval_steps == 0u)
        runtime->config.retry_interval_steps = 1u;
    if (runtime->config.max_work_per_step == 0u)
        runtime->config.max_work_per_step = 8u;
    runtime->outbound_capacity = config->profile.max_outbound;
    runtime->inbound_capacity = config->profile.max_inbound;
    runtime->archive_capacity = config->profile.dedupe_ids;
    runtime->rejection_capacity = config->profile.max_inbound;
    if (runtime_ram_bytes(runtime) > config->profile.dram_ceiling_bytes) {
        free(runtime);
        return NINLIL_ERR_CAPACITY;
    }
    runtime->outbound =
        calloc(runtime->outbound_capacity, sizeof(*runtime->outbound));
    runtime->inbound =
        calloc(runtime->inbound_capacity, sizeof(*runtime->inbound));
    runtime->archive =
        calloc(runtime->archive_capacity, sizeof(*runtime->archive));
    runtime->rejections =
        calloc(runtime->rejection_capacity, sizeof(*runtime->rejections));
    if (!runtime->outbound || !runtime->inbound || !runtime->archive ||
        !runtime->rejections) {
        ninlil_close(runtime);
        return NINLIL_ERR_IO;
    }
    rc =
        ninlil_journal_open(&runtime->journal, runtime->config.journal_location,
                            runtime->config.profile.flash_ceiling_bytes,
                            ninlil_replay_record, runtime);
    if (rc != NINLIL_OK) {
        ninlil_close(runtime);
        return rc;
    }
    *out = runtime;
    return NINLIL_OK;
}

void ninlil_close(ninlil_runtime *runtime)
{
    if (!runtime)
        return;
    ninlil_journal_close(runtime->journal);
    free(runtime->outbound);
    free(runtime->inbound);
    free(runtime->archive);
    free(runtime->rejections);
    free(runtime);
}

static int id_bytes_nonzero(const uint8_t *bytes)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= bytes[index];
    return combined != 0u;
}

static int submission_valid(const ninlil_submission *submission)
{
    return submission && submission->struct_version == NINLIL_API_VERSION &&
           id_bytes_nonzero(submission->idempotency_key.bytes) &&
           submission->target > 0u && submission->target < UINT16_MAX &&
           submission->service >= NINLIL_APPLICATION_SERVICE_MIN &&
           submission->ownership == NINLIL_OWNERSHIP_DURABLE &&
           (submission->required_evidence == NINLIL_EVIDENCE_REMOTE_STORED ||
            submission->required_evidence ==
                NINLIL_EVIDENCE_APPLICATION_ACCEPTED) &&
           submission->traffic_class <= NINLIL_TRAFFIC_BULK &&
           submission->payload_len <= NINLIL_MAX_PAYLOAD &&
           (submission->payload_len == 0u || submission->payload);
}

static int outbound_matches(ninlil_runtime *runtime,
                            const ninlil_outbound_entry *entry,
                            const ninlil_submission *submission, int *matches)
{
    uint8_t payload[NINLIL_MAX_PAYLOAD];
    int rc;

    *matches = 0;
    if (entry->target != submission->target ||
        entry->service != submission->service ||
        entry->payload_len != submission->payload_len ||
        entry->ownership != submission->ownership ||
        entry->required_evidence != submission->required_evidence ||
        entry->traffic_class != submission->traffic_class ||
        entry->absolute_deadline_ms != submission->absolute_deadline_ms)
        return NINLIL_OK;
    rc = ninlil_read_payload(runtime, &entry->record_ref, NINLIL_JRN_OUT_HEADER,
                             payload, entry->payload_len);
    if (rc != NINLIL_OK)
        return rc;
    *matches = entry->payload_len == 0u ||
               memcmp(payload, submission->payload, entry->payload_len) == 0;
    return NINLIL_OK;
}

static int archive_matches(ninlil_runtime *runtime,
                           const ninlil_archive_entry *entry,
                           const ninlil_submission *submission, int *matches)
{
    uint8_t payload[NINLIL_MAX_PAYLOAD];
    int rc;

    *matches = 0;
    if (entry->kind != NINLIL_ARCHIVE_OUTBOUND ||
        entry->peer != submission->target ||
        entry->service != submission->service ||
        entry->payload_len != submission->payload_len ||
        entry->ownership != submission->ownership ||
        entry->required_evidence != submission->required_evidence ||
        entry->traffic_class != submission->traffic_class ||
        entry->absolute_deadline_ms != submission->absolute_deadline_ms)
        return NINLIL_OK;
    rc = ninlil_read_payload(runtime, &entry->record_ref, NINLIL_JRN_OUT_HEADER,
                             payload, entry->payload_len);
    if (rc != NINLIL_OK)
        return rc;
    *matches = entry->payload_len == 0u ||
               memcmp(payload, submission->payload, entry->payload_len) == 0;
    return NINLIL_OK;
}

static int deadline_admissible(ninlil_runtime *runtime, uint64_t deadline)
{
    uint64_t now;
    ninlil_time_quality quality;
    int rc;

    if (deadline == 0u)
        return NINLIL_OK;
    rc = ninlil_clock_now(runtime, &now, &quality);
    if (rc == NINLIL_ERR_NOT_FOUND)
        return NINLIL_ERR_STATE;
    if (rc != NINLIL_OK || quality != NINLIL_TIME_RESTART_SAFE)
        return rc == NINLIL_OK ? NINLIL_ERR_STATE : rc;
    return now >= deadline ? NINLIL_ERR_EXPIRED : NINLIL_OK;
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

int ninlil_submit(ninlil_runtime *runtime, const ninlil_submission *submission,
                  ninlil_id *message_id)
{
    ninlil_outbound_entry *existing;
    ninlil_archive_entry *archived;
    ninlil_outbound_entry candidate;
    ninlil_outbound_entry *slot;
    ninlil_journal_ref reference;
    unsigned int attempt;
    int matches;
    int rc;

    if (!runtime || !message_id || !submission_valid(submission))
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    if (ninlil_wire_data_size(submission->payload_len) >
        runtime->config.link.max_packet_size)
        return NINLIL_ERR_TOO_LARGE;
    existing = ninlil_find_idempotency(runtime, &submission->idempotency_key);
    if (existing) {
        rc = outbound_matches(runtime, existing, submission, &matches);
        if (rc != NINLIL_OK)
            return rc;
        if (!matches)
            return NINLIL_ERR_CONFLICT;
        *message_id = existing->message_id;
        return NINLIL_OK;
    }
    archived = ninlil_find_archive_key(runtime, &submission->idempotency_key);
    if (archived) {
        rc = archive_matches(runtime, archived, submission, &matches);
        if (rc != NINLIL_OK)
            return rc;
        if (!matches)
            return NINLIL_ERR_CONFLICT;
        *message_id = archived->message_id;
        return NINLIL_OK;
    }
    rc = deadline_admissible(runtime, submission->absolute_deadline_ms);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_authorize(runtime, submission->target, submission->service,
                          submission->payload_len, submission->traffic_class,
                          NINLIL_SERVICE_RECEIVE,
                          ninlil_live_service(runtime, submission->target,
                                              submission->service,
                                              NINLIL_SERVICE_RECEIVE));
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_outbound_admission(runtime, submission->traffic_class);
    if (rc != NINLIL_OK || !ninlil_total_owned_available(runtime))
        return NINLIL_ERR_CAPACITY;
    slot = free_outbound(runtime);
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    memset(&candidate, 0, sizeof(candidate));
    candidate.idempotency_key = submission->idempotency_key;
    candidate.target = submission->target;
    candidate.service = submission->service;
    candidate.payload_len = submission->payload_len;
    candidate.ownership = submission->ownership;
    candidate.required_evidence = submission->required_evidence;
    candidate.traffic_class = submission->traffic_class;
    candidate.absolute_deadline_ms = submission->absolute_deadline_ms;
    for (attempt = 0u; attempt < MESSAGE_ID_ATTEMPTS; attempt++) {
        if (runtime->config.random.fill(runtime->config.random.ctx,
                                        candidate.message_id.bytes,
                                        NINLIL_ID_BYTES) != 0)
            return NINLIL_ERR_IO;
        if (id_bytes_nonzero(candidate.message_id.bytes) &&
            !ninlil_id_in_use(runtime, &candidate.message_id))
            break;
    }
    if (attempt == MESSAGE_ID_ATTEMPTS)
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_log_outbound(runtime, &candidate, submission->payload,
                             &reference);
    if (rc != NINLIL_OK)
        return rc;
    candidate.record_ref = reference;
    candidate.used = 1u;
    *slot = candidate;
    runtime->outbound_live++;
    runtime->live_by_class[(unsigned int)candidate.traffic_class]++;
    if (candidate.traffic_class == NINLIL_TRAFFIC_BULK)
        runtime->bulk_live++;
    *message_id = candidate.message_id;
    return NINLIL_OK;
}

int ninlil_step(ninlil_runtime *runtime)
{
    uint32_t work = 0u;
    int result = NINLIL_OK;

    if (!runtime)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    if (runtime->step_count == UINT64_MAX)
        return NINLIL_ERR_FAULT;
    runtime->step_count++;
    while (work < runtime->config.max_work_per_step) {
        uint8_t offset;
        int progressed = 0;

        for (offset = 0u; offset < NINLIL_STEP_PHASES; offset++) {
            uint8_t phase = (uint8_t)((runtime->phase_cursor + offset) %
                                      NINLIL_STEP_PHASES);
            int worked = 0;
            int rc = phase == 0u ? ninlil_process_receive(runtime, &worked)
                     : phase == 1u
                         ? ninlil_process_receipt_send(runtime, &worked)
                         : ninlil_process_outbound(runtime, &worked);

            if (rc == NINLIL_ERR_CAPACITY || rc == NINLIL_ERR_CONFLICT ||
                rc == NINLIL_ERR_UNAUTHORIZED || rc == NINLIL_ERR_EXPIRED ||
                rc == NINLIL_ERR_BUSY)
                result = rc;
            else if (rc != NINLIL_OK)
                return rc;
            if (worked) {
                runtime->phase_cursor =
                    (uint8_t)((phase + 1u) % NINLIL_STEP_PHASES);
                work++;
                progressed = 1;
                break;
            }
        }
        if (!progressed)
            break;
    }
    return result;
}

int ninlil_receive(ninlil_runtime *runtime, ninlil_inbound *out)
{
    uint16_t index;

    if (!runtime || !out)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    for (index = 0u; index < runtime->inbound_capacity; index++) {
        ninlil_inbound_entry *entry = &runtime->inbound[index];
        int rc;

        if (!entry->used || entry->handed)
            continue;
        memset(out, 0, sizeof(*out));
        out->message_id = entry->message_id;
        out->source = entry->source;
        out->service = entry->service;
        out->ownership = entry->ownership;
        out->required_evidence = entry->required_evidence;
        out->traffic_class = entry->traffic_class;
        out->absolute_deadline_ms = entry->absolute_deadline_ms;
        out->payload_len = entry->payload_len;
        rc = ninlil_read_payload(runtime, &entry->record_ref,
                                 NINLIL_JRN_IN_HEADER, out->payload,
                                 entry->payload_len);
        if (rc != NINLIL_OK)
            return rc;
        entry->handed = 1u;
        return NINLIL_OK;
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_application_accept(ninlil_runtime *runtime,
                              const ninlil_id *message_id)
{
    ninlil_inbound_entry *entry;
    ninlil_archive_entry *archive;
    int rc;

    if (!runtime || !message_id)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    entry = ninlil_find_inbound(runtime, message_id);
    if (!entry) {
        archive = ninlil_find_archive_id(runtime, message_id);
        return archive && archive->kind == NINLIL_ARCHIVE_INBOUND
                   ? NINLIL_OK
                   : NINLIL_ERR_NOT_FOUND;
    }
    if (!entry->handed)
        return NINLIL_ERR_STATE;
    rc = ninlil_log_id(runtime, NINLIL_JRN_IN_APPLICATION_ACCEPT, message_id);
    if (rc != NINLIL_OK)
        return rc;
    ninlil_archive_inbound(runtime, entry, NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                           entry->required_evidence ==
                               NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
    return NINLIL_OK;
}

static void info_from_outbound(const ninlil_outbound_entry *entry,
                               ninlil_info *out)
{
    memset(out, 0, sizeof(*out));
    out->message_id = entry->message_id;
    out->peer = entry->target;
    out->service = entry->service;
    out->payload_len = entry->payload_len;
    out->ownership = entry->ownership;
    out->required_evidence = entry->required_evidence;
    out->latest_evidence = entry->latest_evidence;
    out->traffic_class = entry->traffic_class;
    out->outcome = NINLIL_OUTCOME_ACTIVE;
    out->absolute_deadline_ms = entry->absolute_deadline_ms;
    out->remote_boundary_may_have_been_reached = entry->attempted;
}

static void info_from_archive(const ninlil_archive_entry *entry,
                              ninlil_info *out)
{
    memset(out, 0, sizeof(*out));
    out->message_id = entry->message_id;
    out->peer = entry->peer;
    out->service = entry->service;
    out->payload_len = entry->payload_len;
    out->ownership = entry->ownership;
    out->required_evidence = entry->required_evidence;
    out->latest_evidence = entry->latest_evidence;
    out->traffic_class = entry->traffic_class;
    out->outcome = entry->outcome;
    out->absolute_deadline_ms = entry->absolute_deadline_ms;
    out->remote_boundary_may_have_been_reached = entry->attempted;
}

int ninlil_query(ninlil_runtime *runtime, const ninlil_id *message_id,
                 ninlil_info *out)
{
    ninlil_outbound_entry *outbound;
    ninlil_inbound_entry *inbound;
    ninlil_archive_entry *archive;

    if (!runtime || !message_id || !out)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    outbound = ninlil_find_outbound(runtime, message_id);
    if (outbound) {
        info_from_outbound(outbound, out);
        return NINLIL_OK;
    }
    archive = ninlil_find_archive_id(runtime, message_id);
    if (archive) {
        info_from_archive(archive, out);
        return NINLIL_OK;
    }
    inbound = ninlil_find_inbound(runtime, message_id);
    if (!inbound)
        return NINLIL_ERR_NOT_FOUND;
    memset(out, 0, sizeof(*out));
    out->message_id = inbound->message_id;
    out->peer = inbound->source;
    out->service = inbound->service;
    out->payload_len = inbound->payload_len;
    out->ownership = inbound->ownership;
    out->required_evidence = inbound->required_evidence;
    out->latest_evidence = NINLIL_EVIDENCE_REMOTE_STORED;
    out->traffic_class = inbound->traffic_class;
    out->outcome = NINLIL_OUTCOME_ACTIVE;
    out->absolute_deadline_ms = inbound->absolute_deadline_ms;
    return NINLIL_OK;
}

int ninlil_cancel(ninlil_runtime *runtime, const ninlil_id *message_id)
{
    ninlil_outbound_entry *entry;

    if (!runtime || !message_id)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    entry = ninlil_find_outbound(runtime, message_id);
    if (!entry)
        return NINLIL_ERR_NOT_FOUND;
    if (entry->attempted)
        return NINLIL_ERR_STATE;
    return ninlil_finish_outbound(runtime, entry, NINLIL_OUTCOME_CANCELLED);
}

int ninlil_mark_unknown(ninlil_runtime *runtime, const ninlil_id *message_id)
{
    ninlil_outbound_entry *entry;

    if (!runtime || !message_id)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    entry = ninlil_find_outbound(runtime, message_id);
    if (!entry)
        return NINLIL_ERR_NOT_FOUND;
    if (!entry->attempted)
        return NINLIL_ERR_STATE;
    return ninlil_finish_outbound(runtime, entry, NINLIL_OUTCOME_UNKNOWN);
}
