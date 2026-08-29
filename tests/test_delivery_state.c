#define _POSIX_C_SOURCE 200809L

#include "ninlil.h"
#include "ninlil_journal.h"
#include "ninlil_wire.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define APP_SERVICE UINT16_C(0x0100)
#define ARCHIVE_CAPACITY 32u
#define RECEIPT_CLASSES 3u
#define RECEIPT_FAIRNESS_ROUNDS 3u
#define SCHEDULER_PHASES 3u
#define REJECTION_INTERVAL_STEPS 4u
#define OLD_OUT_CREATE 1u
#define OLD_OUT_HEADER 52u
#define IN_RECORD_HEADER 36u
#define CURRENT_RECORD_VERSION 4u
#define DEADLINE_PRESENT 1u
#define OUT_ATTEMPT_RECORD 2u
#define OUT_EVIDENCE_RECORD 3u
#define OUT_TERMINAL_RECORD 4u
#define IN_APPLICATION_ACCEPT_RECORD 6u
#define IN_REJECTION_RECORD 8u

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct scripted_link {
    uint8_t incoming[NINLIL_WIRE_PACKET_MAX];
    uint8_t last_sent[NINLIL_WIRE_PACKET_MAX];
    size_t incoming_length;
    size_t last_sent_length;
    uint32_t send_calls;
    ninlil_id receipt_message_id[64];
    ninlil_id data_message_id[128];
    uint8_t receipt_status[64];
    uint8_t receipt_evidence[64];
    uint8_t receipt_count;
    uint8_t data_count;
    int send_result;
} scripted_link;

typedef struct controlled_policy {
    test_policy value;
    int result;
    uint8_t invalid;
} controlled_policy;

typedef struct fake_clock {
    uint64_t now_ms;
    ninlil_time_quality quality;
    int result;
} fake_clock;

static int scripted_send(void *ctx, const uint8_t *data, size_t length)
{
    scripted_link *link = ctx;

    if (!data || length > sizeof(link->last_sent))
        return NINLIL_ERR_TOO_LARGE;
    link->send_calls++;
    memcpy(link->last_sent, data, length);
    link->last_sent_length = length;
    if (length == NINLIL_WIRE_RECEIPT_SIZE && data[3] == NINLIL_WIRE_RECEIPT &&
        link->receipt_count < sizeof(link->receipt_status)) {
        uint8_t receipt_index = link->receipt_count;

        memcpy(link->receipt_message_id[receipt_index].bytes, data + 8,
               NINLIL_ID_BYTES);
        link->receipt_status[receipt_index] = data[24];
        link->receipt_evidence[receipt_index] = data[25];
        link->receipt_count++;
    } else if (length >= NINLIL_WIRE_DATA_HEADER &&
               data[3] == NINLIL_WIRE_DATA &&
               link->data_count < sizeof(link->data_message_id) /
                                      sizeof(link->data_message_id[0])) {
        memcpy(link->data_message_id[link->data_count].bytes, data + 10,
               NINLIL_ID_BYTES);
        link->data_count++;
    }
    return link->send_result;
}

static int scripted_recv(void *ctx, uint8_t *data, size_t capacity,
                         size_t *length)
{
    scripted_link *link = ctx;

    if (link->incoming_length == 0u)
        return 0;
    if (link->incoming_length > capacity)
        return NINLIL_ERR_TOO_LARGE;
    memcpy(data, link->incoming, link->incoming_length);
    *length = link->incoming_length;
    link->incoming_length = 0u;
    return 1;
}

static int accept_journal_record(void *ctx, uint8_t type,
                                 const uint8_t *payload, uint16_t length,
                                 const ninlil_journal_ref *reference)
{
    (void)ctx;
    (void)type;
    (void)payload;
    (void)length;
    (void)reference;
    return NINLIL_OK;
}

static int append_journal_record(const char *path, uint64_t maximum_bytes,
                                 uint8_t type, const uint8_t *record,
                                 uint16_t length)
{
    ninlil_journal *journal = NULL;
    int rc = ninlil_journal_open(&journal, path, maximum_bytes,
                                 accept_journal_record, NULL);

    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_journal_append(journal, type, record, length, NULL);
    ninlil_journal_close(journal);
    return rc;
}

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

static int clock_now(void *ctx, uint64_t *now_ms, ninlil_time_quality *quality)
{
    fake_clock *clock = ctx;

    if (clock->result != NINLIL_OK)
        return clock->result;
    *now_ms = clock->now_ms;
    *quality = clock->quality;
    return NINLIL_OK;
}

static int controlled_lookup(void *ctx, uint16_t peer,
                             ninlil_peer_policy *policy)
{
    controlled_policy *source = ctx;
    int rc;

    if (source->result != NINLIL_OK)
        return source->result;
    rc = test_policy_lookup(&source->value, peer, policy);
    if (rc == NINLIL_OK && source->invalid)
        policy->membership_epoch = 0u;
    return rc;
}

static int open_runtime_with_clock(ninlil_runtime **runtime, const char *path,
                                   scripted_link *transport,
                                   uint32_t *random_state,
                                   controlled_policy *policy,
                                   const ninlil_role_profile *profile,
                                   fake_clock *clock)
{
    ninlil_config config;

    memset(&config, 0, sizeof(config));
    config.journal_location = path;
    config.node_id = 1u;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = 1u;
    config.profile = *profile;
    config.link.send = scripted_send;
    config.link.recv = scripted_recv;
    config.link.ctx = transport;
    config.link.max_packet_size = NINLIL_WIRE_PACKET_MAX;
    config.random.fill = test_rng_fill;
    config.random.ctx = random_state;
    config.policy_lookup = controlled_lookup;
    config.policy_ctx = policy;
    if (clock) {
        config.clock.now = clock_now;
        config.clock.ctx = clock;
    }
    return ninlil_open(runtime, &config);
}

static int open_runtime(ninlil_runtime **runtime, const char *path,
                        scripted_link *transport, uint32_t *random_state,
                        controlled_policy *policy,
                        const ninlil_role_profile *profile)
{
    return open_runtime_with_clock(runtime, path, transport, random_state,
                                   policy, profile, NULL);
}

static int open_runtime_high_work(ninlil_runtime **runtime, const char *path,
                                  scripted_link *transport,
                                  uint32_t *random_state,
                                  controlled_policy *policy,
                                  const ninlil_role_profile *profile)
{
    ninlil_config config;

    memset(&config, 0, sizeof(config));
    config.journal_location = path;
    config.node_id = 1u;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = 16u;
    config.profile = *profile;
    config.link.send = scripted_send;
    config.link.recv = scripted_recv;
    config.link.ctx = transport;
    config.link.max_packet_size = NINLIL_WIRE_PACKET_MAX;
    config.random.fill = test_rng_fill;
    config.random.ctx = random_state;
    config.policy_lookup = controlled_lookup;
    config.policy_ctx = policy;
    return ninlil_open(runtime, &config);
}

static ninlil_submission
make_submission(ninlil_id key, ninlil_evidence required, const uint8_t *payload)
{
    ninlil_submission submission;

    ninlil_submission_defaults(&submission);
    submission.idempotency_key = key;
    submission.target = 2u;
    submission.service = APP_SERVICE;
    submission.required_evidence = required;
    submission.payload = payload;
    submission.payload_len = 1u;
    return submission;
}

static int inject_packet(scripted_link *link, const uint8_t *packet,
                         size_t length)
{
    if (link->incoming_length != 0u || length > sizeof(link->incoming))
        return NINLIL_ERR_CAPACITY;
    memcpy(link->incoming, packet, length);
    link->incoming_length = length;
    return NINLIL_OK;
}

static int inject_receipt(scripted_link *link, const ninlil_id *message_id,
                          uint8_t status, ninlil_evidence evidence)
{
    uint8_t packet[NINLIL_WIRE_RECEIPT_SIZE];
    size_t length = ninlil_wire_encode_receipt(packet, 2u, 1u, message_id,
                                               status, evidence);

    return inject_packet(link, packet, length);
}

static int inject_empty_data(scripted_link *link, const ninlil_id *message_id,
                             ninlil_evidence required)
{
    ninlil_id key;
    ninlil_submission submission;
    uint8_t packet[NINLIL_WIRE_DATA_HEADER];
    size_t length;

    test_fill_id(&key, UINT8_C(0xEF));
    submission = make_submission(key, required, NULL);
    submission.target = 1u;
    submission.payload_len = 0u;
    length = ninlil_wire_encode_data(packet, 2u, &submission, message_id, NULL);
    return inject_packet(link, packet, length);
}

static int inject_data(scripted_link *link, const ninlil_id *message_id,
                       ninlil_evidence required, uint8_t payload)
{
    ninlil_id key;
    ninlil_submission submission;
    uint8_t packet[NINLIL_WIRE_DATA_HEADER + 1u];
    size_t length;

    test_fill_id(&key, UINT8_C(0xF0));
    submission = make_submission(key, required, &payload);
    submission.target = 1u;
    length =
        ninlil_wire_encode_data(packet, 2u, &submission, message_id, &payload);
    return inject_packet(link, packet, length);
}

static int inject_deadline_data(scripted_link *link,
                                const ninlil_id *message_id,
                                ninlil_evidence required, uint8_t payload,
                                uint64_t deadline)
{
    ninlil_id key;
    ninlil_submission submission;
    uint8_t packet[NINLIL_WIRE_DATA_HEADER + 1u];
    size_t length;

    test_fill_id(&key, UINT8_C(0xF1));
    submission = make_submission(key, required, &payload);
    submission.target = 1u;
    submission.absolute_deadline_ms = deadline;
    length =
        ninlil_wire_encode_data(packet, 2u, &submission, message_id, &payload);
    return inject_packet(link, packet, length);
}

static int drain_incoming(ninlil_runtime *runtime, scripted_link *link)
{
    unsigned int attempt;

    for (attempt = 0u; attempt < 8u && link->incoming_length != 0u; attempt++) {
        int rc = ninlil_step(runtime);

        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY &&
            rc != NINLIL_ERR_BUSY)
            return rc;
    }
    return link->incoming_length == 0u ? NINLIL_OK : NINLIL_ERR_FAULT;
}

static int wait_for_attempt(ninlil_runtime *runtime,
                            const ninlil_id *message_id)
{
    unsigned int attempt;

    for (attempt = 0u; attempt < 16u; attempt++) {
        ninlil_info info;
        int rc = ninlil_query(runtime, message_id, &info);

        if (rc != NINLIL_OK)
            return rc;
        if (info.remote_boundary_may_have_been_reached)
            return NINLIL_OK;
        rc = ninlil_step(runtime);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
            return rc;
    }
    return NINLIL_ERR_FAULT;
}

static int wait_for_receipt(ninlil_runtime *runtime, scripted_link *link,
                            const ninlil_id *message_id, uint8_t status)
{
    uint8_t first = link->receipt_count;
    unsigned int attempt;

    for (attempt = 0u; attempt < 16u; attempt++) {
        uint8_t index;
        int rc = ninlil_step(runtime);

        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY &&
            rc != NINLIL_ERR_BUSY)
            return rc;
        for (index = first; index < link->receipt_count; index++) {
            if (memcmp(link->receipt_message_id[index].bytes, message_id->bytes,
                       NINLIL_ID_BYTES) == 0 &&
                link->receipt_status[index] == status)
                return NINLIL_OK;
        }
    }
    return NINLIL_ERR_FAULT;
}

static int journal_size(const char *path, off_t *size)
{
    struct stat status;

    if (stat(path, &status) != 0)
        return -1;
    *size = status.st_size;
    return 0;
}

static int flip_file_byte(const char *path, long offset)
{
    FILE *file = fopen(path, "r+b");
    int value;
    int result = -1;

    if (!file)
        return -1;
    if (fseek(file, offset, SEEK_SET) == 0) {
        value = fgetc(file);
        if (value != EOF && fseek(file, offset, SEEK_SET) == 0 &&
            fputc(value ^ 1, file) != EOF && fflush(file) == 0)
            result = 0;
    }
    if (fclose(file) != 0)
        result = -1;
    return result;
}

static int same_id(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

static uint8_t data_send_count(const scripted_link *link, const ninlil_id *id)
{
    uint8_t index;
    uint8_t count = 0u;

    for (index = 0u; index < link->data_count; index++) {
        if (same_id(&link->data_message_id[index], id))
            count++;
    }
    return count;
}

static void indexed_id(ninlil_id *id, uint32_t value)
{
    memset(id->bytes, 0, NINLIL_ID_BYTES);
    id->bytes[0] = UINT8_C(0xA5);
    id->bytes[12] = (uint8_t)(value >> 24);
    id->bytes[13] = (uint8_t)(value >> 16);
    id->bytes[14] = (uint8_t)(value >> 8);
    id->bytes[15] = (uint8_t)value;
}

static int accept_protected(ninlil_runtime *runtime, scripted_link *link,
                            const ninlil_id *message_id, uint8_t payload)
{
    ninlil_inbound inbound;
    int rc = inject_data(link, message_id, NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                         payload);

    if (rc != NINLIL_OK || drain_incoming(runtime, link) != NINLIL_OK)
        return NINLIL_ERR_FAULT;
    if (ninlil_step(runtime) != NINLIL_ERR_CAPACITY ||
        ninlil_receive(runtime, &inbound) != NINLIL_OK ||
        !same_id(&inbound.message_id, message_id) ||
        ninlil_application_accept(runtime, message_id) != NINLIL_OK)
        return NINLIL_ERR_FAULT;
    return NINLIL_OK;
}

static int fill_protected_archive(ninlil_runtime *runtime, scripted_link *link,
                                  ninlil_id *ids)
{
    unsigned int index;

    for (index = 0u; index < ARCHIVE_CAPACITY; index++) {
        test_fill_id(&ids[index], (uint8_t)(UINT8_C(0x20) + index));
        if (accept_protected(runtime, link, &ids[index], (uint8_t)index) !=
            NINLIL_OK)
            return NINLIL_ERR_FAULT;
    }
    return NINLIL_OK;
}

static int setup_leaf(char *directory, char *path, ninlil_role_profile *profile,
                      controlled_policy *policy)
{
    if (test_make_directory(directory, 40u) != 0 ||
        test_make_path(path, 80u, directory, "state.j") != 0 ||
        ninlil_role_profile_standard(NINLIL_ROLE_BATTERY_LEAF, profile) !=
            NINLIL_OK)
        return -1;
    memset(policy, 0, sizeof(*policy));
    test_policy_init(&policy->value, APP_SERVICE, 64u);
    return 0;
}

static int test_pre_attempt_receipts_do_not_mutate(void)
{
    static const struct {
        uint8_t status;
        ninlil_evidence evidence;
    } receipts[] = {
        {NINLIL_RECEIPT_EVIDENCE, NINLIL_EVIDENCE_REMOTE_STORED},
        {NINLIL_RECEIPT_EXPIRED, NINLIL_EVIDENCE_NONE},
        {NINLIL_RECEIPT_PERMANENT_REJECTION, NINLIL_EVIDENCE_NONE},
    };
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission submission;
    ninlil_info info;
    uint32_t random_state = 1u;
    uint8_t payload = 1u;
    off_t committed_size;
    size_t index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x81));
    submission = make_submission(key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);
    CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_OK);
    CHECK(journal_size(path, &committed_size) == 0);

    for (index = 0u; index < sizeof(receipts) / sizeof(receipts[0]); index++) {
        off_t current_size;

        CHECK(inject_receipt(&link, &message_id, receipts[index].status,
                             receipts[index].evidence) == NINLIL_OK);
        CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
        CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
        CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
        CHECK(info.latest_evidence == NINLIL_EVIDENCE_NONE);
        CHECK(!info.remote_boundary_may_have_been_reached);
        CHECK(journal_size(path, &current_size) == 0 &&
              current_size == committed_size);
        ninlil_close(runtime);
        runtime = NULL;
        CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                           &profile) == NINLIL_OK);
        CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
        CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
        CHECK(info.latest_evidence == NINLIL_EVIDENCE_NONE);
        CHECK(!info.remote_boundary_may_have_been_reached);
    }

    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_reordered_receipts_are_monotonic(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission submission;
    ninlil_info info;
    uint32_t random_state = 2u;
    uint8_t payload = 2u;
    off_t strong_size;
    off_t current_size;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x82));
    submission =
        make_submission(key, NINLIL_EVIDENCE_APPLICATION_ACCEPTED, &payload);
    CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &message_id) == NINLIL_OK);
    CHECK(inject_receipt(&link, &message_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &strong_size) == 0);

    CHECK(inject_receipt(&link, &message_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_GATEWAY_CUSTODY) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(inject_receipt(&link, &message_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == strong_size);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);
    CHECK(inject_receipt(&link, &message_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_APPLICATION_ACCEPTED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_receipt_classes_make_bounded_progress(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id archived_id;
    ninlil_id live_ids[2];
    ninlil_id rejected_id;
    ninlil_id outbound_key;
    ninlil_id outbound_id;
    ninlil_id last_live_receipt;
    ninlil_submission outbound;
    uint32_t random_state = 7u;
    uint8_t outbound_payload = UINT8_C(0xE4);
    uint8_t have_live_receipt = 0u;
    unsigned int index;
    unsigned int round;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    test_fill_id(&archived_id, UINT8_C(0xE0));
    CHECK(accept_protected(runtime, &link, &archived_id, UINT8_C(0xE0)) ==
          NINLIL_OK);

    for (index = 0u; index < 2u; index++) {
        uint8_t value = (uint8_t)(UINT8_C(0xE1) + index);

        test_fill_id(&live_ids[index], value);
        CHECK(inject_data(&link, &live_ids[index],
                          NINLIL_EVIDENCE_REMOTE_STORED, value) == NINLIL_OK);
        CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    }
    policy.result = NINLIL_ERR_NOT_FOUND;
    test_fill_id(&rejected_id, UINT8_C(0xE3));
    CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      UINT8_C(0xE3)) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    policy.result = NINLIL_OK;

    test_fill_id(&outbound_key, UINT8_C(0xE4));
    outbound = make_submission(outbound_key, NINLIL_EVIDENCE_REMOTE_STORED,
                               &outbound_payload);
    CHECK(ninlil_submit(runtime, &outbound, &outbound_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &outbound_id) == NINLIL_OK);

    link.receipt_count = 0u;
    for (round = 0u; round < RECEIPT_FAIRNESS_ROUNDS; round++) {
        uint8_t begin = link.receipt_count;
        unsigned int live_count = 0u;
        unsigned int application_count = 0u;
        unsigned int rejection_count = 0u;
        unsigned int step;

        for (step = 0u; step < SCHEDULER_PHASES * RECEIPT_CLASSES; step++) {
            int rc;

            if (link.incoming_length == 0u)
                CHECK(inject_data(&link, &live_ids[0],
                                  NINLIL_EVIDENCE_REMOTE_STORED,
                                  UINT8_C(0xE1)) == NINLIL_OK);
            rc = ninlil_step(runtime);
            CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_CAPACITY);
        }
        CHECK(link.receipt_count == (uint8_t)(begin + RECEIPT_CLASSES));
        for (index = begin; index < link.receipt_count; index++) {
            ninlil_id *receipt_id = &link.receipt_message_id[index];

            if (link.receipt_status[index] == NINLIL_RECEIPT_EVIDENCE &&
                link.receipt_evidence[index] == NINLIL_EVIDENCE_REMOTE_STORED) {
                CHECK(same_id(receipt_id, &live_ids[0]) ||
                      same_id(receipt_id, &live_ids[1]));
                if (have_live_receipt)
                    CHECK(!same_id(receipt_id, &last_live_receipt));
                last_live_receipt = *receipt_id;
                have_live_receipt = 1u;
                live_count++;
            } else if (link.receipt_status[index] == NINLIL_RECEIPT_EVIDENCE &&
                       link.receipt_evidence[index] ==
                           NINLIL_EVIDENCE_APPLICATION_ACCEPTED) {
                CHECK(same_id(receipt_id, &archived_id));
                application_count++;
            } else {
                CHECK(link.receipt_status[index] ==
                      NINLIL_RECEIPT_PERMANENT_REJECTION);
                CHECK(same_id(receipt_id, &rejected_id));
                rejection_count++;
            }
        }
        CHECK(live_count == 1u && application_count == 1u &&
              rejection_count == 1u);
    }

    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_inbound_archive_pressure(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id ids[ARCHIVE_CAPACITY + 1u];
    ninlil_id outbound_key;
    ninlil_id outbound_id;
    ninlil_submission outbound;
    ninlil_inbound inbound;
    ninlil_info info;
    uint32_t random_state = 3u;
    uint8_t payload = UINT8_C(0x61);
    off_t before_accept;
    off_t after_accept;
    unsigned int index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(fill_protected_archive(runtime, &link, ids) == NINLIL_OK);
    CHECK(link.send_calls >= ARCHIVE_CAPACITY);

    test_fill_id(&ids[ARCHIVE_CAPACITY], UINT8_C(0x60));
    CHECK(inject_data(&link, &ids[ARCHIVE_CAPACITY],
                      NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                      UINT8_C(0x60)) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_OK);
    CHECK(journal_size(path, &before_accept) == 0);
    CHECK(ninlil_application_accept(runtime, &ids[ARCHIVE_CAPACITY]) ==
          NINLIL_ERR_CAPACITY);
    CHECK(journal_size(path, &after_accept) == 0 &&
          after_accept == before_accept);
    for (index = 0u; index < ARCHIVE_CAPACITY; index++) {
        CHECK(ninlil_query(runtime, &ids[index], &info) == NINLIL_OK);
        CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    }
    CHECK(ninlil_query(runtime, &ids[ARCHIVE_CAPACITY], &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    for (index = 0u; index < ARCHIVE_CAPACITY; index++)
        CHECK(ninlil_query(runtime, &ids[index], &info) == NINLIL_OK);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_OK);
    CHECK(ninlil_application_accept(runtime, &ids[ARCHIVE_CAPACITY]) ==
          NINLIL_ERR_CAPACITY);

    link.send_result = NINLIL_OK;
    link.receipt_count = 0u;
    for (index = 0u; index < ARCHIVE_CAPACITY + 1u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.receipt_count == ARCHIVE_CAPACITY + 1u);
    CHECK(ninlil_application_accept(runtime, &ids[ARCHIVE_CAPACITY]) ==
          NINLIL_OK);
    CHECK(ninlil_query(runtime, &ids[ARCHIVE_CAPACITY], &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &ids[ARCHIVE_CAPACITY], &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);

    test_fill_id(&outbound_key, UINT8_C(0x62));
    outbound =
        make_submission(outbound_key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);
    CHECK(ninlil_submit(runtime, &outbound, &outbound_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &outbound_id) == NINLIL_OK);
    CHECK(inject_receipt(&link, &outbound_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &outbound_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &ids[1], &info) == NINLIL_ERR_NOT_FOUND);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &outbound_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &ids[1], &info) == NINLIL_ERR_NOT_FOUND);

    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_protected_cursor_uses_safe_slot(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id protected_id;
    ninlil_id safe_key;
    ninlil_id safe_id;
    ninlil_id filler_id;
    ninlil_id newcomer_id;
    ninlil_submission submission;
    ninlil_inbound inbound;
    ninlil_info info;
    uint32_t random_state = 6u;
    uint8_t payload = 6u;
    unsigned int index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);

    test_fill_id(&protected_id, UINT8_C(0xA0));
    CHECK(accept_protected(runtime, &link, &protected_id, payload) ==
          NINLIL_OK);
    test_fill_id(&safe_key, UINT8_C(0xA1));
    submission =
        make_submission(safe_key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);
    CHECK(ninlil_submit(runtime, &submission, &safe_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &safe_id) == NINLIL_OK);
    CHECK(inject_receipt(&link, &safe_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);

    for (index = 0u; index < ARCHIVE_CAPACITY - 2u; index++) {
        test_fill_id(&filler_id, (uint8_t)(UINT8_C(0xB0) + index));
        CHECK(accept_protected(runtime, &link, &filler_id, (uint8_t)index) ==
              NINLIL_OK);
    }
    test_fill_id(&newcomer_id, UINT8_C(0xD0));
    CHECK(inject_data(&link, &newcomer_id, NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                      payload) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_OK);
    CHECK(ninlil_application_accept(runtime, &newcomer_id) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &protected_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &newcomer_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &safe_id, &info) == NINLIL_ERR_NOT_FOUND);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &protected_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &newcomer_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &safe_id, &info) == NINLIL_ERR_NOT_FOUND);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_outbound_archive_pressure(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id archived[ARCHIVE_CAPACITY];
    ninlil_id evidence_key;
    ninlil_id terminal_key;
    ninlil_id evidence_id;
    ninlil_id terminal_id;
    ninlil_submission submission;
    ninlil_info info;
    uint32_t random_state = 4u;
    uint8_t payload = 4u;
    off_t before_blocked;
    off_t after_blocked;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(fill_protected_archive(runtime, &link, archived) == NINLIL_OK);

    test_fill_id(&evidence_key, UINT8_C(0x83));
    submission =
        make_submission(evidence_key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);
    CHECK(ninlil_submit(runtime, &submission, &evidence_id) == NINLIL_OK);
    test_fill_id(&terminal_key, UINT8_C(0x84));
    submission.idempotency_key = terminal_key;
    CHECK(ninlil_submit(runtime, &submission, &terminal_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &evidence_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &terminal_id) == NINLIL_OK);

    CHECK(inject_receipt(&link, &evidence_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_GATEWAY_CUSTODY) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &before_blocked) == 0);
    CHECK(inject_receipt(&link, &evidence_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(inject_receipt(&link, &terminal_id,
                         NINLIL_RECEIPT_PERMANENT_REJECTION,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(inject_receipt(&link, &terminal_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &after_blocked) == 0 &&
          after_blocked == before_blocked);
    CHECK(ninlil_query(runtime, &evidence_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_GATEWAY_CUSTODY);
    CHECK(ninlil_query(runtime, &terminal_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &evidence_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_GATEWAY_CUSTODY);
    CHECK(ninlil_query(runtime, &terminal_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);

    link.send_result = NINLIL_OK;
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(inject_receipt(&link, &evidence_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(inject_receipt(&link, &terminal_id,
                         NINLIL_RECEIPT_PERMANENT_REJECTION,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &evidence_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &terminal_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_FAILED);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &evidence_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_query(runtime, &terminal_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_FAILED);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_replay_rejects_protected_slot_collisions(void)
{
    unsigned int collision;

    for (collision = 0u; collision < 3u; collision++) {
        char directory[40];
        char path[80];
        scripted_link link;
        controlled_policy policy;
        ninlil_role_profile profile;
        ninlil_runtime *runtime = NULL;
        ninlil_id protected_id;
        ninlil_id affected_id;
        uint32_t random_state = (uint32_t)(20u + collision);
        uint8_t record[4u + NINLIL_ID_BYTES];
        uint8_t payload = (uint8_t)collision;
        uint8_t type;
        uint16_t length;

        CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
        memset(&link, 0, sizeof(link));
        link.send_result = NINLIL_ERR_CAPACITY;
        CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                           &profile) == NINLIL_OK);
        test_fill_id(&protected_id, (uint8_t)(UINT8_C(0x70) + collision));
        CHECK(accept_protected(runtime, &link, &protected_id, payload) ==
              NINLIL_OK);
        memset(record, 0, sizeof(record));
        record[0] = CURRENT_RECORD_VERSION;

        if (collision == 2u) {
            ninlil_inbound inbound;

            test_fill_id(&affected_id, UINT8_C(0x75));
            CHECK(inject_data(&link, &affected_id,
                              NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                              payload) == NINLIL_OK);
            CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
            CHECK(ninlil_receive(runtime, &inbound) == NINLIL_OK);
            memcpy(record + 1, affected_id.bytes, NINLIL_ID_BYTES);
            put_be16(record + 17, 0u);
            type = IN_APPLICATION_ACCEPT_RECORD;
            length = 3u + NINLIL_ID_BYTES;
        } else {
            ninlil_id key;
            ninlil_submission submission;

            test_fill_id(&key, (uint8_t)(UINT8_C(0x76) + collision));
            submission =
                make_submission(key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);
            CHECK(ninlil_submit(runtime, &submission, &affected_id) ==
                  NINLIL_OK);
            CHECK(wait_for_attempt(runtime, &affected_id) == NINLIL_OK);
            memcpy(record + 1, affected_id.bytes, NINLIL_ID_BYTES);
            record[17] = collision == 0u
                             ? (uint8_t)NINLIL_OUTCOME_FAILED
                             : (uint8_t)NINLIL_EVIDENCE_REMOTE_STORED;
            put_be16(record + 18, 0u);
            type = collision == 0u ? OUT_TERMINAL_RECORD : OUT_EVIDENCE_RECORD;
            length = 4u + NINLIL_ID_BYTES;
        }
        ninlil_close(runtime);
        runtime = NULL;
        CHECK(append_journal_record(path, profile.flash_ceiling_bytes, type,
                                    record, length) == NINLIL_OK);
        CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                           &profile) == NINLIL_ERR_CORRUPT);
        CHECK(runtime == NULL);
        test_remove_directory(directory, path, NULL);
    }
    return 0;
}

static int test_terminal_receipt_uses_required_evidence(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission submission;
    ninlil_info info;
    uint32_t random_state = 24u;
    uint8_t payload = 24u;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x79));
    submission =
        make_submission(key, NINLIL_EVIDENCE_APPLICATION_ACCEPTED, &payload);
    CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &message_id) == NINLIL_OK);
    CHECK(inject_receipt(&link, &message_id, NINLIL_RECEIPT_EVIDENCE,
                         NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);

    CHECK(inject_receipt(&link, &message_id, NINLIL_RECEIPT_PERMANENT_REJECTION,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_FAILED);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);
    ninlil_close(runtime);
    runtime = NULL;

    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_FAILED);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_old_delivery_record_is_rejected(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_journal *journal = NULL;
    uint32_t random_state = 8u;
    uint8_t old_record[OLD_OUT_HEADER];

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    memset(old_record, 0, sizeof(old_record));
    old_record[0] = 3u;
    CHECK(ninlil_journal_open(&journal, path, profile.flash_ceiling_bytes,
                              accept_journal_record, NULL) == NINLIL_OK);
    CHECK(ninlil_journal_append(journal, OLD_OUT_CREATE, old_record,
                                (uint16_t)sizeof(old_record),
                                NULL) == NINLIL_OK);
    ninlil_journal_close(journal);
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_ERR_CORRUPT);
    CHECK(runtime == NULL);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_handoff_marker_capacity_suppresses_resend(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id inbound_id;
    ninlil_inbound inbound;
    ninlil_info info;
    uint32_t random_state = 25u;
    uint8_t payload[64];
    off_t size;
    off_t maximum;
    uint32_t send_calls;
    unsigned int index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    memset(payload, UINT8_C(0x5A), sizeof(payload));
    link.send_result = NINLIL_ERR_CAPACITY;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);

    for (index = 0u; index < 2620u; index++) {
        ninlil_id key;
        ninlil_id message_id;
        ninlil_submission submission;

        indexed_id(&key, index + 1u);
        ninlil_submission_defaults(&submission);
        submission.idempotency_key = key;
        submission.target = 2u;
        submission.service = APP_SERVICE;
        submission.payload = index == 2619u ? payload : NULL;
        submission.payload_len = index == 2619u ? 31u : 0u;
        CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_OK);
        CHECK(ninlil_cancel(runtime, &message_id) == NINLIL_OK);
    }
    maximum = (off_t)profile.flash_ceiling_bytes;
    CHECK(journal_size(path, &size) == 0 && size == maximum - 113);

    test_fill_id(&inbound_id, UINT8_C(0xF1));
    CHECK(inject_empty_data(&link, &inbound_id,
                            NINLIL_EVIDENCE_APPLICATION_ACCEPTED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &size) == 0 && size == maximum - 63);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_OK);
    CHECK(ninlil_application_accept(runtime, &inbound_id) == NINLIL_OK);
    CHECK(ninlil_application_accept(runtime, &inbound_id) == NINLIL_OK);
    CHECK(journal_size(path, &size) == 0 && size == maximum - 30);
    link.send_result = NINLIL_OK;
    link.send_calls = 0u;
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(link.send_calls == 1u);
    send_calls = link.send_calls;
    for (index = 0u; index < 8u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(link.send_calls == send_calls);
    CHECK(inject_empty_data(&link, &inbound_id,
                            NINLIL_EVIDENCE_APPLICATION_ACCEPTED) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(link.send_calls == send_calls);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(link.send_calls == send_calls + 1u);
    send_calls = link.send_calls;
    for (index = 0u; index < 8u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(link.send_calls == send_calls);
    CHECK(ninlil_query(runtime, &inbound_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_application_accept(runtime, &inbound_id) == NINLIL_OK);

    ninlil_close(runtime);
    runtime = NULL;
    link.send_calls = 0u;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &inbound_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_application_accept(runtime, &inbound_id) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(link.send_calls == 1u);

    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_policy_error_classification(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_id inbound_id;
    ninlil_submission submission;
    ninlil_inbound inbound;
    uint32_t random_state = 5u;
    uint8_t payload = 5u;
    off_t empty_size;
    off_t current_size;
    uint32_t send_calls;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(journal_size(path, &empty_size) == 0);
    test_fill_id(&key, UINT8_C(0x85));
    submission = make_submission(key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);

    policy.result = NINLIL_ERR_IO;
    CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_ERR_IO);
    test_fill_id(&inbound_id, UINT8_C(0x91));
    CHECK(inject_data(&link, &inbound_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_IO);

    policy.result = NINLIL_ERR_BUSY;
    CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_ERR_BUSY);
    test_fill_id(&inbound_id, UINT8_C(0x92));
    CHECK(inject_data(&link, &inbound_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_BUSY);

    policy.result = NINLIL_OK;
    policy.invalid = 1u;
    CHECK(ninlil_submit(runtime, &submission, &message_id) ==
          NINLIL_ERR_CORRUPT);
    test_fill_id(&inbound_id, UINT8_C(0x93));
    CHECK(inject_data(&link, &inbound_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CORRUPT);
    policy.invalid = 0u;

    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);
    CHECK(journal_size(path, &current_size) == 0 && current_size == empty_size);
    send_calls = link.send_calls;
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.send_calls == send_calls);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.send_calls == send_calls);

    policy.result = NINLIL_ERR_NOT_FOUND;
    CHECK(ninlil_submit(runtime, &submission, &message_id) ==
          NINLIL_ERR_UNAUTHORIZED);
    test_fill_id(&inbound_id, UINT8_C(0x94));
    CHECK(inject_data(&link, &inbound_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.send_calls == send_calls + 1u);
    CHECK(link.last_sent_length == NINLIL_WIRE_RECEIPT_SIZE);
    CHECK(link.last_sent[3] == NINLIL_WIRE_RECEIPT);
    CHECK(link.last_sent[24] == NINLIL_RECEIPT_PERMANENT_REJECTION);
    CHECK(journal_size(path, &current_size) == 0 && current_size > empty_size);

    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_durable_permanent_rejection_tombstones(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id rejected_id;
    ninlil_inbound inbound;
    uint32_t random_state = 39u;
    uint8_t payload = UINT8_C(0x39);
    off_t empty_size;
    off_t rejected_size;
    off_t current_size;
    uint32_t send_calls;
    unsigned int index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    policy.result = NINLIL_ERR_NOT_FOUND;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(journal_size(path, &empty_size) == 0);
    test_fill_id(&rejected_id, UINT8_C(0x39));
    CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.receipt_count == 0u);
    CHECK(journal_size(path, &rejected_size) == 0 &&
          rejected_size > empty_size);
    CHECK(wait_for_receipt(runtime, &link, &rejected_id,
                           NINLIL_RECEIPT_PERMANENT_REJECTION) == NINLIL_OK);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);
    ninlil_close(runtime);
    runtime = NULL;

    policy.result = NINLIL_OK;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);
    CHECK(wait_for_receipt(runtime, &link, &rejected_id,
                           NINLIL_RECEIPT_PERMANENT_REJECTION) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == rejected_size);

    send_calls = link.send_calls;
    CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      (uint8_t)(payload + 1u)) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CONFLICT);
    for (index = 0u; index < 4u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.send_calls == send_calls);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == rejected_size);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);

    CHECK(flip_file_byte(path, 10L + (long)IN_RECORD_HEADER) == 0);
    CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CORRUPT);
    CHECK(link.send_calls == send_calls);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CORRUPT);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_ERR_CORRUPT);
    CHECK(runtime == NULL);
    CHECK(remove(path) == 0);

    memset(&link, 0, sizeof(link));
    policy.result = NINLIL_ERR_NOT_FOUND;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    for (index = 0u; index < profile.max_inbound; index++) {
        test_fill_id(&rejected_id, (uint8_t)(UINT8_C(0x40) + (uint8_t)index));
        CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                          (uint8_t)index) == NINLIL_OK);
        CHECK(ninlil_step(runtime) == NINLIL_OK);
        CHECK(wait_for_receipt(runtime, &link, &rejected_id,
                               NINLIL_RECEIPT_PERMANENT_REJECTION) ==
              NINLIL_OK);
    }
    CHECK(journal_size(path, &rejected_size) == 0);
    test_fill_id(&rejected_id, UINT8_C(0x4F));
    send_calls = link.send_calls;
    CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CAPACITY);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == rejected_size);
    for (index = 0u; index < 8u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.send_calls == send_calls);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(remove(path) == 0);

    {
        uint8_t invalid[IN_RECORD_HEADER];

        memset(invalid, 0, sizeof(invalid));
        invalid[0] = CURRENT_RECORD_VERSION;
        invalid[1] = NINLIL_OWNERSHIP_DURABLE;
        invalid[2] = NINLIL_EVIDENCE_REMOTE_STORED;
        invalid[3] = NINLIL_TRAFFIC_NORMAL;
        invalid[5] = NINLIL_RECEIPT_EXPIRED;
        put_be16(invalid + 6, 2u);
        put_be16(invalid + 8, APP_SERVICE);
        test_fill_id(&rejected_id, UINT8_C(0x50));
        memcpy(invalid + 20, rejected_id.bytes, NINLIL_ID_BYTES);
        CHECK(append_journal_record(path, profile.flash_ceiling_bytes,
                                    IN_REJECTION_RECORD, invalid,
                                    (uint16_t)sizeof(invalid)) == NINLIL_OK);
        CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                           &profile) == NINLIL_ERR_CORRUPT);
        CHECK(runtime == NULL);
    }
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_rejection_attempts_consume_interval(void)
{
    static const int results[] = {NINLIL_ERR_BUSY, NINLIL_ERR_CAPACITY};
    char directory[40];
    char path[80];
    ninlil_role_profile profile;
    controlled_policy policy;
    size_t result_index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    for (result_index = 0u; result_index < sizeof(results) / sizeof(results[0]);
         result_index++) {
        scripted_link link;
        ninlil_runtime *runtime = NULL;
        ninlil_id rejected_id;
        uint32_t random_state = (uint32_t)(40u + result_index);
        uint8_t payload = UINT8_C(0x40);
        unsigned int step;

        memset(&link, 0, sizeof(link));
        link.send_result = results[result_index];
        policy.result = NINLIL_ERR_NOT_FOUND;
        CHECK(open_runtime_high_work(&runtime, path, &link, &random_state,
                                     &policy, &profile) == NINLIL_OK);
        test_fill_id(&rejected_id, (uint8_t)(UINT8_C(0x60) + result_index));
        CHECK(inject_data(&link, &rejected_id, NINLIL_EVIDENCE_REMOTE_STORED,
                          payload) == NINLIL_OK);
        CHECK(ninlil_step(runtime) == results[result_index]);
        CHECK(link.send_calls == 1u);
        for (step = 0u; step < REJECTION_INTERVAL_STEPS - 1u; step++) {
            CHECK(ninlil_step(runtime) == NINLIL_OK);
            CHECK(link.send_calls == 1u);
        }
        CHECK(ninlil_step(runtime) == results[result_index]);
        CHECK(link.send_calls == 2u);
        ninlil_close(runtime);
        CHECK(remove(path) == 0);
    }
    test_remove_directory(directory, NULL, NULL);
    return 0;
}

static int test_posix_referenced_reads_revalidate_records(void)
{
    static const long mutation_offsets[] = {0L, 10L, 11L};
    char directory[40];
    char path[80];
    size_t index;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    for (index = 0u;
         index < sizeof(mutation_offsets) / sizeof(mutation_offsets[0]);
         index++) {
        ninlil_journal *journal = NULL;
        ninlil_journal_ref reference;
        uint8_t payload = UINT8_C(0xA5);
        uint8_t result = 0u;

        CHECK(test_make_path(path, sizeof(path), directory, "record.j") == 0);
        CHECK(ninlil_journal_open(&journal, path, UINT64_C(4096),
                                  accept_journal_record, NULL) == NINLIL_OK);
        CHECK(ninlil_journal_append(journal, OLD_OUT_CREATE, &payload, 1u,
                                    &reference) == NINLIL_OK);
        CHECK(flip_file_byte(path, mutation_offsets[index]) == 0);
        CHECK(ninlil_journal_read(journal, &reference, 0u, &result, 1u) ==
              NINLIL_ERR_CORRUPT);
        ninlil_journal_close(journal);
        journal = NULL;
        CHECK(ninlil_journal_open(&journal, path, UINT64_C(4096),
                                  accept_journal_record,
                                  NULL) == NINLIL_ERR_CORRUPT);
        CHECK(journal == NULL);
        CHECK(remove(path) == 0);
    }
    test_remove_directory(directory, NULL, NULL);
    return 0;
}

static int test_posix_runtime_stops_on_payload_corruption(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    ninlil_runtime *runtime = NULL;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_id inbound_id;
    ninlil_submission submission;
    ninlil_inbound inbound;
    uint32_t random_state = 31u;
    uint8_t payload = UINT8_C(0x31);
    uint32_t send_calls;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x31));
    submission = make_submission(key, NINLIL_EVIDENCE_REMOTE_STORED, &payload);
    CHECK(ninlil_submit(runtime, &submission, &message_id) == NINLIL_OK);
    CHECK(flip_file_byte(path, 10L + (long)OLD_OUT_HEADER) == 0);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CORRUPT);
    CHECK(link.send_calls == 0u);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_ERR_CORRUPT);
    CHECK(runtime == NULL);
    CHECK(remove(path) == 0);

    CHECK(test_make_path(path, sizeof(path), directory, "inbound.j") == 0);
    memset(&link, 0, sizeof(link));
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    test_fill_id(&inbound_id, UINT8_C(0x32));
    CHECK(inject_data(&link, &inbound_id, NINLIL_EVIDENCE_REMOTE_STORED,
                      payload) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(link.incoming_length == 0u);
    CHECK(flip_file_byte(path, 10L + (long)IN_RECORD_HEADER) == 0);
    send_calls = link.send_calls;
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_CORRUPT);
    CHECK(link.send_calls == send_calls);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_ERR_CORRUPT);
    CHECK(runtime == NULL);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_deadline_outbound_and_expired_receipts(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    fake_clock clock = {1000u, NINLIL_TIME_RESTART_SAFE, NINLIL_OK};
    ninlil_runtime *runtime = NULL;
    ninlil_id key;
    ninlil_id receipt_id;
    ninlil_id ambiguous_id;
    ninlil_id unrelated_id;
    ninlil_id unattempted_id;
    ninlil_id no_deadline_id;
    ninlil_submission submission;
    ninlil_info info;
    uint32_t random_state = 32u;
    uint8_t payload = UINT8_C(0x32);
    uint8_t sends_before;
    off_t empty_size;
    off_t attempted_size;
    off_t current_size;
    unsigned int index;

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_OK);
    CHECK(journal_size(path, &empty_size) == 0);
    test_fill_id(&key, UINT8_C(0x40));
    submission =
        make_submission(key, NINLIL_EVIDENCE_APPLICATION_ACCEPTED, &payload);
    submission.absolute_deadline_ms = 1100u;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) ==
          NINLIL_ERR_INVALID);
    CHECK(journal_size(path, &current_size) == 0 && current_size == empty_size);

    submission.required_evidence = NINLIL_EVIDENCE_REMOTE_STORED;
    clock.quality = NINLIL_TIME_UNAVAILABLE;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) == NINLIL_ERR_STATE);
    clock.quality = NINLIL_TIME_RUNTIME_ONLY;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) == NINLIL_ERR_STATE);
    clock.quality = (ninlil_time_quality)-1;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) ==
          NINLIL_ERR_INVALID);
    clock.quality = NINLIL_TIME_RESTART_SAFE;
    clock.result = NINLIL_ERR_IO;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) == NINLIL_ERR_IO);
    clock.result = NINLIL_OK;
    clock.now_ms = 1100u;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) ==
          NINLIL_ERR_EXPIRED);
    CHECK(journal_size(path, &current_size) == 0 && current_size == empty_size);

    clock.now_ms = 1000u;
    CHECK(ninlil_submit(runtime, &submission, &receipt_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &receipt_id) == NINLIL_OK);
    CHECK(journal_size(path, &attempted_size) == 0);
    clock.now_ms = 1050u;
    CHECK(inject_receipt(&link, &receipt_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == attempted_size);
    CHECK(ninlil_query(runtime, &receipt_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    clock.now_ms = 1200u;
    clock.quality = NINLIL_TIME_RUNTIME_ONLY;
    CHECK(inject_receipt(&link, &receipt_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == attempted_size);

    sends_before = data_send_count(&link, &receipt_id);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    for (index = 0u; index < 6u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(data_send_count(&link, &receipt_id) == sends_before);
    CHECK(inject_receipt(&link, &receipt_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == attempted_size);
    ninlil_close(runtime);
    runtime = NULL;

    clock.quality = NINLIL_TIME_RESTART_SAFE;
    clock.now_ms = 1100u;
    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_OK);
    CHECK(inject_receipt(&link, &receipt_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &receipt_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_EXPIRED);

    test_fill_id(&submission.idempotency_key, UINT8_C(0x41));
    submission.absolute_deadline_ms = 1200u;
    CHECK(ninlil_submit(runtime, &submission, &ambiguous_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &ambiguous_id) == NINLIL_OK);
    sends_before = data_send_count(&link, &ambiguous_id);
    clock.now_ms = 1200u;
    for (index = 0u; index < 8u; index++)
        CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(data_send_count(&link, &ambiguous_id) == sends_before);
    CHECK(ninlil_query(runtime, &ambiguous_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE &&
          info.remote_boundary_may_have_been_reached == 1u);

    test_fill_id(&submission.idempotency_key, UINT8_C(0x42));
    submission.absolute_deadline_ms = 0u;
    CHECK(ninlil_submit(runtime, &submission, &unrelated_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &unrelated_id) == NINLIL_OK);
    CHECK(data_send_count(&link, &unrelated_id) > 0u);

    test_fill_id(&submission.idempotency_key, UINT8_C(0x43));
    submission.absolute_deadline_ms = 1300u;
    CHECK(ninlil_submit(runtime, &submission, &unattempted_id) == NINLIL_OK);
    clock.now_ms = 1300u;
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &unattempted_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_EXPIRED);
    CHECK(data_send_count(&link, &unattempted_id) == 0u);

    test_fill_id(&submission.idempotency_key, UINT8_C(0x44));
    submission.absolute_deadline_ms = 0u;
    CHECK(ninlil_submit(runtime, &submission, &no_deadline_id) == NINLIL_OK);
    CHECK(wait_for_attempt(runtime, &no_deadline_id) == NINLIL_OK);
    CHECK(journal_size(path, &attempted_size) == 0);
    CHECK(inject_receipt(&link, &no_deadline_id, NINLIL_RECEIPT_EXPIRED,
                         NINLIL_EVIDENCE_NONE) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 &&
          current_size == attempted_size);
    CHECK(ninlil_query(runtime, &no_deadline_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &receipt_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_EXPIRED);
    CHECK(ninlil_query(runtime, &ambiguous_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(ninlil_query(runtime, &unattempted_id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_EXPIRED);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_deadline_inbound_and_replay_contract(void)
{
    char directory[40];
    char path[80];
    scripted_link link;
    controlled_policy policy;
    ninlil_role_profile profile;
    fake_clock clock = {1000u, NINLIL_TIME_RESTART_SAFE, NINLIL_OK};
    ninlil_runtime *runtime = NULL;
    ninlil_id message_id;
    ninlil_id key;
    ninlil_inbound inbound;
    uint32_t random_state = 33u;
    uint8_t payload = UINT8_C(0x33);
    off_t empty_size;
    off_t current_size;
    uint8_t create[OLD_OUT_HEADER];
    uint8_t attempt[1u + NINLIL_ID_BYTES];
    uint8_t terminal[4u + NINLIL_ID_BYTES];

    CHECK(setup_leaf(directory, path, &profile, &policy) == 0);
    memset(&link, 0, sizeof(link));
    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    CHECK(journal_size(path, &empty_size) == 0);
    test_fill_id(&message_id, UINT8_C(0x50));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_APPLICATION_ACCEPTED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 && current_size == empty_size);
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);

    test_fill_id(&message_id, UINT8_C(0x51));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_STATE);
    CHECK(journal_size(path, &current_size) == 0 && current_size == empty_size);
    ninlil_close(runtime);
    runtime = NULL;

    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_OK);
    clock.quality = NINLIL_TIME_UNAVAILABLE;
    test_fill_id(&message_id, UINT8_C(0x52));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_STATE);
    clock.quality = NINLIL_TIME_RUNTIME_ONLY;
    test_fill_id(&message_id, UINT8_C(0x53));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_STATE);
    clock.quality = (ninlil_time_quality)-1;
    test_fill_id(&message_id, UINT8_C(0x54));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_INVALID);
    clock.quality = NINLIL_TIME_RESTART_SAFE;
    clock.result = NINLIL_ERR_IO;
    test_fill_id(&message_id, UINT8_C(0x55));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_ERR_IO);
    clock.result = NINLIL_OK;
    clock.now_ms = 1100u;
    test_fill_id(&message_id, UINT8_C(0x56));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 && current_size == empty_size);

    clock.now_ms = 1000u;
    test_fill_id(&message_id, UINT8_C(0x57));
    CHECK(inject_deadline_data(&link, &message_id,
                               NINLIL_EVIDENCE_REMOTE_STORED, payload,
                               1100u) == NINLIL_OK);
    CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
    CHECK(journal_size(path, &current_size) == 0 && current_size > empty_size);
    {
        uint8_t receipts = link.receipt_count;
        unsigned int step;

        for (step = 0u; step < 4u && link.receipt_count == receipts; step++)
            CHECK(ninlil_step(runtime) == NINLIL_OK);
        CHECK(link.receipt_count == (uint8_t)(receipts + 1u));
        CHECK(same_id(&link.receipt_message_id[receipts], &message_id));
        CHECK(link.receipt_evidence[receipts] == NINLIL_EVIDENCE_REMOTE_STORED);
    }
    CHECK(journal_size(path, &empty_size) == 0);
    ninlil_close(runtime);
    runtime = NULL;

    CHECK(open_runtime(&runtime, path, &link, &random_state, &policy,
                       &profile) == NINLIL_OK);
    {
        uint8_t receipts = link.receipt_count;
        unsigned int step;

        CHECK(inject_deadline_data(&link, &message_id,
                                   NINLIL_EVIDENCE_REMOTE_STORED, payload,
                                   1100u) == NINLIL_OK);
        CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
        for (step = 0u; step < 4u && link.receipt_count == receipts; step++)
            CHECK(ninlil_step(runtime) == NINLIL_OK);
        CHECK(link.receipt_count == (uint8_t)(receipts + 1u));
        CHECK(same_id(&link.receipt_message_id[receipts], &message_id));
        for (step = 0u; step < 4u; step++)
            CHECK(ninlil_step(runtime) == NINLIL_OK);
        CHECK(link.receipt_count == (uint8_t)(receipts + 1u));
        CHECK(journal_size(path, &current_size) == 0 &&
              current_size == empty_size);
    }
    ninlil_close(runtime);
    runtime = NULL;

    clock.now_ms = 1100u;
    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_OK);
    {
        uint8_t receipts = link.receipt_count;
        unsigned int step;

        CHECK(inject_deadline_data(&link, &message_id,
                                   NINLIL_EVIDENCE_REMOTE_STORED, payload,
                                   1100u) == NINLIL_OK);
        CHECK(drain_incoming(runtime, &link) == NINLIL_OK);
        for (step = 0u; step < 4u && link.receipt_count == receipts; step++)
            CHECK(ninlil_step(runtime) == NINLIL_OK);
        CHECK(link.receipt_count == (uint8_t)(receipts + 1u));
        CHECK(same_id(&link.receipt_message_id[receipts], &message_id));
    }
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_OK);
    CHECK(same_id(&inbound.message_id, &message_id));
    CHECK(ninlil_receive(runtime, &inbound) == NINLIL_ERR_EMPTY);
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(remove(path) == 0);

    memset(create, 0, sizeof(create));
    create[0] = CURRENT_RECORD_VERSION;
    create[1] = NINLIL_OWNERSHIP_DURABLE;
    create[2] = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    create[3] = NINLIL_TRAFFIC_NORMAL;
    create[4] = DEADLINE_PRESENT;
    put_be16(create + 6, 2u);
    put_be16(create + 8, APP_SERVICE);
    put_be64(create + 12, 1100u);
    test_fill_id(&message_id, UINT8_C(0x58));
    test_fill_id(&key, UINT8_C(0x59));
    memcpy(create + 20, message_id.bytes, NINLIL_ID_BYTES);
    memcpy(create + 36, key.bytes, NINLIL_ID_BYTES);
    CHECK(append_journal_record(path, profile.flash_ceiling_bytes,
                                OLD_OUT_CREATE, create,
                                (uint16_t)sizeof(create)) == NINLIL_OK);
    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_ERR_CORRUPT);
    CHECK(runtime == NULL);
    CHECK(remove(path) == 0);

    memset(create, 0, sizeof(create));
    create[0] = CURRENT_RECORD_VERSION;
    create[1] = NINLIL_OWNERSHIP_DURABLE;
    create[2] = NINLIL_EVIDENCE_REMOTE_STORED;
    create[3] = NINLIL_TRAFFIC_NORMAL;
    put_be16(create + 6, 2u);
    put_be16(create + 8, APP_SERVICE);
    test_fill_id(&message_id, UINT8_C(0x5A));
    test_fill_id(&key, UINT8_C(0x5B));
    memcpy(create + 20, message_id.bytes, NINLIL_ID_BYTES);
    memcpy(create + 36, key.bytes, NINLIL_ID_BYTES);
    CHECK(append_journal_record(path, profile.flash_ceiling_bytes,
                                OLD_OUT_CREATE, create,
                                (uint16_t)sizeof(create)) == NINLIL_OK);
    memset(attempt, 0, sizeof(attempt));
    attempt[0] = CURRENT_RECORD_VERSION;
    memcpy(attempt + 1, message_id.bytes, NINLIL_ID_BYTES);
    CHECK(append_journal_record(path, profile.flash_ceiling_bytes,
                                OUT_ATTEMPT_RECORD, attempt,
                                (uint16_t)sizeof(attempt)) == NINLIL_OK);
    memset(terminal, 0, sizeof(terminal));
    terminal[0] = CURRENT_RECORD_VERSION;
    memcpy(terminal + 1, message_id.bytes, NINLIL_ID_BYTES);
    terminal[17] = NINLIL_OUTCOME_EXPIRED;
    put_be16(terminal + 18, 0u);
    CHECK(append_journal_record(path, profile.flash_ceiling_bytes,
                                OUT_TERMINAL_RECORD, terminal,
                                (uint16_t)sizeof(terminal)) == NINLIL_OK);
    CHECK(open_runtime_with_clock(&runtime, path, &link, &random_state, &policy,
                                  &profile, &clock) == NINLIL_ERR_CORRUPT);
    CHECK(runtime == NULL);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int (*const tests[])(void) = {
    test_pre_attempt_receipts_do_not_mutate,
    test_reordered_receipts_are_monotonic,
    test_receipt_classes_make_bounded_progress,
    test_inbound_archive_pressure,
    test_protected_cursor_uses_safe_slot,
    test_outbound_archive_pressure,
    test_replay_rejects_protected_slot_collisions,
    test_terminal_receipt_uses_required_evidence,
    test_old_delivery_record_is_rejected,
    test_handoff_marker_capacity_suppresses_resend,
    test_policy_error_classification,
    test_durable_permanent_rejection_tombstones,
    test_rejection_attempts_consume_interval,
    test_posix_referenced_reads_revalidate_records,
    test_posix_runtime_stops_on_payload_corruption,
    test_deadline_outbound_and_expired_receipts,
    test_deadline_inbound_and_replay_contract,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("delivery_state_%02zu %s\n", index + 1u,
               rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
