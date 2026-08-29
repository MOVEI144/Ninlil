#define _POSIX_C_SOURCE 200809L

#include "ninlil.h"
#include "ninlil_radio.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define APP_SERVICE UINT16_C(0x0100)

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct fake_clock {
    uint64_t now_ms;
    ninlil_time_quality quality;
    int result;
} fake_clock;

typedef struct capture_link {
    uint8_t classes[64];
    uint8_t count;
} capture_link;

static int clock_now(void *ctx, uint64_t *now_ms, ninlil_time_quality *quality)
{
    fake_clock *clock = ctx;

    if (clock->result != NINLIL_OK)
        return clock->result;
    *now_ms = clock->now_ms;
    *quality = clock->quality;
    return NINLIL_OK;
}

static int capture_send(void *ctx, const uint8_t *data, size_t length)
{
    capture_link *capture = ctx;

    if (!data || length < 40u || data[3] != 1u ||
        capture->count >= sizeof(capture->classes))
        return NINLIL_ERR_CAPACITY;
    capture->classes[capture->count++] = data[28];
    return NINLIL_OK;
}

static int empty_recv(void *ctx, uint8_t *data, size_t capacity, size_t *length)
{
    (void)ctx;
    (void)data;
    (void)capacity;
    (void)length;
    return 0;
}

static int open_runtime(ninlil_runtime **runtime, const char *path,
                        uint16_t node_id, ninlil_link link,
                        uint32_t *random_state, test_policy *policy,
                        const ninlil_role_profile *profile, fake_clock *clock)
{
    ninlil_config config;

    memset(&config, 0, sizeof(config));
    config.journal_location = path;
    config.node_id = node_id;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = 16u;
    config.link = link;
    config.random.fill = test_rng_fill;
    config.random.ctx = random_state;
    config.policy_lookup = test_policy_lookup;
    config.policy_ctx = policy;
    config.profile = *profile;
    if (clock) {
        config.clock.now = clock_now;
        config.clock.ctx = clock;
    }
    return ninlil_open(runtime, &config);
}

static ninlil_submission submission(ninlil_id key, uint16_t target,
                                    ninlil_evidence required,
                                    ninlil_traffic_class traffic_class,
                                    const uint8_t *payload,
                                    uint16_t payload_len)
{
    ninlil_submission request;

    memset(&request, 0, sizeof(request));
    request.struct_version = NINLIL_API_VERSION;
    request.idempotency_key = key;
    request.target = target;
    request.service = APP_SERVICE;
    request.ownership = NINLIL_OWNERSHIP_DURABLE;
    request.required_evidence = required;
    request.traffic_class = traffic_class;
    request.payload = payload;
    request.payload_len = payload_len;
    return request;
}

static int pump(ninlil_runtime *first, ninlil_runtime *second,
                unsigned int count)
{
    unsigned int index;

    for (index = 0u; index < count; index++) {
        int rc = ninlil_step(first);

        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY &&
            rc != NINLIL_ERR_UNAUTHORIZED && rc != NINLIL_ERR_EXPIRED)
            return rc;
        rc = ninlil_step(second);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY &&
            rc != NINLIL_ERR_UNAUTHORIZED && rc != NINLIL_ERR_EXPIRED)
            return rc;
    }
    return NINLIL_OK;
}

static int pair_setup(char *directory, char *first_path, char *second_path,
                      test_link *transport, ninlil_link *first_link,
                      ninlil_link *second_link, ninlil_role_profile *profile,
                      test_policy *policy)
{
    if (test_make_directory(directory, 40u) != 0 ||
        test_make_path(first_path, 80u, directory, "a.j") != 0 ||
        test_make_path(second_path, 80u, directory, "b.j") != 0 ||
        ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT, profile) !=
            NINLIL_OK)
        return 1;
    test_policy_init(policy, APP_SERVICE, 128u);
    test_link_init(transport, NINLIL_RADIO_MTU);
    test_link_bind(transport, 0u, first_link);
    test_link_bind(transport, 1u, second_link);
    return 0;
}

static int test_remote_store_is_default_success_and_restart_handoff(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    test_link transport;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_role_profile profile;
    test_policy policy;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 1u;
    uint32_t second_random = 2u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_id result_key;
    ninlil_id result_id;
    ninlil_submission request;
    ninlil_submission result;
    ninlil_inbound inbound;
    ninlil_info info;

    CHECK(pair_setup(directory, first_path, second_path, &transport,
                     &first_link, &second_link, &profile, &policy) == 0);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    ninlil_submission_defaults(&request);
    CHECK(request.struct_version == NINLIL_API_VERSION);
    CHECK(request.ownership == NINLIL_OWNERSHIP_DURABLE);
    CHECK(request.required_evidence == NINLIL_EVIDENCE_REMOTE_STORED);
    CHECK(request.traffic_class == NINLIL_TRAFFIC_NORMAL);
    test_fill_id(&key, UINT8_C(0x11));
    request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                         NINLIL_TRAFFIC_NORMAL, (const uint8_t *)"durable", 7u);
    CHECK(ninlil_submit(first, &request, &message_id) == NINLIL_OK);
    CHECK(pump(first, second, 4u) == NINLIL_OK);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);

    ninlil_close(second);
    second = NULL;
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_OK);
    CHECK(memcmp(inbound.message_id.bytes, message_id.bytes, NINLIL_ID_BYTES) ==
          0);
    CHECK(ninlil_application_accept(second, &message_id) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);

    test_fill_id(&result_key, UINT8_C(0x12));
    result =
        submission(result_key, 1u, NINLIL_EVIDENCE_REMOTE_STORED,
                   NINLIL_TRAFFIC_NORMAL, message_id.bytes, NINLIL_ID_BYTES);
    CHECK(ninlil_submit(second, &result, &result_id) == NINLIL_OK);
    CHECK(pump(first, second, 4u) == NINLIL_OK);
    CHECK(ninlil_receive(first, &inbound) == NINLIL_OK);
    CHECK(memcmp(inbound.payload, message_id.bytes, NINLIL_ID_BYTES) == 0);
    CHECK(ninlil_application_accept(first, &result_id) == NINLIL_OK);

    ninlil_close(first);
    first = NULL;
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_application_evidence_and_receipt_loss(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    test_link transport;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_role_profile profile;
    test_policy policy;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 3u;
    uint32_t second_random = 4u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission request;
    ninlil_inbound inbound;
    ninlil_info info;

    CHECK(pair_setup(directory, first_path, second_path, &transport,
                     &first_link, &second_link, &profile, &policy) == 0);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x22));
    request = submission(key, 2u, NINLIL_EVIDENCE_APPLICATION_ACCEPTED,
                         NINLIL_TRAFFIC_CONTROL, (const uint8_t *)"adopt", 5u);
    test_link_duplicate_next(&transport, 0u, 1u);
    CHECK(ninlil_submit(first, &request, &message_id) == NINLIL_OK);
    CHECK(pump(first, second, 4u) == NINLIL_OK);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_REMOTE_STORED);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);

    ninlil_close(second);
    second = NULL;
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       &policy, &profile, NULL) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_OK);
    /* Revocation blocks new admissions but cannot erase an accepted duplicate
     * or replace its committed evidence with permanent rejection. */
    policy.session_membership_epoch = 0u;
    test_link_drop_next(&transport, 1u, 1u);
    CHECK(ninlil_application_accept(second, &message_id) == NINLIL_OK);
    CHECK(pump(first, second, 12u) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED);

    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_malformed_contract_and_unauthorized_before_inbox(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    test_link transport;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_role_profile profile;
    test_policy first_policy;
    test_policy second_policy;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 5u;
    uint32_t second_random = 6u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission request;
    ninlil_inbound inbound;
    ninlil_info info;

    CHECK(pair_setup(directory, first_path, second_path, &transport,
                     &first_link, &second_link, &profile, &first_policy) == 0);
    second_policy = first_policy;
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random,
                       &first_policy, &profile, NULL) == NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       &second_policy, &profile, NULL) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x33));
    request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                         NINLIL_TRAFFIC_NORMAL, (const uint8_t *)"safe", 4u);
    CHECK(ninlil_submit(first, &request, &message_id) == NINLIL_OK);
    CHECK(ninlil_step(first) == NINLIL_OK);
    CHECK(transport.endpoint[1].count == 1u);
    transport.endpoint[1].packets[0][27] = UINT8_C(0xFE);
    CHECK(ninlil_step(second) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);

    second_policy.capabilities &= ~NINLIL_CAP_APP_SEND;
    CHECK(pump(first, second, 5u) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_FAILED);

    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_reserve_and_starvation_bounds(void)
{
    char directory[40];
    char path[80];
    capture_link capture;
    ninlil_link link;
    ninlil_role_profile profile;
    test_policy policy;
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 7u;
    ninlil_id ids[16];
    unsigned int index;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "schedule.j") == 0);
    memset(&capture, 0, sizeof(capture));
    memset(&link, 0, sizeof(link));
    link.send = capture_send;
    link.recv = empty_recv;
    link.ctx = &capture;
    link.max_packet_size = 320u;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_BATTERY_LEAF, &profile) ==
          NINLIL_OK);
    test_policy_init(&policy, APP_SERVICE, 32u);
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_OK);
    for (index = 0u; index < 5u; index++) {
        ninlil_id key;
        ninlil_submission request;
        uint8_t payload = (uint8_t)index;

        test_fill_id(&key, (uint8_t)(0x40u + index));
        request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                             NINLIL_TRAFFIC_NORMAL, &payload, 1u);
        CHECK(ninlil_submit(runtime, &request, &ids[index]) == NINLIL_OK);
    }
    {
        ninlil_id key;
        ninlil_submission request;
        uint8_t payload = 9u;

        test_fill_id(&key, UINT8_C(0x4F));
        request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                             NINLIL_TRAFFIC_NORMAL, &payload, 1u);
        CHECK(ninlil_submit(runtime, &request, &ids[5]) == NINLIL_ERR_CAPACITY);
        request.traffic_class = NINLIL_TRAFFIC_CONTROL;
        CHECK(ninlil_submit(runtime, &request, &ids[5]) == NINLIL_OK);
        key.bytes[0]++;
        request.idempotency_key = key;
        request.traffic_class = NINLIL_TRAFFIC_CRITICAL;
        CHECK(ninlil_submit(runtime, &request, &ids[6]) == NINLIL_OK);
        key.bytes[0]++;
        request.idempotency_key = key;
        CHECK(ninlil_submit(runtime, &request, &ids[7]) == NINLIL_OK);
        key.bytes[0]++;
        request.idempotency_key = key;
        request.traffic_class = NINLIL_TRAFFIC_BULK;
        CHECK(ninlil_submit(runtime, &request, &ids[8]) == NINLIL_ERR_CAPACITY);
    }
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "control-reserve.j") ==
          0);
    memset(&capture, 0, sizeof(capture));
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_OK);
    for (index = 0u; index < 7u; index++) {
        ninlil_id key;
        ninlil_submission request;
        uint8_t payload = (uint8_t)index;

        test_fill_id(&key, (uint8_t)(UINT8_C(0x50) + index));
        request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                             NINLIL_TRAFFIC_CRITICAL, &payload, 1u);
        CHECK(ninlil_submit(runtime, &request, &ids[index]) == NINLIL_OK);
    }
    {
        ninlil_id key;
        ninlil_submission request;
        uint8_t payload = UINT8_C(0x5F);

        test_fill_id(&key, UINT8_C(0x5F));
        request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                             NINLIL_TRAFFIC_CRITICAL, &payload, 1u);
        CHECK(ninlil_submit(runtime, &request, &ids[7]) == NINLIL_ERR_CAPACITY);
        ninlil_close(runtime);
        runtime = NULL;
        CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                           &profile, NULL) == NINLIL_OK);
        CHECK(ninlil_submit(runtime, &request, &ids[7]) == NINLIL_ERR_CAPACITY);
        request.traffic_class = NINLIL_TRAFFIC_CONTROL;
        CHECK(ninlil_submit(runtime, &request, &ids[7]) == NINLIL_OK);
    }
    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_OK);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "starve.j") == 0);
    memset(&capture, 0, sizeof(capture));
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &profile) == NINLIL_OK);
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_OK);
    for (index = 0u; index < 10u; index++) {
        ninlil_id key;
        ninlil_submission request;
        uint8_t payload = (uint8_t)index;

        test_fill_id(&key, (uint8_t)(0x60u + index));
        request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                             index == 9u ? NINLIL_TRAFFIC_NORMAL
                                         : NINLIL_TRAFFIC_CRITICAL,
                             &payload, 1u);
        CHECK(ninlil_submit(runtime, &request, &ids[index]) == NINLIL_OK);
    }
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(capture.count == 10u);
    for (index = 0u; index < 8u; index++)
        CHECK(capture.classes[index] == NINLIL_TRAFFIC_CRITICAL);
    CHECK(capture.classes[8] == NINLIL_TRAFFIC_NORMAL);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_deadline_cancel_and_unknown_restart(void)
{
    char directory[40];
    char path[80];
    capture_link capture;
    ninlil_link link;
    ninlil_role_profile profile;
    test_policy policy;
    fake_clock clock = {1000u, NINLIL_TIME_RESTART_SAFE, NINLIL_OK};
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 8u;
    ninlil_id key;
    ninlil_id cancel_id;
    ninlil_id unknown_id;
    ninlil_id expired_id;
    ninlil_submission request;
    ninlil_info info;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "deadline.j") == 0);
    memset(&capture, 0, sizeof(capture));
    memset(&link, 0, sizeof(link));
    link.send = capture_send;
    link.recv = empty_recv;
    link.ctx = &capture;
    link.max_packet_size = 320u;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &profile) == NINLIL_OK);
    test_policy_init(&policy, APP_SERVICE, 32u);
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, &clock) == NINLIL_OK);

    test_fill_id(&key, UINT8_C(0x71));
    request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                         NINLIL_TRAFFIC_NORMAL, (const uint8_t *)"x", 1u);
    CHECK(ninlil_submit(runtime, &request, &cancel_id) == NINLIL_OK);
    CHECK(ninlil_cancel(runtime, &cancel_id) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &cancel_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_CANCELLED);

    test_fill_id(&request.idempotency_key, UINT8_C(0x72));
    CHECK(ninlil_submit(runtime, &request, &unknown_id) == NINLIL_OK);
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(ninlil_cancel(runtime, &unknown_id) == NINLIL_ERR_STATE);
    CHECK(ninlil_mark_unknown(runtime, &unknown_id) == NINLIL_OK);

    test_fill_id(&request.idempotency_key, UINT8_C(0x73));
    request.absolute_deadline_ms = 1100u;
    CHECK(ninlil_submit(runtime, &request, &expired_id) == NINLIL_OK);
    clock.now_ms = 1100u;
    CHECK(ninlil_step(runtime) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &expired_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_EXPIRED);
    test_fill_id(&request.idempotency_key, UINT8_C(0x74));
    CHECK(ninlil_submit(runtime, &request, &expired_id) == NINLIL_ERR_EXPIRED);

    ninlil_close(runtime);
    runtime = NULL;
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, &clock) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &unknown_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_UNKNOWN);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_service_direction_payload_class_and_quota(void)
{
    char directory[40];
    char path[80];
    capture_link capture;
    ninlil_link link;
    ninlil_role_profile profile;
    test_policy policy;
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 10u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission request;
    uint8_t payload[4] = {1u, 2u, 3u, 4u};

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "authorization.j") ==
          0);
    memset(&capture, 0, sizeof(capture));
    memset(&link, 0, sizeof(link));
    link.send = capture_send;
    link.recv = empty_recv;
    link.ctx = &capture;
    link.max_packet_size = 320u;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &profile) == NINLIL_OK);
    test_policy_init(&policy, APP_SERVICE, 1u);
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x79));
    request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                         NINLIL_TRAFFIC_CRITICAL, payload, sizeof(payload));
    {
        struct stat before;
        struct stat after;

        CHECK(stat(path, &before) == 0);
        request.traffic_class = (ninlil_traffic_class)-1;
        CHECK(NINLIL_TRAFFIC_MASK(request.traffic_class) == 0u);
        CHECK(ninlil_submit(runtime, &request, &message_id) ==
              NINLIL_ERR_INVALID);
        request.traffic_class = (ninlil_traffic_class)(NINLIL_TRAFFIC_BULK + 1);
        CHECK(NINLIL_TRAFFIC_MASK(request.traffic_class) == 0u);
        CHECK(ninlil_submit(runtime, &request, &message_id) ==
              NINLIL_ERR_INVALID);
        CHECK(stat(path, &after) == 0 && before.st_size == after.st_size);
        CHECK(ninlil_role_profile_standard((ninlil_role)-1, &profile) ==
              NINLIL_ERR_INVALID);
        request.traffic_class = NINLIL_TRAFFIC_CRITICAL;
    }
    policy.grants[0].directions = NINLIL_SERVICE_SEND;
    CHECK(ninlil_submit(runtime, &request, &message_id) ==
          NINLIL_ERR_UNAUTHORIZED);
    policy.grants[0].directions = NINLIL_SERVICE_BOTH;
    policy.grants[0].maximum_payload_bytes = 2u;
    CHECK(ninlil_submit(runtime, &request, &message_id) ==
          NINLIL_ERR_UNAUTHORIZED);
    policy.grants[0].maximum_payload_bytes = 32u;
    policy.grants[0].traffic_class_mask =
        NINLIL_TRAFFIC_MASK(NINLIL_TRAFFIC_NORMAL);
    CHECK(ninlil_submit(runtime, &request, &message_id) ==
          NINLIL_ERR_UNAUTHORIZED);
    request.traffic_class = NINLIL_TRAFFIC_NORMAL;
    CHECK(ninlil_submit(runtime, &request, &message_id) == NINLIL_OK);
    test_fill_id(&request.idempotency_key, UINT8_C(0x7A));
    CHECK(ninlil_submit(runtime, &request, &message_id) ==
          NINLIL_ERR_UNAUTHORIZED);
    policy.session_membership_epoch = 0u;
    test_fill_id(&request.idempotency_key, UINT8_C(0x7B));
    CHECK(ninlil_submit(runtime, &request, &message_id) == NINLIL_ERR_STATE);
    ninlil_close(runtime);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int test_mtu_and_incompatible_journal_rejected(void)
{
    char directory[40];
    char path[80];
    test_link transport;
    ninlil_link link;
    ninlil_role_profile profile;
    test_policy policy;
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 9u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_submission request;
    uint8_t payload[53];
    struct stat before;
    struct stat after;
    FILE *file;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "mtu.j") == 0);
    test_link_init(&transport, NINLIL_RADIO_MTU);
    test_link_bind(&transport, 0u, &link);
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &profile) == NINLIL_OK);
    test_policy_init(&policy, APP_SERVICE, 32u);
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_OK);
    CHECK(stat(path, &before) == 0);
    memset(payload, UINT8_C(0xA5), sizeof(payload));
    test_fill_id(&key, UINT8_C(0x75));
    request = submission(key, 2u, NINLIL_EVIDENCE_REMOTE_STORED,
                         NINLIL_TRAFFIC_NORMAL, payload, sizeof(payload));
    CHECK(ninlil_submit(runtime, &request, &message_id) ==
          NINLIL_ERR_TOO_LARGE);
    CHECK(stat(path, &after) == 0 && before.st_size == after.st_size);
    ninlil_close(runtime);
    CHECK(remove(path) == 0);

    file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite("NJL2\x02\x01\x00\x00\xff\xff", 1u, 10u, file) == 10u);
    CHECK(fclose(file) == 0);
    CHECK(open_runtime(&runtime, path, 1u, link, &random_state, &policy,
                       &profile, NULL) == NINLIL_ERR_CORRUPT);
    test_remove_directory(directory, path, NULL);
    return 0;
}

static int (*const tests[])(void) = {
    test_remote_store_is_default_success_and_restart_handoff,
    test_application_evidence_and_receipt_loss,
    test_malformed_contract_and_unauthorized_before_inbox,
    test_reserve_and_starvation_bounds,
    test_deadline_cancel_and_unknown_restart,
    test_service_direction_payload_class_and_quota,
    test_mtu_and_incompatible_journal_rejected,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("core_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
