#include "ninlil.h"
#include "test_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
static void open_runtime(ninlil_runtime **runtime, const char *path,
                         uint16_t node, test_link *link, uint8_t side,
                         test_policy *policy, uint32_t *rng)
{
    ninlil_config c = {0};
    c.node_id = node;
    c.journal_location = path;
    c.max_work_per_step = 8u;
    c.random.fill = test_rng_fill;
    c.random.ctx = rng;
    c.policy_lookup = test_policy_lookup;
    c.policy_ctx = policy;
    REQUIRE(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                         &c.profile) == NINLIL_OK);
    test_link_bind(link, side, &c.link);
    REQUIRE(ninlil_open(runtime, &c) == NINLIL_OK);
}
static void cycle(ninlil_runtime *a, ninlil_runtime *b)
{
    for (unsigned int i = 0u; i < 12u; i++) {
        REQUIRE(ninlil_step(a) == NINLIL_OK);
        REQUIRE(ninlil_step(b) == NINLIL_OK);
    }
}
int main(void)
{
    char directory[128], a_path[256], b_path[256];
    ninlil_runtime *a = NULL, *b = NULL;
    test_link link;
    test_policy policy;
    ninlil_submission s;
    ninlil_id first, second;
    ninlil_info info;
    ninlil_inbound inbound;
    uint32_t rng_a = 7u, rng_b = 8u;
    REQUIRE(test_make_directory(directory, sizeof(directory)) == 0);
    REQUIRE(test_make_path(a_path, sizeof(a_path), directory, "a") == 0);
    REQUIRE(test_make_path(b_path, sizeof(b_path), directory, "b") == 0);
    test_policy_init(&policy, 0x100u, 1u);
    test_link_init(&link, 320u);
    open_runtime(&a, a_path, 1u, &link, 0u, &policy, &rng_a);
    open_runtime(&b, b_path, 2u, &link, 1u, &policy, &rng_b);
    ninlil_submission_defaults(&s);
    s.target = 2u;
    s.service = 0x100u;
    s.payload = (const uint8_t *)"value";
    s.payload_len = 5u;
    test_fill_id(&s.idempotency_key, 1u);
    REQUIRE(ninlil_submit(a, &s, &first) == NINLIL_OK);
    cycle(a, b);
    REQUIRE(ninlil_query(a, &first, &info) == NINLIL_OK &&
            info.outcome == NINLIL_OUTCOME_SATISFIED);
    /* The source's live count is zero; destination still has one stored inbox
     * item. */
    test_fill_id(&s.idempotency_key, 2u);
    REQUIRE(ninlil_submit(a, &s, &second) == NINLIL_OK);
    cycle(a, b);
    REQUIRE(ninlil_query(a, &second, &info) == NINLIL_OK &&
            info.outcome == NINLIL_OUTCOME_FAILED);
    REQUIRE(ninlil_receive(b, &inbound) == NINLIL_OK);
    REQUIRE(ninlil_application_accept(b, &inbound.message_id) == NINLIL_OK);
    cycle(a, b);
    REQUIRE(ninlil_receive(b, &inbound) == NINLIL_ERR_EMPTY);
    ninlil_close(a);
    ninlil_close(b);
    test_link_init(&link, 320u);
    open_runtime(&a, a_path, 1u, &link, 0u, &policy, &rng_a);
    open_runtime(&b, b_path, 2u, &link, 1u, &policy, &rng_b);
    cycle(a, b);
    REQUIRE(ninlil_query(a, &second, &info) == NINLIL_OK &&
            info.outcome == NINLIL_OUTCOME_FAILED);
    printf("REPRODUCED service capacity: second authorized message is durably "
           "FAILED while first inbox item occupies quota=1; freeing capacity "
           "and restarting do not retry\n");
    ninlil_close(a);
    ninlil_close(b);
    test_remove_directory(directory, a_path, b_path);
    return 0;
}
