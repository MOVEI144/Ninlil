#define _POSIX_C_SOURCE 200809L
#include "ninlil_bulk.h"
#include "node_example.h"
#include "test_support.h"
#include <psa/crypto.h>
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
int main(void)
{
    char directory[128], prior[4096];
    ninlil_runtime *core;
    ninlil_config c = {0};
    test_policy policy;
    test_link link;
    uint32_t rng = 88u;
    uint8_t data[52] = {1u}, out[128], object[40] = {7u}, chunk[44] = {0};
    size_t size;
    CHECK(getcwd(prior, sizeof(prior)) != NULL);
    CHECK(test_make_directory(directory, sizeof(directory)) == 0 &&
          chdir(directory) == 0);
    test_policy_init(&policy, 256u, 8u);
    policy.session_membership_epoch = 0u;
    test_link_init(&link, 200u);
    test_link_bind(&link, 0u, &c.link);
    c.journal_location = "core";
    c.node_id = 1u;
    c.max_work_per_step = 4u;
    c.random = (ninlil_random){test_rng_fill, &rng};
    c.policy_lookup = test_policy_lookup;
    c.policy_ctx = &policy;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &c.profile) == NINLIL_OK);
    CHECK(ninlil_open(&core, &c) == NINLIL_OK);
    CHECK(node_bulk_command('O', (uint8_t[3]){0u, 2u, 1u}, 3u, out, &size) ==
          NINLIL_OK);
    data[19] = 40u;
    CHECK(psa_crypto_init() == PSA_SUCCESS);
    CHECK(psa_hash_compute(PSA_ALG_SHA_256, object, sizeof(object), data + 20,
                           32u, &size) == PSA_SUCCESS);
    CHECK(node_bulk_command('M', data, sizeof(data), out, &size) == NINLIL_OK);
    memcpy(chunk + 4, object, sizeof(object));
    CHECK(node_bulk_command('W', chunk, sizeof(chunk), out, &size) ==
          NINLIL_OK);
    CHECK(node_bulk_command('V', NULL, 0u, out, &size) == NINLIL_OK);
    CHECK(node_bulk_step(core) == NINLIL_OK &&
          ninlil_health(core) == NINLIL_OK);
    CHECK(node_bulk_command('J', NULL, 0u, out, &size) == NINLIL_OK &&
          size == 65u);
    CHECK(out[59] == 1u && out[60] == 0u &&
          out[64] == 242u); /* -14: session not ready */
    policy.session_membership_epoch = policy.membership_epoch;
    CHECK(node_bulk_step(core) == NINLIL_OK);
    CHECK(node_bulk_command('J', NULL, 0u, out, &size) == NINLIL_OK);
    CHECK(out[60] == 0u && memcmp(out + 61, (uint8_t[4]){0}, 4u) == 0);
    node_bulk_close();
    ninlil_close(core);
    {
        ninlil_bulk *bad = NULL;
        ninlil_bulk_manifest manifest = {0};
        ninlil_bulk_status state;
        manifest.id.bytes[0] = 9u;
        manifest.length = sizeof(object);
        CHECK(ninlil_bulk_open(&bad, "bad_digest", 2u, 256u, 0) == NINLIL_OK);
        CHECK(ninlil_bulk_begin(bad, &manifest) == NINLIL_OK);
        CHECK(ninlil_bulk_write(bad, 0u, object, sizeof(object)) == NINLIL_OK);
        CHECK(ninlil_bulk_seal(bad) == NINLIL_ERR_CONFLICT);
        CHECK(ninlil_bulk_query(bad, &state) == NINLIL_OK && !state.ready &&
              !state.remote_stored);
        ninlil_bulk_close(bad);
        CHECK(ninlil_bulk_open(&bad, "bad_digest", 3u, 256u, 0) ==
                  NINLIL_ERR_CORRUPT &&
              !bad);
        CHECK(ninlil_bulk_open(&bad, "bad_digest", 2u, 256u, 0) == NINLIL_OK);
        CHECK(ninlil_bulk_seal(bad) == NINLIL_ERR_CONFLICT);
        ninlil_bulk_close(bad);
        CHECK(unlink("bad_digest") == 0);
    }
    CHECK(unlink("node_bulk") == 0 && unlink("core") == 0 &&
          chdir(prior) == 0 && rmdir(directory) == 0);
    puts("Reference bulk owner retains the object and resumes after "
         "cold-session admission blocking PASS");
    return 0;
}
