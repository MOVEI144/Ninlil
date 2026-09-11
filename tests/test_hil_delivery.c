#include "hil_delivery.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int open_hil(ninlil_runtime **runtime, const char *path, uint16_t node,
                    ninlil_link link, test_policy *policy, uint32_t *rng)
{
    ninlil_config config;

    memset(&config, 0, sizeof(config));
    config.node_id = node;
    config.journal_location = path;
    config.link = link;
    config.retry_interval_steps = 1u;
    config.max_work_per_step = 8u;
    config.random.fill = test_rng_fill;
    config.random.ctx = rng;
    config.policy_lookup = test_policy_lookup;
    config.policy_ctx = policy;
    if (ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                     &config.profile) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    return ninlil_open(runtime, &config);
}

static int test_contract(void)
{
    ninlil_hil_campaign sender = {2026090701u, 1u, 2u, 100u};
    ninlil_hil_campaign receiver = {2026090701u, 2u, 1u, 100u};
    ninlil_submission request;
    ninlil_submission saved;
    ninlil_inbound inbound;
    ninlil_id original_key;
    uint8_t payload[NINLIL_HIL_PAYLOAD_SIZE];
    uint32_t sequence = 555u;
    size_t index;

    CHECK(ninlil_hil_request(&sender, 1u, &request, payload) == NINLIL_OK);
    original_key = request.idempotency_key;
    memset(&inbound, 0, sizeof(inbound));
    inbound.source = 1u;
    inbound.service = request.service;
    inbound.ownership = request.ownership;
    inbound.required_evidence = request.required_evidence;
    inbound.traffic_class = request.traffic_class;
    inbound.payload_len = request.payload_len;
    memcpy(inbound.payload, payload, sizeof(payload));
    CHECK(ninlil_hil_inbound(&receiver, &inbound, &sequence) == NINLIL_OK);
    CHECK(sequence == 1u);
    // Reject cross-campaign, wrong direction and out-of-range identifiers.
    for (index = 0u; index < 8u; index++) {
        inbound.payload[index] ^= 1u;
        sequence = 555u;
        CHECK(ninlil_hil_inbound(&receiver, &inbound, &sequence) ==
              NINLIL_ERR_INVALID);
        CHECK(sequence == 555u);
        inbound.payload[index] ^= 1u;
    }
    inbound.payload[11] = 0u;
    CHECK(ninlil_hil_inbound(&receiver, &inbound, &sequence) ==
          NINLIL_ERR_INVALID);
    inbound.payload[11] = 101u;
    CHECK(ninlil_hil_inbound(&receiver, &inbound, &sequence) ==
          NINLIL_ERR_INVALID);
    inbound.payload[11] = 1u;
    inbound.required_evidence = NINLIL_EVIDENCE_REMOTE_RECEIVED;
    CHECK(ninlil_hil_inbound(&receiver, &inbound, &sequence) ==
          NINLIL_ERR_INVALID);
    inbound.required_evidence = NINLIL_EVIDENCE_REMOTE_STORED;
    inbound.ownership = NINLIL_OWNERSHIP_VOLATILE;
    CHECK(ninlil_hil_inbound(&receiver, &inbound, &sequence) ==
          NINLIL_ERR_INVALID);
    saved = request;
    CHECK(ninlil_hil_request(&sender, 101u, &request, payload) ==
          NINLIL_ERR_INVALID);
    CHECK(memcmp(&request, &saved, sizeof(request)) == 0);
    sender.campaign++;
    CHECK(ninlil_hil_request(&sender, 1u, &request, payload) == NINLIL_OK);
    CHECK(memcmp(&request.idempotency_key, &original_key,
                 sizeof(original_key)) != 0);
    sender.campaign = 0u;
    CHECK(ninlil_hil_request(&sender, 1u, &request, payload) ==
          NINLIL_ERR_INVALID);
    return 0;
}

static int test_batch_restart(void)
{
    ninlil_hil_campaign sender = {2026090701u, 1u, 2u, 100u};
    ninlil_hil_campaign receiver = {2026090701u, 2u, 1u, 100u};
    char directory[256], first[320], second[320];
    test_link transport;
    ninlil_link links[2];
    test_policy policy;
    ninlil_runtime *a = NULL, *b = NULL;
    uint32_t rng = 41u;
    ninlil_id ids[100];
    uint32_t sequence;
    uint32_t consumed = 0u;
    ninlil_inbound inbound;

    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(first, sizeof(first), directory, "a") == 0);
    CHECK(test_make_path(second, sizeof(second), directory, "b") == 0);
    test_link_init(&transport, 64u);
    test_link_bind(&transport, 0u, &links[0]);
    test_link_bind(&transport, 1u, &links[1]);
    test_policy_init(&policy, 0x0100u, 32u);
    CHECK(open_hil(&a, first, 1u, links[0], &policy, &rng) == NINLIL_OK);
    CHECK(open_hil(&b, second, 2u, links[1], &policy, &rng) == NINLIL_OK);
    for (sequence = 1u; sequence <= 100u; sequence++) {
        uint8_t payload[NINLIL_HIL_PAYLOAD_SIZE];
        ninlil_submission request;
        ninlil_info info;
        unsigned int step;

        CHECK(ninlil_hil_request(&sender, sequence, &request, payload) ==
              NINLIL_OK);
        CHECK(ninlil_submit(a, &request, &ids[sequence - 1u]) == NINLIL_OK);
        if (sequence == 1u) {
            ninlil_id replayed;

            // Restart with committed pending work before delivery; same key
            // must recover the same message identity rather than create anew.
            ninlil_close(a);
            CHECK(open_hil(&a, first, 1u, links[0], &policy, &rng) ==
                  NINLIL_OK);
            CHECK(ninlil_submit(a, &request, &replayed) == NINLIL_OK);
            CHECK(memcmp(&replayed, &ids[0], sizeof(replayed)) == 0);
            test_link_drop_next(&transport, 1u, 1u);
        }
        for (step = 0u; step < 100u; step++) {
            uint32_t received_sequence;

            CHECK(ninlil_step(a) == NINLIL_OK);
            CHECK(ninlil_step(b) == NINLIL_OK);
            if (ninlil_receive(b, &inbound) == NINLIL_OK) {
                CHECK(ninlil_hil_inbound(&receiver, &inbound,
                                         &received_sequence) == NINLIL_OK);
                CHECK(received_sequence == sequence);
                CHECK(memcmp(&inbound.message_id, &ids[sequence - 1u],
                             sizeof(ninlil_id)) == 0);
                CHECK(ninlil_application_accept(b, &inbound.message_id) ==
                      NINLIL_OK);
                consumed++;
            }
            CHECK(ninlil_query(a, &ids[sequence - 1u], &info) == NINLIL_OK);
            if (info.outcome == NINLIL_OUTCOME_SATISFIED)
                break;
        }
        CHECK(step < 100u &&
              info.latest_evidence >= NINLIL_EVIDENCE_REMOTE_STORED);
    }
    CHECK(consumed == 100u);
    ninlil_close(a);
    ninlil_close(b);
    CHECK(open_hil(&a, first, 1u, links[0], &policy, &rng) == NINLIL_OK);
    CHECK(open_hil(&b, second, 2u, links[1], &policy, &rng) == NINLIL_OK);
    for (sequence = 1u; sequence <= 100u; sequence++) {
        uint8_t payload[NINLIL_HIL_PAYLOAD_SIZE];
        ninlil_submission request;
        ninlil_id replayed;
        ninlil_info info;

        CHECK(ninlil_hil_request(&sender, sequence, &request, payload) ==
              NINLIL_OK);
        CHECK(ninlil_submit(a, &request, &replayed) == NINLIL_OK);
        CHECK(memcmp(&replayed, &ids[sequence - 1u], sizeof(replayed)) == 0);
        CHECK(ninlil_query(a, &replayed, &info) == NINLIL_OK);
        CHECK(info.outcome == NINLIL_OUTCOME_SATISFIED);
        CHECK(ninlil_query(b, &replayed, &info) == NINLIL_OK);
        CHECK(info.latest_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
    }
    CHECK(ninlil_receive(b, &inbound) == NINLIL_ERR_EMPTY);
    ninlil_close(a);
    ninlil_close(b);
    test_remove_directory(directory, first, second);
    return 0;
}

int main(void)
{
    return test_contract() || test_batch_restart();
}
