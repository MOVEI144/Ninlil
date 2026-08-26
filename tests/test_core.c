#define _POSIX_C_SOURCE 200809L

#include "ninlil.h"
#include "ninlil_radio.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, \
                    #expression);                                             \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int open_runtime(ninlil_runtime **runtime,
                        const char *path,
                        uint16_t node_id,
                        ninlil_link link,
                        uint32_t *random_state,
                        uint32_t max_outbound,
                        uint32_t max_inbound)
{
    ninlil_config config;

    memset(&config, 0, sizeof(config));
    config.journal_location = path;
    config.node_id = node_id;
    config.max_outbound = max_outbound;
    config.max_inbound = max_inbound;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = 8u;
    config.link = link;
    config.random.fill = test_rng_fill;
    config.random.ctx = random_state;
    return ninlil_open(runtime, &config);
}

static int pump(ninlil_runtime *first, ninlil_runtime *second, unsigned int count)
{
    unsigned int index;

    for (index = 0u; index < count; index++) {
        int rc = ninlil_step(first);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
            return rc;
        rc = ninlil_step(second);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
            return rc;
    }
    return NINLIL_OK;
}

static int test_durable_direct_and_receiver_replay(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    test_link transport;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 1u;
    uint32_t second_random = 2u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_inbound inbound;
    ninlil_info info;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(first_path, sizeof(first_path), directory, "a.j") == 0);
    CHECK(test_make_path(second_path, sizeof(second_path), directory, "b.j") == 0);
    test_link_init(&transport, NINLIL_RADIO_MTU);
    test_link_bind(&transport, 0u, &first_link);
    test_link_bind(&transport, 1u, &second_link);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random, 8u,
                       8u) == NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       8u, 8u) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x11));
    CHECK(ninlil_submit(first, &key, 2u, 7u,
                        (const uint8_t *)"durable", 7u,
                        &message_id) == NINLIL_OK);
    CHECK(pump(first, second, 4u) == NINLIL_OK);
    ninlil_close(second);
    second = NULL;
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       8u, 8u) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_OK);
    CHECK(memcmp(inbound.message_id.bytes, message_id.bytes,
                 NINLIL_ID_BYTES) == 0);
    CHECK(ninlil_complete(second, &message_id,
                          NINLIL_PROGRESS_APPLIED) == NINLIL_OK);
    CHECK(pump(first, second, 5u) == NINLIL_OK);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);

    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_mtu_rejected_before_durable_ownership(void)
{
    char directory[40];
    char journal_path[80];
    test_link transport;
    ninlil_link link;
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 3u;
    ninlil_id key;
    ninlil_id message_id;
    uint8_t payload[65];
    struct stat before;
    struct stat after;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(journal_path, sizeof(journal_path), directory,
                         "node.j") == 0);
    test_link_init(&transport, NINLIL_RADIO_MTU);
    test_link_bind(&transport, 0u, &link);
    CHECK(open_runtime(&runtime, journal_path, 1u, link, &random_state, 8u,
                       8u) == NINLIL_OK);
    CHECK(stat(journal_path, &before) == 0);
    memset(payload, UINT8_C(0xA5), sizeof(payload));
    test_fill_id(&key, UINT8_C(0x22));
    CHECK(ninlil_submit(runtime, &key, 2u, 1u, payload,
                        (uint16_t)sizeof(payload),
                        &message_id) == NINLIL_ERR_TOO_LARGE);
    CHECK(stat(journal_path, &after) == 0);
    CHECK(before.st_size == after.st_size);

    ninlil_close(runtime);
    test_remove_directory(directory, journal_path, NULL);
    return 0;
}

static int test_lost_receipt_and_duplicate_data(void)
{
    char directory[40];
    char first_path[80];
    char second_path[80];
    test_link transport;
    ninlil_link first_link;
    ninlil_link second_link;
    ninlil_runtime *first = NULL;
    ninlil_runtime *second = NULL;
    uint32_t first_random = 4u;
    uint32_t second_random = 5u;
    ninlil_id key;
    ninlil_id message_id;
    ninlil_inbound inbound;
    ninlil_info info;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(first_path, sizeof(first_path), directory, "a.j") == 0);
    CHECK(test_make_path(second_path, sizeof(second_path), directory, "b.j") == 0);
    test_link_init(&transport, NINLIL_RADIO_MTU);
    test_link_bind(&transport, 0u, &first_link);
    test_link_bind(&transport, 1u, &second_link);
    CHECK(open_runtime(&first, first_path, 1u, first_link, &first_random, 8u,
                       8u) == NINLIL_OK);
    CHECK(open_runtime(&second, second_path, 2u, second_link, &second_random,
                       8u, 8u) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x33));
    test_link_duplicate_next(&transport, 0u, 1u);
    CHECK(ninlil_submit(first, &key, 2u, 2u, (const uint8_t *)"once", 4u,
                        &message_id) == NINLIL_OK);
    CHECK(pump(first, second, 4u) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);
    test_link_drop_next(&transport, 1u, 1u);
    CHECK(ninlil_complete(second, &message_id,
                          NINLIL_PROGRESS_APPLIED) == NINLIL_OK);
    CHECK(pump(first, second, 8u) == NINLIL_OK);
    CHECK(ninlil_receive(second, &inbound) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_query(first, &message_id, &info) == NINLIL_OK);
    CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);

    ninlil_close(first);
    ninlil_close(second);
    test_remove_directory(directory, first_path, second_path);
    return 0;
}

static int test_m0_config_compatibility(void)
{
    char directory[40];
    char journal_path[80];
    test_link transport;
    ninlil_link link;
    ninlil_config config;
    ninlil_runtime *runtime = NULL;
    uint32_t random_state = 6u;
    ninlil_id key;
    ninlil_id message_id;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(journal_path, sizeof(journal_path), directory,
                         "compat.j") == 0);
    test_link_init(&transport, NINLIL_MAX_PAYLOAD + 28u);
    test_link_bind(&transport, 0u, &link);
    link.max_packet_size = 0u;

    memset(&config, 0, sizeof(config));
    config.journal_path = journal_path;
    config.node_id = 1u;
    config.max_outbound = 8u;
    config.max_inbound = 8u;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = 8u;
    config.link = link;
    config.random.fill = test_rng_fill;
    config.random.ctx = &random_state;
    CHECK(ninlil_open(&runtime, &config) == NINLIL_OK);
    test_fill_id(&key, UINT8_C(0x44));
    CHECK(ninlil_submit(runtime, &key, 2u, 1u,
                        (const uint8_t *)"compat", 6u,
                        &message_id) == NINLIL_OK);
    ninlil_close(runtime);
    runtime = NULL;

    config.journal_location = "different-location";
    runtime = (ninlil_runtime *)(uintptr_t)1u;
    CHECK(ninlil_open(&runtime, &config) == NINLIL_ERR_INVALID);
    CHECK(runtime == NULL);
    runtime = (ninlil_runtime *)(uintptr_t)1u;
    CHECK(ninlil_open(&runtime, NULL) == NINLIL_ERR_INVALID);
    CHECK(runtime == NULL);
    test_remove_directory(directory, journal_path, NULL);
    return 0;
}

static int (*const tests[])(void) = {
    test_durable_direct_and_receiver_replay,
    test_mtu_rejected_before_durable_ownership,
    test_lost_receipt_and_duplicate_data,
    test_m0_config_compatibility,
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
