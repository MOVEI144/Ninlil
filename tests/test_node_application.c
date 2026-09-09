#define _POSIX_C_SOURCE 200809L
#include "node_example.h"
#include "test_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
ninlil_identity node_identity;
static int interrupt_accept;
static int must_be_empty(void *ctx, uint8_t type, const uint8_t *data,
                         uint16_t size, const ninlil_journal_ref *ref)
{
    (void)ctx;
    (void)type;
    (void)data;
    (void)size;
    (void)ref;
    return NINLIL_ERR_CORRUPT;
}
int __real_ninlil_application_accept(ninlil_runtime *, const ninlil_id *);
int __wrap_ninlil_application_accept(ninlil_runtime *, const ninlil_id *);
int __wrap_ninlil_application_accept(ninlil_runtime *core, const ninlil_id *id)
{
    /* Failure boundary after the real app ledger commit, before Core adoption.
     */
    return interrupt_accept ? NINLIL_ERR_IO
                            : __real_ninlil_application_accept(core, id);
}
static void open_core(ninlil_runtime **core, const char *path, uint16_t node,
                      test_link *link, test_policy *policy, uint32_t *rng)
{
    ninlil_config c = {0};
    c.node_id = node;
    c.journal_location = path;
    c.max_work_per_step = 4u;
    c.random = (ninlil_random){test_rng_fill, rng};
    c.policy_lookup = test_policy_lookup;
    c.policy_ctx = policy;
    test_link_bind(link, (uint8_t)(node - 1u), &c.link);
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &c.profile) == NINLIL_OK);
    CHECK(ninlil_open(core, &c) == NINLIL_OK);
}
static int cycle(ninlil_runtime *a, ninlil_runtime *b)
{
    CHECK(ninlil_step(a) == NINLIL_OK);
    CHECK(ninlil_step(b) == NINLIL_OK);
    return node_application_step(b);
}
int main(void)
{
    char directory[128], a_path[256], b_path[256], app_path[256];
    ninlil_runtime *a = NULL, *b = NULL;
    test_link link;
    test_policy policy;
    uint32_t ra = 3u, rb = 4u;
    ninlil_id id;
    ninlil_info info;
    ninlil_submission empty;
    FILE *file;
    int byte, rc = NINLIL_OK;
    CHECK(psa_crypto_init() == PSA_SUCCESS);
    node_identity.identity[0] = 7u;
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(a_path, sizeof(a_path), directory, "a") == 0);
    CHECK(test_make_path(b_path, sizeof(b_path), directory, "b") == 0);
    CHECK(test_make_path(app_path, sizeof(app_path), directory, "app") == 0);
    test_link_init(&link, 320u);
    test_policy_init(&policy, 256u, 32u);
    open_core(&a, a_path, 1u, &link, &policy, &ra);
    open_core(&b, b_path, 2u, &link, &policy, &rb);
    CHECK(node_application_open(app_path, 0) == NINLIL_ERR_CORRUPT);
    CHECK(node_application_open(app_path, 1) == NINLIL_OK);
    CHECK(node_application_submit(a, 2u, 1u, &id) == NINLIL_OK);
    interrupt_accept = 1;
    for (unsigned int i = 0u; i < 100u && rc == NINLIL_OK; i++)
        rc = cycle(a, b);
    CHECK(rc == NINLIL_ERR_IO && node_application_count() == 1u);
    CHECK(ninlil_query(a, &id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    node_application_close();
    ninlil_close(b);
    interrupt_accept = 0;
    open_core(&b, b_path, 2u, &link, &policy, &rb);
    CHECK(node_application_open(app_path, 0) == NINLIL_OK &&
          node_application_count() == 1u);
    for (unsigned int i = 0u; i < 100u; i++)
        CHECK(cycle(a, b) == NINLIL_OK);
    CHECK(ninlil_query(a, &id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_SATISFIED &&
          info.latest_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
    CHECK(node_application_count() == 1u);
    ninlil_submission_defaults(&empty);
    empty.target = 2u;
    empty.service = 256u;
    empty.idempotency_key.bytes[0] = 2u;
    empty.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    CHECK(ninlil_submit(a, &empty, &id) == NINLIL_OK);
    for (unsigned int i = 0u; i < 100u; i++)
        CHECK(cycle(a, b) == NINLIL_OK);
    CHECK(ninlil_query(a, &id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(node_application_count() == 2u);
    node_application_close();
    ninlil_close(a);
    ninlil_close(b);
    node_identity.identity[0] ^= 1u;
    CHECK(node_application_open(app_path, 0) == NINLIL_ERR_CORRUPT);
    node_identity.identity[0] ^= 1u;
    file = fopen(app_path, "r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, 70L, SEEK_SET) == 0);
    byte = fgetc(file);
    CHECK(byte != EOF);
    CHECK(fseek(file, 70L, SEEK_SET) == 0 && fputc(byte ^ 1, file) != EOF &&
          fclose(file) == 0);
    CHECK(node_application_open(app_path, 0) == NINLIL_ERR_CORRUPT);
    CHECK(unlink(app_path) == 0);
    /* A valid journal checksum does not validate application record fields. */
    for (unsigned int i = 0u; i < 4u; i++) {
        ninlil_journal *j = NULL;
        uint8_t record[38] = {1};
        record[17] = 1u;
        record[18] = 1u;
        if (i == 0u)
            record[17] = 0u;
        else if (i == 1u)
            record[16] = record[17] = 255u;
        else if (i == 2u)
            record[18] = 0u;
        else
            record[21] = 65u;
        CHECK(ninlil_journal_open(&j, app_path, 0x20000u, must_be_empty,
                                  NULL) == NINLIL_OK);
        CHECK(ninlil_journal_append(j, 1u, node_identity.identity, 32u, NULL) ==
              NINLIL_OK);
        CHECK(ninlil_journal_append(j, 2u, record, sizeof(record), NULL) ==
              NINLIL_OK);
        ninlil_journal_close(j);
        CHECK(node_application_open(app_path, 0) == NINLIL_ERR_CORRUPT);
        CHECK(unlink(app_path) == 0);
    }
    test_remove_directory(directory, a_path, b_path);
    puts("real application ledger crash boundary, duplicate, empty payload, "
         "binding and corruption PASS");
    return 0;
}
