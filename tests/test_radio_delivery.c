#define _POSIX_C_SOURCE 200809L

#include "ninlil.h"
#include "ninlil_radio.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define APP_SERVICE UINT16_C(0x0100)
#define BATCH_MESSAGES 20u
#define TOTAL_MESSAGES 100u
#define FLASH_PAYLOAD_OFFSET 32L
#define OUTBOUND_PAYLOAD_OFFSET 52L
#define INBOUND_PAYLOAD_OFFSET 36L

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct radio_pair {
    ninlil_radio_link first;
    ninlil_radio_link second;
    uint8_t drop_first_to_second;
    uint8_t drop_second_to_first;
    uint8_t duplicate_first_to_second;
    uint8_t duplicate_second_to_first;
} radio_pair;

static test_policy delivery_policy;

static int initialize_radio(ninlil_radio_link *radio, ninlil_link *link)
{
    ninlil_radio_link_init(radio);
    ninlil_radio_link_bind(radio, link);
    if (ninlil_radio_begin_reset(radio) != NINLIL_OK)
        return -1;
    return ninlil_radio_mark_initialized(radio);
}

static int open_runtime_work(ninlil_runtime **runtime, const char *path,
                             uint16_t node_id, ninlil_link link,
                             uint32_t *random_state, uint32_t max_work)
{
    ninlil_config config;

    memset(&config, 0, sizeof(config));
    config.journal_location = path;
    config.node_id = node_id;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = max_work;
    config.link = link;
    config.random.fill = test_rng_fill;
    config.random.ctx = random_state;
    config.policy_lookup = test_policy_lookup;
    config.policy_ctx = &delivery_policy;
    if (ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                     &config.profile) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    return ninlil_open(runtime, &config);
}

static int open_runtime(ninlil_runtime **runtime, const char *path,
                        uint16_t node_id, ninlil_link link,
                        uint32_t *random_state)
{
    return open_runtime_work(runtime, path, node_id, link, random_state, 8u);
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

static int submit_message(ninlil_runtime *runtime, const ninlil_id *key,
                          uint16_t target, const uint8_t *payload,
                          uint16_t payload_len, ninlil_id *message_id)
{
    ninlil_submission request;

    ninlil_submission_defaults(&request);
    request.idempotency_key = *key;
    request.target = target;
    request.service = APP_SERVICE;
    request.payload = payload;
    request.payload_len = payload_len;
    return ninlil_submit(runtime, &request, message_id);
}

static int air_move(ninlil_radio_link *source, ninlil_radio_link *target,
                    uint8_t *drop, uint8_t *duplicate)
{
    const uint8_t *packet;
    size_t length;
    int rc;

    if (!source->tx_pending)
        return NINLIL_OK;
    rc = ninlil_radio_begin_tx(source, &packet, &length);
    if (rc != NINLIL_OK)
        return rc;
    if (*drop > 0u) {
        (*drop)--;
    } else {
        rc = ninlil_radio_push_rx(target, packet, length);
        if (rc != NINLIL_OK)
            return rc;
        if (*duplicate > 0u) {
            (*duplicate)--;
            rc = ninlil_radio_push_rx(target, packet, length);
            if (rc != NINLIL_OK)
                return rc;
        }
    }
    return ninlil_radio_tx_done(source);
}

static int cycle(radio_pair *pair, ninlil_runtime *first,
                 ninlil_runtime *second)
{
    int rc = ninlil_step(first);

    if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
        return rc;
    rc = air_move(&pair->first, &pair->second, &pair->drop_first_to_second,
                  &pair->duplicate_first_to_second);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_step(second);
    if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
        return rc;
    return air_move(&pair->second, &pair->first, &pair->drop_second_to_first,
                    &pair->duplicate_second_to_first);
}

static int accept_all(ninlil_runtime *runtime, size_t *accepted)
{
    for (;;) {
        ninlil_inbound inbound;
        int rc = ninlil_receive(runtime, &inbound);

        if (rc == NINLIL_ERR_EMPTY)
            return NINLIL_OK;
        if (rc != NINLIL_OK)
            return rc;
        rc = ninlil_application_accept(runtime, &inbound.message_id);
        if (rc != NINLIL_OK)
            return rc;
        (*accepted)++;
    }
}

static int test_direct_delivery_with_loss_and_duplicates(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    radio_pair pair;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 10u;
    uint32_t second_random = 20u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_info info;
    size_t accepted = 0u;
    unsigned int index;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(first_path, sizeof(first_path), directory,
                         "a.flash") == 0);
    CHECK(test_make_path(second_path, sizeof(second_path), directory,
                         "b.flash") == 0);
    memset(&pair, 0, sizeof(pair));
    CHECK(initialize_radio(&pair.first, &first_link) == NINLIL_OK);
    CHECK(initialize_radio(&pair.second, &second_link) == NINLIL_OK);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random) ==
          NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random) ==
          NINLIL_OK);

    pair.drop_first_to_second = 1u;
    pair.drop_second_to_first = 1u;
    pair.duplicate_first_to_second = 1u;
    test_fill_id(&key, UINT8_C(0x51));
    CHECK(submit_message(first, &key, 2u, (const uint8_t *)"radio", 5u,
                         &message_id) == NINLIL_OK);
    for (index = 0u; index < 40u; index++) {
        CHECK(cycle(&pair, first, second) == NINLIL_OK);
        CHECK(accept_all(second, &accepted) == NINLIL_OK);
    }
    CHECK(accepted == 1u);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);

    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_bidirectional_hundred_messages(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    radio_pair pair;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 30u;
    uint32_t second_random = 40u;
    ninlil_id first_ids[TOTAL_MESSAGES];
    ninlil_id second_ids[TOTAL_MESSAGES];
    size_t first_accepted = 0u;
    size_t second_accepted = 0u;
    unsigned int index;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(first_path, sizeof(first_path), directory,
                         "a.flash") == 0);
    CHECK(test_make_path(second_path, sizeof(second_path), directory,
                         "b.flash") == 0);
    memset(&pair, 0, sizeof(pair));
    CHECK(initialize_radio(&pair.first, &first_link) == NINLIL_OK);
    CHECK(initialize_radio(&pair.second, &second_link) == NINLIL_OK);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random) ==
          NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random) ==
          NINLIL_OK);

    for (index = 0u; index < TOTAL_MESSAGES; index += BATCH_MESSAGES) {
        unsigned int item;
        unsigned int cycle_index;
        size_t expected = index + BATCH_MESSAGES;

        for (item = index; item < index + BATCH_MESSAGES; item++) {
            ninlil_id key;
            uint8_t payload[8];

            test_fill_id(&key, (uint8_t)(item + 1u));
            memset(payload, (int)(uint8_t)item, sizeof(payload));
            CHECK(submit_message(first, &key, 2u, payload, sizeof(payload),
                                 &first_ids[item]) == NINLIL_OK);
            key.bytes[0] ^= UINT8_C(0xA5);
            CHECK(submit_message(second, &key, 1u, payload, sizeof(payload),
                                 &second_ids[item]) == NINLIL_OK);
        }
        for (cycle_index = 0u; cycle_index < 1000u; cycle_index++) {
            int all_satisfied = 1;

            CHECK(cycle(&pair, first, second) == NINLIL_OK);
            CHECK(accept_all(first, &first_accepted) == NINLIL_OK);
            CHECK(accept_all(second, &second_accepted) == NINLIL_OK);
            if (first_accepted != expected || second_accepted != expected)
                continue;
            for (item = index; item < index + BATCH_MESSAGES; item++) {
                ninlil_info first_info;
                ninlil_info second_info;

                if (ninlil_query(first, &first_ids[item], &first_info) !=
                        NINLIL_OK ||
                    ninlil_query(second, &second_ids[item], &second_info) !=
                        NINLIL_OK ||
                    first_info.outcome != NINLIL_OUTCOME_SATISFIED ||
                    second_info.outcome != NINLIL_OUTCOME_SATISFIED) {
                    all_satisfied = 0;
                    break;
                }
            }
            if (all_satisfied)
                break;
        }
        CHECK(cycle_index < 1000u);
    }
    CHECK(first_accepted == TOTAL_MESSAGES);
    CHECK(second_accepted == TOTAL_MESSAGES);

    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_submit_survives_abrupt_exit(void)
{
    char directory[40];
    char journal_path[80];
    radio_pair pair;
    ninlil_link link;
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 50u;
    ninlil_id key;
    ninlil_id message_id;
    pid_t child;
    int status;
    ninlil_info info;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(journal_path, sizeof(journal_path), directory,
                         "node.flash") == 0);
    memset(&pair, 0, sizeof(pair));
    CHECK(initialize_radio(&pair.first, &link) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x77));
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        ninlil_runtime *child_runtime = NULL;
        uint32_t child_random = 50u;
        ninlil_id child_message;
        int rc =
            open_runtime(&child_runtime, journal_path, 1u, link, &child_random);

        if (rc == NINLIL_OK) {
            rc = submit_message(child_runtime, &key, 2u,
                                (const uint8_t *)"owned", 5u, &child_message);
        }
        _exit(rc == NINLIL_OK ? 0 : 1);
    }
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(open_runtime(&runtime, journal_path, 1u, link, &random_state) ==
          NINLIL_OK);
    CHECK(submit_message(runtime, &key, 2u, (const uint8_t *)"owned", 5u,
                         &message_id) == NINLIL_OK);
    CHECK(ninlil_query(runtime, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);

    ninlil_close(runtime);
    test_remove_directory(directory, journal_path, NULL);
    return 0;
}

static int test_flash_runtime_stops_on_payload_corruption(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    char idempotency_path[80];
    radio_pair pair;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 60u;
    uint32_t second_random = 61u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_inbound inbound;
    uint8_t payload = UINT8_C(0x60);
    int rc;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(first_path, sizeof(first_path), directory,
                         "out.flash") == 0);
    CHECK(test_make_path(second_path, sizeof(second_path), directory,
                         "in.flash") == 0);
    CHECK(test_make_path(idempotency_path, sizeof(idempotency_path), directory,
                         "idempotency.flash") == 0);
    memset(&pair, 0, sizeof(pair));
    CHECK(initialize_radio(&pair.first, &first_link) == NINLIL_OK);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random) ==
          NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x60));
    CHECK(submit_message(first, &key, 2u, &payload, 1u, &message_id) ==
          NINLIL_OK);
    CHECK(flip_file_byte(first_path,
                         FLASH_PAYLOAD_OFFSET + OUTBOUND_PAYLOAD_OFFSET) == 0);
    CHECK(ninlil_step(first) == NINLIL_ERR_CORRUPT);
    CHECK(pair.first.tx_pending == 0u);
    ninlil_close(first);
    first = NULL;
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random) ==
          NINLIL_ERR_CORRUPT);
    CHECK(first == NULL);
    CHECK(remove(first_path) == 0);

    memset(&pair, 0, sizeof(pair));
    CHECK(initialize_radio(&pair.first, &first_link) == NINLIL_OK);
    CHECK(open_runtime(&first, idempotency_path, 1u, first_link,
                       &first_random) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x62));
    CHECK(submit_message(first, &key, 2u, &payload, 1u, &message_id) ==
          NINLIL_OK);
    CHECK(flip_file_byte(idempotency_path,
                         FLASH_PAYLOAD_OFFSET + OUTBOUND_PAYLOAD_OFFSET) == 0);
    CHECK(submit_message(first, &key, 2u, &payload, 1u, &message_id) ==
          NINLIL_ERR_CORRUPT);
    CHECK(pair.first.tx_pending == 0u);
    CHECK(ninlil_step(first) == NINLIL_ERR_CORRUPT);
    CHECK(pair.first.tx_pending == 0u);
    ninlil_close(first);
    first = NULL;
    CHECK(open_runtime(&first, idempotency_path, 1u, first_link,
                       &first_random) == NINLIL_ERR_CORRUPT);
    CHECK(first == NULL);
    CHECK(remove(idempotency_path) == 0);

    memset(&pair, 0, sizeof(pair));
    CHECK(initialize_radio(&pair.first, &first_link) == NINLIL_OK);
    CHECK(initialize_radio(&pair.second, &second_link) == NINLIL_OK);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random) ==
          NINLIL_OK);
    CHECK(open_runtime_work(&second, second_path, 2u, second_link,
                            &second_random, 1u) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x61));
    CHECK(submit_message(first, &key, 2u, &payload, 1u, &message_id) ==
          NINLIL_OK);
    rc = ninlil_step(first);
    CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_BUSY);
    CHECK(pair.first.tx_pending == 1u);
    CHECK(air_move(&pair.first, &pair.second, &pair.drop_first_to_second,
                   &pair.duplicate_first_to_second) == NINLIL_OK);
    CHECK(ninlil_step(second) == NINLIL_OK);
    CHECK(pair.second.tx_pending == 0u);
    CHECK(flip_file_byte(second_path,
                         FLASH_PAYLOAD_OFFSET + INBOUND_PAYLOAD_OFFSET) == 0);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_step(second) == NINLIL_ERR_CORRUPT);
    CHECK(pair.second.tx_pending == 0u);
    ninlil_close(first);
    ninlil_close(second);
    first = NULL;
    second = NULL;
    CHECK(open_runtime_work(&second, second_path, 2u, second_link,
                            &second_random, 1u) == NINLIL_ERR_CORRUPT);
    CHECK(second == NULL);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int (*const tests[])(void) = {
    test_direct_delivery_with_loss_and_duplicates,
    test_bidirectional_hundred_messages,
    test_submit_survives_abrupt_exit,
    test_flash_runtime_stops_on_payload_corruption,
};

int main(void)
{
    size_t index;

    test_policy_init(&delivery_policy, APP_SERVICE, 128u);
    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("delivery_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
