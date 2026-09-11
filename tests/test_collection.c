#include "ninlil_internal.h"
#include "test_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
static void open_runtime(ninlil_runtime **r, const char *path, uint16_t node,
                         test_link *link, test_policy *policy, uint32_t *rng)
{
    ninlil_config c = {0};
    c.node_id = node;
    c.journal_location = path;
    c.max_work_per_step = 4u;
    c.random.fill = test_rng_fill;
    c.random.ctx = rng;
    c.policy_lookup = test_policy_lookup;
    c.policy_ctx = policy;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_BATTERY_LEAF, &c.profile) ==
          NINLIL_OK);
    test_link_bind(link, (uint8_t)(node - 1u), &c.link);
    CHECK(ninlil_open(r, &c) == NINLIL_OK);
}
static void tick(ninlil_runtime *r)
{
    int rc = ninlil_step(r);
    CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_BUSY ||
          rc == NINLIL_ERR_CAPACITY);
}
int main(void)
{
    char directory[128], a_path[256], b_path[256];
    ninlil_runtime *a, *b;
    test_link link;
    test_policy policy;
    uint32_t rng_a = 71u, rng_b = 81u;
    ninlil_submission s;
    ninlil_id pending, last_cancelled, same;
    ninlil_info info;
    ninlil_inbound inbound;
    ninlil_journal_ref stale;
    uint8_t payload[200];
    uint64_t used, capacity;
    memset(payload, 42, sizeof(payload));
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(a_path, sizeof(a_path), directory, "a") == 0);
    CHECK(test_make_path(b_path, sizeof(b_path), directory, "b") == 0);
    test_policy_init(&policy, 256u, 8u);
    test_link_init(&link, 320u);
    open_runtime(&a, a_path, 1u, &link, &policy, &rng_a);
    open_runtime(&b, b_path, 2u, &link, &policy, &rng_b);
    ninlil_submission_defaults(&s);
    s.target = 2u;
    s.service = 256u;
    s.payload = payload;
    s.payload_len = sizeof(payload);
    s.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    test_fill_id(&s.idempotency_key, 1u);
    CHECK(ninlil_submit(a, &s, &pending) == NINLIL_OK);
    stale = ninlil_find_outbound(a, &pending)->record_ref;
    for (unsigned int i = 0u; i < 1400u; i++) {
        memset(s.idempotency_key.bytes, 0, 16u);
        s.idempotency_key.bytes[0] = 99u;
        s.idempotency_key.bytes[1] = (uint8_t)(i >> 8);
        s.idempotency_key.bytes[2] = (uint8_t)i;
        CHECK(ninlil_submit(a, &s, &last_cancelled) == NINLIL_OK);
        CHECK(ninlil_cancel(a, &last_cancelled) == NINLIL_OK);
        tick(a);
    }
    CHECK(ninlil_journal_usage(a->journal, &used, &capacity) == NINLIL_OK);
    CHECK(used < capacity && a->collected_bytes > 0u);
    CHECK(ninlil_journal_read(a->journal, &stale, 0u, NULL, 0u) ==
          NINLIL_ERR_STATE);
    CHECK(ninlil_collect(a) == NINLIL_OK);
    CHECK(ninlil_query(a, &pending, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(ninlil_cancel(a, &pending) == NINLIL_ERR_STATE);
    ninlil_close(a);
    open_runtime(&a, a_path, 1u, &link, &policy, &rng_a);
    CHECK(ninlil_query(a, &last_cancelled, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_CANCELLED);
    for (unsigned int i = 0u; i < 12u; i++) {
        tick(a);
        tick(b);
    }
    CHECK(ninlil_receive(b, &inbound) == NINLIL_OK);
    CHECK(ninlil_id_equal(&inbound.message_id, &pending) &&
          inbound.payload_len == sizeof(payload));
    CHECK(memcmp(inbound.payload, payload, sizeof(payload)) == 0);
    CHECK(ninlil_application_accept(b, &pending) == NINLIL_OK);
    CHECK(ninlil_collect(b) == NINLIL_OK);
    ninlil_close(a);
    ninlil_close(b);
    test_link_init(&link, 320u);
    open_runtime(&a, a_path, 1u, &link, &policy, &rng_a);
    open_runtime(&b, b_path, 2u, &link, &policy, &rng_b);
    for (unsigned int i = 0u; i < 32u; i++) {
        tick(a);
        tick(b);
    }
    CHECK(ninlil_query(a, &pending, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_receive(b, &inbound) == NINLIL_ERR_EMPTY);
    test_fill_id(&s.idempotency_key, 1u);
    CHECK(ninlil_submit(a, &s, &same) == NINLIL_OK &&
          ninlil_id_equal(&same, &pending));
    payload[0] ^= 1u;
    CHECK(ninlil_submit(a, &s, &same) == NINLIL_ERR_CONFLICT);
    ninlil_close(a);
    ninlil_close(b);
    test_remove_directory(directory, a_path, b_path);
    puts("automatic collection, pending/cancelled contracts, stale references "
         "and receipt-loss restart PASS");
    return 0;
}
