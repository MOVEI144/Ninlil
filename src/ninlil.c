#include "ninlil.h"
#include "ninlil_journal.h"
#include "ninlil_wire.h"

#include <stdlib.h>
#include <string.h>

#define JRN_OUT_CREATE 1u
#define JRN_OUT_SATISFIED 2u
#define JRN_IN_ACCEPT 3u
#define JRN_IN_APPLIED 4u
#define OUT_CREATE_HEADER 38u
#define IN_ACCEPT_HEADER 22u
#define MESSAGE_ID_ATTEMPTS 16u
#define STEP_PHASES 3u

struct outbound_entry {
    ninlil_id message_id;
    ninlil_id idempotency_key;
    uint16_t target;
    uint16_t service;
    uint16_t payload_len;
    uint8_t payload[NINLIL_MAX_PAYLOAD];
    ninlil_outcome outcome;
    uint64_t last_sent_step;
};

struct inbound_entry {
    ninlil_id message_id;
    uint16_t source;
    uint16_t service;
    uint16_t payload_len;
    uint8_t payload[NINLIL_MAX_PAYLOAD];
    ninlil_progress progress;
    uint8_t handed;
    uint8_t need_receipt;
};

struct ninlil_runtime {
    ninlil_config config;
    ninlil_journal *journal;
    struct outbound_entry *outbound;
    struct inbound_entry *inbound;
    uint32_t outbound_count;
    uint32_t inbound_count;
    uint32_t outbound_cursor;
    uint32_t receipt_cursor;
    unsigned int phase_cursor;
    uint64_t step_count;
    int fatal_error;
};

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static int id_equal(const ninlil_id *a, const ninlil_id *b)
{
    return memcmp(a->bytes, b->bytes, NINLIL_ID_BYTES) == 0;
}

static struct outbound_entry *find_outbound(ninlil_runtime *runtime,
                                             const ninlil_id *id)
{
    uint32_t index;
    for (index = 0u; index < runtime->outbound_count; index++) {
        if (id_equal(&runtime->outbound[index].message_id, id))
            return &runtime->outbound[index];
    }
    return NULL;
}

static struct outbound_entry *find_idempotency(ninlil_runtime *runtime,
                                                const ninlil_id *id)
{
    uint32_t index;
    for (index = 0u; index < runtime->outbound_count; index++) {
        if (id_equal(&runtime->outbound[index].idempotency_key, id))
            return &runtime->outbound[index];
    }
    return NULL;
}

static struct inbound_entry *find_inbound(ninlil_runtime *runtime,
                                           const ninlil_id *id)
{
    uint32_t index;
    for (index = 0u; index < runtime->inbound_count; index++) {
        if (id_equal(&runtime->inbound[index].message_id, id))
            return &runtime->inbound[index];
    }
    return NULL;
}

static int id_in_use(ninlil_runtime *runtime, const ninlil_id *id)
{
    return find_outbound(runtime, id) != NULL || find_inbound(runtime, id) != NULL;
}

static int append_record(ninlil_runtime *runtime,
                         uint8_t type,
                         const uint8_t *payload,
                         uint16_t length)
{
    int rc;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    rc = ninlil_journal_append(runtime->journal, type, payload, length);
    if (rc != NINLIL_OK)
        runtime->fatal_error = rc;
    return rc;
}

static int replay_record(void *ctx,
                         uint8_t type,
                         const uint8_t *payload,
                         uint16_t length)
{
    ninlil_runtime *runtime = ctx;
    ninlil_id id;

    if (!payload && length > 0u)
        return NINLIL_ERR_CORRUPT;

    if (type == JRN_OUT_CREATE) {
        struct outbound_entry candidate;
        uint16_t payload_len;

        if (length < OUT_CREATE_HEADER)
            return NINLIL_ERR_CORRUPT;
        payload_len = get_be16(payload + 36);
        if (payload_len > NINLIL_MAX_PAYLOAD ||
            length != (uint16_t)(OUT_CREATE_HEADER + payload_len))
            return NINLIL_ERR_CORRUPT;
        if (runtime->outbound_count >= runtime->config.max_outbound)
            return NINLIL_ERR_CAPACITY;

        memset(&candidate, 0, sizeof(candidate));
        memcpy(candidate.message_id.bytes, payload, NINLIL_ID_BYTES);
        memcpy(candidate.idempotency_key.bytes, payload + 16,
               NINLIL_ID_BYTES);
        if (id_in_use(runtime, &candidate.message_id) ||
            find_idempotency(runtime, &candidate.idempotency_key) != NULL)
            return NINLIL_ERR_CORRUPT;
        candidate.target = get_be16(payload + 32);
        candidate.service = get_be16(payload + 34);
        candidate.payload_len = payload_len;
        if (payload_len > 0u) {
            memcpy(candidate.payload, payload + OUT_CREATE_HEADER,
                   payload_len);
        }
        candidate.outcome = NINLIL_OUTCOME_ACTIVE;
        runtime->outbound[runtime->outbound_count++] = candidate;
        return NINLIL_OK;
    }

    if (type == JRN_IN_ACCEPT) {
        struct inbound_entry candidate;
        uint16_t payload_len;

        if (length < IN_ACCEPT_HEADER)
            return NINLIL_ERR_CORRUPT;
        payload_len = get_be16(payload + 20);
        if (payload_len > NINLIL_MAX_PAYLOAD ||
            length != (uint16_t)(IN_ACCEPT_HEADER + payload_len))
            return NINLIL_ERR_CORRUPT;
        if (runtime->inbound_count >= runtime->config.max_inbound)
            return NINLIL_ERR_CAPACITY;

        memset(&candidate, 0, sizeof(candidate));
        memcpy(candidate.message_id.bytes, payload, NINLIL_ID_BYTES);
        if (id_in_use(runtime, &candidate.message_id))
            return NINLIL_ERR_CORRUPT;
        candidate.source = get_be16(payload + 16);
        candidate.service = get_be16(payload + 18);
        candidate.payload_len = payload_len;
        if (payload_len > 0u) {
            memcpy(candidate.payload, payload + IN_ACCEPT_HEADER,
                   payload_len);
        }
        candidate.progress = NINLIL_PROGRESS_STORED;
        runtime->inbound[runtime->inbound_count++] = candidate;
        return NINLIL_OK;
    }

    if (length != NINLIL_ID_BYTES)
        return NINLIL_ERR_CORRUPT;
    memcpy(id.bytes, payload, NINLIL_ID_BYTES);

    if (type == JRN_OUT_SATISFIED) {
        struct outbound_entry *entry = find_outbound(runtime, &id);
        if (!entry || entry->outcome != NINLIL_OUTCOME_ACTIVE)
            return NINLIL_ERR_CORRUPT;
        entry->outcome = NINLIL_OUTCOME_SATISFIED;
        return NINLIL_OK;
    }
    if (type == JRN_IN_APPLIED) {
        struct inbound_entry *entry = find_inbound(runtime, &id);
        if (!entry || entry->progress != NINLIL_PROGRESS_STORED)
            return NINLIL_ERR_CORRUPT;
        entry->progress = NINLIL_PROGRESS_APPLIED;
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}

static int log_outbound(ninlil_runtime *runtime,
                        const struct outbound_entry *entry)
{
    uint8_t record[OUT_CREATE_HEADER + NINLIL_MAX_PAYLOAD];
    memcpy(record, entry->message_id.bytes, NINLIL_ID_BYTES);
    memcpy(record + 16, entry->idempotency_key.bytes, NINLIL_ID_BYTES);
    put_be16(record + 32, entry->target);
    put_be16(record + 34, entry->service);
    put_be16(record + 36, entry->payload_len);
    if (entry->payload_len > 0u)
        memcpy(record + OUT_CREATE_HEADER, entry->payload, entry->payload_len);
    return append_record(runtime,
                         JRN_OUT_CREATE,
                         record,
                         (uint16_t)(OUT_CREATE_HEADER + entry->payload_len));
}

static int log_inbound(ninlil_runtime *runtime,
                       const struct inbound_entry *entry)
{
    uint8_t record[IN_ACCEPT_HEADER + NINLIL_MAX_PAYLOAD];
    memcpy(record, entry->message_id.bytes, NINLIL_ID_BYTES);
    put_be16(record + 16, entry->source);
    put_be16(record + 18, entry->service);
    put_be16(record + 20, entry->payload_len);
    if (entry->payload_len > 0u)
        memcpy(record + IN_ACCEPT_HEADER, entry->payload, entry->payload_len);
    return append_record(runtime,
                         JRN_IN_ACCEPT,
                         record,
                         (uint16_t)(IN_ACCEPT_HEADER + entry->payload_len));
}

static int handle_data(ninlil_runtime *runtime,
                       const uint8_t *packet,
                       size_t length)
{
    ninlil_wire_data_view view;
    struct inbound_entry *existing;
    struct inbound_entry candidate;
    int rc;

    if (ninlil_wire_decode_data(packet, length, &view) != NINLIL_OK ||
        view.target != runtime->config.node_id)
        return NINLIL_OK;
    existing = find_inbound(runtime, &view.message_id);
    if (existing) {
        if (existing->source != view.source || existing->service != view.service ||
            existing->payload_len != view.payload_length ||
            (view.payload_length > 0u &&
             memcmp(existing->payload, view.payload, view.payload_length) != 0))
            return NINLIL_ERR_CONFLICT;
        if (existing->progress == NINLIL_PROGRESS_APPLIED)
            existing->need_receipt = 1u;
        return NINLIL_OK;
    }
    if (find_outbound(runtime, &view.message_id) != NULL)
        return NINLIL_ERR_CONFLICT;
    if (runtime->inbound_count >= runtime->config.max_inbound)
        return NINLIL_ERR_CAPACITY;
    memset(&candidate, 0, sizeof(candidate));
    candidate.message_id = view.message_id;
    candidate.source = view.source;
    candidate.service = view.service;
    candidate.payload_len = view.payload_length;
    candidate.progress = NINLIL_PROGRESS_STORED;
    if (view.payload_length > 0u)
        memcpy(candidate.payload, view.payload, view.payload_length);
    rc = log_inbound(runtime, &candidate);
    if (rc != NINLIL_OK)
        return rc;
    runtime->inbound[runtime->inbound_count++] = candidate;
    return NINLIL_OK;
}

static int handle_receipt(ninlil_runtime *runtime,
                          const uint8_t *packet,
                          size_t length)
{
    ninlil_wire_receipt_view view;
    struct outbound_entry *entry;
    int rc;

    if (ninlil_wire_decode_receipt(packet, length, &view) != NINLIL_OK ||
        view.target != runtime->config.node_id ||
        view.progress != NINLIL_PROGRESS_APPLIED)
        return NINLIL_OK;
    entry = find_outbound(runtime, &view.message_id);
    if (!entry || entry->target != view.source ||
        entry->outcome != NINLIL_OUTCOME_ACTIVE)
        return NINLIL_OK;
    rc = append_record(runtime,
                       JRN_OUT_SATISFIED,
                       view.message_id.bytes,
                       NINLIL_ID_BYTES);
    if (rc == NINLIL_OK)
        entry->outcome = NINLIL_OUTCOME_SATISFIED;
    return rc;
}

static uint32_t circular(uint32_t start, uint32_t offset, uint32_t count)
{
    uint32_t result = start + offset;
    return result >= count ? result - count : result;
}

static int process_receive(ninlil_runtime *runtime, int *worked)
{
    uint8_t packet[NINLIL_WIRE_PACKET_MAX];
    size_t length = 0u;
    uint8_t type;
    int rc;

    *worked = 0;
    rc = runtime->config.link.recv(runtime->config.link.ctx,
                                   packet,
                                   sizeof(packet),
                                   &length);
    if (rc == 0)
        return NINLIL_OK;
    *worked = 1;
    if (rc < 0)
        return rc == NINLIL_ERR_INVALID ? NINLIL_OK : rc;
    if (rc != 1 || length > sizeof(packet))
        return NINLIL_ERR_INVALID;
    if (ninlil_wire_packet_type(packet, length, &type) != NINLIL_OK)
        return NINLIL_OK;
    if (type == NINLIL_WIRE_DATA)
        return handle_data(runtime, packet, length);
    if (type == NINLIL_WIRE_RECEIPT)
        return handle_receipt(runtime, packet, length);
    return NINLIL_OK;
}

static int process_receipt(ninlil_runtime *runtime, int *worked)
{
    uint32_t scanned;
    *worked = 0;
    for (scanned = 0u; scanned < runtime->inbound_count; scanned++) {
        uint32_t index = circular(runtime->receipt_cursor,
                                  scanned,
                                  runtime->inbound_count);
        struct inbound_entry *entry = &runtime->inbound[index];
        uint8_t packet[NINLIL_WIRE_RECEIPT_SIZE];
        size_t length;
        int rc;
        if (!entry->need_receipt)
            continue;
        runtime->receipt_cursor = (index + 1u) % runtime->inbound_count;
        *worked = 1;
        length = ninlil_wire_encode_receipt(packet,
                                            runtime->config.node_id,
                                            entry->source,
                                            &entry->message_id,
                                            NINLIL_PROGRESS_APPLIED);
        rc = runtime->config.link.send(runtime->config.link.ctx, packet, length);
        if (rc == NINLIL_OK) {
            entry->need_receipt = 0u;
            return NINLIL_OK;
        }
        return rc == NINLIL_ERR_CAPACITY ? NINLIL_OK : rc;
    }
    return NINLIL_OK;
}

static int process_outbound(ninlil_runtime *runtime, int *worked)
{
    uint32_t scanned;
    *worked = 0;
    for (scanned = 0u; scanned < runtime->outbound_count; scanned++) {
        uint32_t index = circular(runtime->outbound_cursor,
                                  scanned,
                                  runtime->outbound_count);
        struct outbound_entry *entry = &runtime->outbound[index];
        uint8_t packet[NINLIL_WIRE_DATA_MAX];
        size_t length;
        int rc;
        if (entry->outcome != NINLIL_OUTCOME_ACTIVE)
            continue;
        if (entry->last_sent_step != 0u &&
            runtime->step_count - entry->last_sent_step <
                runtime->config.retry_interval_steps)
            continue;
        runtime->outbound_cursor = (index + 1u) % runtime->outbound_count;
        *worked = 1;
        length = ninlil_wire_encode_data(packet,
                                         runtime->config.node_id,
                                         entry->target,
                                         entry->service,
                                         &entry->message_id,
                                         entry->payload,
                                         entry->payload_len);
        rc = runtime->config.link.send(runtime->config.link.ctx, packet, length);
        if (rc == NINLIL_OK) {
            entry->last_sent_step = runtime->step_count;
            return NINLIL_OK;
        }
        return rc == NINLIL_ERR_CAPACITY ? NINLIL_OK : rc;
    }
    return NINLIL_OK;
}

int ninlil_open(ninlil_runtime **out, const ninlil_config *config)
{
    const char *journal_location;
    ninlil_runtime *runtime;
    int rc;

    if (!out)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    if (!config)
        return NINLIL_ERR_INVALID;
    journal_location = config->journal_location ? config->journal_location
                                                : config->journal_path;
    if (!journal_location || journal_location[0] == '\0' ||
        (config->journal_location && config->journal_path &&
         strcmp(config->journal_location, config->journal_path) != 0) ||
        !config->link.send || !config->link.recv ||
        (config->link.max_packet_size != 0u &&
         config->link.max_packet_size < NINLIL_WIRE_RECEIPT_SIZE) ||
        !config->random.fill || config->max_outbound == 0u ||
        config->max_inbound == 0u || config->max_outbound > NINLIL_MAX_ENTRIES ||
        config->max_inbound > NINLIL_MAX_ENTRIES ||
        config->max_work_per_step > NINLIL_MAX_STEP_WORK)
        return NINLIL_ERR_INVALID;
    runtime = calloc(1u, sizeof(*runtime));
    if (!runtime)
        return NINLIL_ERR_IO;
    runtime->config = *config;
    runtime->config.journal_location = journal_location;
    if (runtime->config.link.max_packet_size == 0u)
        runtime->config.link.max_packet_size = NINLIL_WIRE_PACKET_MAX;
    if (runtime->config.retry_interval_steps == 0u)
        runtime->config.retry_interval_steps = 1u;
    if (runtime->config.max_work_per_step == 0u)
        runtime->config.max_work_per_step = 8u;
    runtime->outbound = calloc(config->max_outbound, sizeof(*runtime->outbound));
    runtime->inbound = calloc(config->max_inbound, sizeof(*runtime->inbound));
    if (!runtime->outbound || !runtime->inbound) {
        ninlil_close(runtime);
        return NINLIL_ERR_IO;
    }
    rc = ninlil_journal_open(&runtime->journal,
                             runtime->config.journal_location,
                             replay_record,
                             runtime);
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
    free(runtime);
}

int ninlil_submit(ninlil_runtime *runtime,
                  const ninlil_id *idempotency_key,
                  uint16_t target,
                  uint16_t service,
                  const uint8_t *payload,
                  uint16_t payload_len,
                  ninlil_id *message_id)
{
    struct outbound_entry *existing;
    struct outbound_entry candidate;
    unsigned int attempt;
    int rc;

    if (!runtime || !idempotency_key || !message_id ||
        payload_len > NINLIL_MAX_PAYLOAD || (payload_len > 0u && !payload))
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    if (ninlil_wire_data_size(payload_len) >
        runtime->config.link.max_packet_size)
        return NINLIL_ERR_TOO_LARGE;
    existing = find_idempotency(runtime, idempotency_key);
    if (existing) {
        if (existing->target != target || existing->service != service ||
            existing->payload_len != payload_len ||
            (payload_len > 0u &&
             memcmp(existing->payload, payload, payload_len) != 0))
            return NINLIL_ERR_CONFLICT;
        *message_id = existing->message_id;
        return NINLIL_OK;
    }
    if (runtime->outbound_count >= runtime->config.max_outbound)
        return NINLIL_ERR_CAPACITY;
    memset(&candidate, 0, sizeof(candidate));
    candidate.idempotency_key = *idempotency_key;
    candidate.target = target;
    candidate.service = service;
    candidate.payload_len = payload_len;
    candidate.outcome = NINLIL_OUTCOME_ACTIVE;
    if (payload_len > 0u)
        memcpy(candidate.payload, payload, payload_len);
    for (attempt = 0u; attempt < MESSAGE_ID_ATTEMPTS; attempt++) {
        if (runtime->config.random.fill(runtime->config.random.ctx,
                                        candidate.message_id.bytes,
                                        NINLIL_ID_BYTES) != 0)
            return NINLIL_ERR_IO;
        if (!id_in_use(runtime, &candidate.message_id))
            break;
    }
    if (attempt == MESSAGE_ID_ATTEMPTS)
        return NINLIL_ERR_CONFLICT;
    rc = log_outbound(runtime, &candidate);
    if (rc != NINLIL_OK)
        return rc;
    runtime->outbound[runtime->outbound_count++] = candidate;
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
    runtime->step_count++;
    while (work < runtime->config.max_work_per_step) {
        unsigned int offset;
        int progressed = 0;
        for (offset = 0u; offset < STEP_PHASES; offset++) {
            unsigned int phase = (runtime->phase_cursor + offset) % STEP_PHASES;
            int worked = 0;
            int rc = phase == 0u ? process_receive(runtime, &worked)
                     : phase == 1u ? process_receipt(runtime, &worked)
                                   : process_outbound(runtime, &worked);
            if (rc == NINLIL_ERR_CAPACITY || rc == NINLIL_ERR_CONFLICT)
                result = rc;
            else if (rc != NINLIL_OK)
                return rc;
            if (worked) {
                runtime->phase_cursor = (phase + 1u) % STEP_PHASES;
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
    uint32_t index;
    if (!runtime || !out)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    for (index = 0u; index < runtime->inbound_count; index++) {
        struct inbound_entry *entry = &runtime->inbound[index];
        if (entry->progress == NINLIL_PROGRESS_APPLIED || entry->handed)
            continue;
        memset(out, 0, sizeof(*out));
        out->message_id = entry->message_id;
        out->source = entry->source;
        out->service = entry->service;
        out->payload_len = entry->payload_len;
        if (entry->payload_len > 0u)
            memcpy(out->payload, entry->payload, entry->payload_len);
        entry->handed = 1u;
        return NINLIL_OK;
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_complete(ninlil_runtime *runtime,
                    const ninlil_id *message_id,
                    ninlil_progress progress)
{
    struct inbound_entry *entry;
    int rc;
    if (!runtime || !message_id || progress != NINLIL_PROGRESS_APPLIED)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    entry = find_inbound(runtime, message_id);
    if (!entry)
        return NINLIL_ERR_NOT_FOUND;
    if (entry->progress == NINLIL_PROGRESS_APPLIED)
        return NINLIL_OK;
    rc = append_record(runtime,
                       JRN_IN_APPLIED,
                       message_id->bytes,
                       NINLIL_ID_BYTES);
    if (rc != NINLIL_OK)
        return rc;
    entry->progress = NINLIL_PROGRESS_APPLIED;
    entry->need_receipt = 1u;
    return NINLIL_OK;
}

int ninlil_query(ninlil_runtime *runtime,
                 const ninlil_id *message_id,
                 ninlil_info *out)
{
    struct outbound_entry *outbound;
    struct inbound_entry *inbound;
    if (!runtime || !message_id || !out)
        return NINLIL_ERR_INVALID;
    if (runtime->fatal_error != NINLIL_OK)
        return runtime->fatal_error;
    outbound = find_outbound(runtime, message_id);
    if (outbound) {
        memset(out, 0, sizeof(*out));
        out->message_id = outbound->message_id;
        out->peer = outbound->target;
        out->service = outbound->service;
        out->payload_len = outbound->payload_len;
        out->outcome = outbound->outcome;
        out->progress = outbound->outcome == NINLIL_OUTCOME_SATISFIED
                            ? NINLIL_PROGRESS_APPLIED
                            : NINLIL_PROGRESS_NONE;
        return NINLIL_OK;
    }
    inbound = find_inbound(runtime, message_id);
    if (inbound) {
        memset(out, 0, sizeof(*out));
        out->message_id = inbound->message_id;
        out->peer = inbound->source;
        out->service = inbound->service;
        out->payload_len = inbound->payload_len;
        out->progress = inbound->progress;
        out->outcome = inbound->progress == NINLIL_PROGRESS_APPLIED
                           ? NINLIL_OUTCOME_SATISFIED
                           : NINLIL_OUTCOME_ACTIVE;
        return NINLIL_OK;
    }
    return NINLIL_ERR_NOT_FOUND;
}
