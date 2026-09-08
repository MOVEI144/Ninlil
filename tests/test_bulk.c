#include "ninlil_bulk.h"
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
static uint8_t body[NINLIL_BULK_MAX];
static ninlil_runtime *core[2];
static ninlil_bulk *bulk[2];
static ninlil_config configs[2];
static char paths[4][256];
static void reopen(void)
{
    for (unsigned int i = 0u; i < 2u; i++) {
        ninlil_bulk_close(bulk[i]);
        ninlil_close(core[i]);
        CHECK(ninlil_open(&core[i], &configs[i]) == NINLIL_OK);
        CHECK(ninlil_bulk_open(&bulk[i], paths[i + 2u], (uint16_t)(2u - i),
                               256u, i == 0u) == NINLIL_OK);
    }
}
int main(void)
{
    char directory[128];
    test_link link;
    test_policy policy;
    uint32_t rng[2] = {101u, 111u};
    ninlil_bulk_manifest m = {0};
    ninlil_bulk_status status[2];
    size_t digest_len;
    unsigned int restarts = 0u;
    ninlil_id normal = {{0}};
    int normal_submitted = 0, normal_received = 0;
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    for (unsigned int i = 0u; i < 4u; i++) {
        char name[2] = {(char)('a' + i), 0};
        CHECK(test_make_path(paths[i], sizeof(paths[i]), directory, name) == 0);
    }
    test_policy_init(&policy, 256u, 8u);
    policy.grants[1] = policy.grants[0];
    policy.grants[1].service_id = 257u;
    policy.grant_count = 2u;
    test_link_init(&link, 200u);
    for (unsigned int i = 0u; i < 2u; i++) {
        configs[i].node_id = (uint16_t)(i + 1u);
        configs[i].journal_location = paths[i];
        configs[i].random.fill = test_rng_fill;
        configs[i].random.ctx = &rng[i];
        configs[i].policy_lookup = test_policy_lookup;
        configs[i].policy_ctx = &policy;
        configs[i].max_work_per_step = 4u;
        configs[i].retry_interval_steps = 2u;
        CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                           &configs[i].profile) == NINLIL_OK);
        test_link_bind(&link, (uint8_t)i, &configs[i].link);
    }
    reopen();
    for (uint32_t i = 0u; i < sizeof(body); i++)
        body[i] = (uint8_t)(i * 37u + i / 256u);
    m.id.bytes[0] = 77u;
    m.length = sizeof(body);
    CHECK(psa_crypto_init() == PSA_SUCCESS);
    CHECK(psa_hash_compute(PSA_ALG_SHA_256, body, sizeof(body), m.sha256,
                           sizeof(m.sha256), &digest_len) == PSA_SUCCESS);
    CHECK(digest_len == 32u);
    CHECK(ninlil_bulk_begin(bulk[0], &m) == NINLIL_OK);
    CHECK(ninlil_bulk_write(bulk[0], 40u, body + 40, 40u) == NINLIL_ERR_BUSY);
    CHECK(ninlil_bulk_seal(bulk[0]) == NINLIL_ERR_STATE);
    for (uint32_t offset = 0u; offset < sizeof(body);
         offset += NINLIL_BULK_CHUNK) {
        uint16_t size = (uint16_t)(sizeof(body) - offset > NINLIL_BULK_CHUNK
                                       ? NINLIL_BULK_CHUNK
                                       : sizeof(body) - offset);
        CHECK(ninlil_bulk_write(bulk[0], offset, body + offset, size) ==
              NINLIL_OK);
        if (offset == 800u)
            reopen();
    }
    CHECK(ninlil_bulk_begin(bulk[0], &m) == NINLIL_OK);
    m.sha256[0] ^= 1u;
    CHECK(ninlil_bulk_begin(bulk[0], &m) == NINLIL_ERR_CONFLICT);
    m.sha256[0] ^= 1u;
    CHECK(ninlil_bulk_seal(bulk[0]) == NINLIL_OK);
    for (unsigned int tick = 0u; tick < 60000u; tick++) {
        for (unsigned int i = 0u; i < 2u; i++) {
            int rc = ninlil_bulk_step(bulk[i], core[i]);
            CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_CAPACITY ||
                  rc == NINLIL_ERR_BUSY);
            rc = ninlil_step(core[i]);
            CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_CAPACITY ||
                  rc == NINLIL_ERR_BUSY);
            CHECK(ninlil_bulk_query(bulk[i], &status[i]) == NINLIL_OK);
        }
        if (!normal_submitted && status[1].stored_bytes > 400u) {
            ninlil_submission s;
            ninlil_submission_defaults(&s);
            s.target = 2u;
            s.service = 257u;
            s.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
            s.payload = body;
            s.payload_len = 16u;
            s.idempotency_key.bytes[0] = 99u;
            CHECK(ninlil_submit(core[0], &s, &normal) == NINLIL_OK);
            normal_submitted = 1;
        }
        if (normal_submitted && !normal_received) {
            ninlil_inbound in;
            if (ninlil_receive_service(core[1], 257u, &in) == NINLIL_OK) {
                CHECK(!memcmp(in.message_id.bytes, normal.bytes, 16u));
                CHECK(ninlil_application_accept(core[1], &in.message_id) ==
                      NINLIL_OK);
                CHECK(!status[0].remote_stored);
                normal_received = 1;
            }
        }
        if (tick % 131u == 0u)
            test_link_drop_next(&link, 0u, 1u);
        if (tick % 113u == 0u)
            test_link_duplicate_next(&link, 1u, 1u);
        if ((restarts == 0u && status[1].stored_bytes >= 1000u) ||
            (restarts == 1u && status[1].stored_bytes >= 32000u) ||
            (restarts == 2u && status[1].ready)) {
            test_link_init(&link, 200u);
            for (unsigned int i = 0u; i < 2u; i++)
                test_link_bind(&link, (uint8_t)i, &configs[i].link);
            reopen();
            restarts++;
        }
        if (status[0].remote_stored)
            break;
    }
    fprintf(stderr, "bulk cursor=%u bytes=%u ready=%u restarts=%u normal=%d\n",
            status[0].acknowledged_frames, status[1].stored_bytes,
            status[1].ready, restarts, normal_received);
    CHECK(status[0].remote_stored && status[1].ready && restarts == 3u &&
          normal_received);
    CHECK(ninlil_bulk_collect(bulk[0]) == NINLIL_OK);
    CHECK(ninlil_bulk_collect(bulk[1]) == NINLIL_OK);
    reopen();
    for (uint32_t offset = 0u; offset < sizeof(body); offset += 256u) {
        uint8_t bytes[256];
        CHECK(ninlil_bulk_read(bulk[1], offset, bytes, sizeof(bytes)) ==
              NINLIL_OK);
        CHECK(!memcmp(bytes, body + offset, sizeof(bytes)));
    }
    CHECK(ninlil_bulk_write(bulk[1], 0u, body, 40u) == NINLIL_OK);
    body[0] ^= 1u;
    CHECK(ninlil_bulk_write(bulk[1], 0u, body, 40u) == NINLIL_ERR_CONFLICT);
    for (unsigned int i = 0u; i < 2u; i++) {
        ninlil_bulk_close(bulk[i]);
        ninlil_close(core[i]);
    }
    for (unsigned int i = 0u; i < 4u; i++)
        CHECK(unlink(paths[i]) == 0);
    CHECK(rmdir(directory) == 0);
    puts("64 KiB: loss, duplicate, import/transfer/completion restart, "
         "collection, normal-traffic coexistence PASS");
    return 0;
}
