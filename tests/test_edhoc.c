#include "edhoc_cipher_suite_2.h"
#include "ninlil_control_log.h"
#include "ninlil_edhoc.h"
#include "ninlil_secure_link.h"
#include "security_test_io.h"
#include "test_support.h"
#include "test_vector_x5chain_sign_keys_suite_2.h"

#include <psa/crypto.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check line %d: %s\n", __LINE__, #x);              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#include "edhoc_test_credentials.h"

static int delivery_integration(const ninlil_session_material *material)
{
    ninlil_secure_session sessions[2];
    ninlil_counter_store counters[2];
    flash storage[2];
    ninlil_secure_mux muxes[2];
    ninlil_secure_peer peers[2][1];
    test_link transport;
    ninlil_link raw[2], links[2];
    ninlil_runtime *runtime[2] = {NULL, NULL};
    test_policy policy;
    char directory[256], paths[2][320];
    uint32_t rng = 123u;
    size_t i;
    unsigned int seq;
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(paths[0], sizeof(paths[0]), directory, "a.journal") ==
          0);
    CHECK(test_make_path(paths[1], sizeof(paths[1]), directory, "b.journal") ==
          0);
    test_link_init(&transport, 240u);
    test_policy_init(&policy, 256u, 32u);
    for (i = 0u; i < 2u; i++) {
        ninlil_security_io io = {read_flash, write_flash, erase_flash,
                                 &storage[i], sizeof(storage[i].bytes)};
        ninlil_counter_config cc;
        ninlil_config config;
        memset(&cc, 0, sizeof(cc));
        memset(storage[i].bytes, 255, sizeof(storage[i].bytes));
        storage[i].fail = 0;
        memcpy(cc.session_fingerprint, material->fingerprint, 16u);
        cc.direction = (uint8_t)i;
        cc.reservation_size = 32u;
        cc.max_counter_exclusive = 10000u;
        CHECK(ninlil_counter_open(&counters[i], &io, NINLIL_COUNTER_CREATE_NEW,
                                  &cc) == NINLIL_OK);
        CHECK(ninlil_secure_open(&sessions[i], material, &counters[i],
                                 ninlil_psa_aead(), (uint16_t)(i + 1u),
                                 (uint16_t)(2u - i), (uint8_t)i) == NINLIL_OK);
        test_link_bind(&transport, (uint8_t)i, &raw[i]);
        CHECK(ninlil_secure_mux_open(&muxes[i], raw[i], peers[i], 1u,
                                     (uint16_t)(i + 1u), test_policy_lookup,
                                     &policy, &links[i]) == NINLIL_OK);
        CHECK(ninlil_secure_mux_add(&muxes[i], (uint16_t)(2u - i),
                                    &sessions[i]) == NINLIL_OK);
        memset(&config, 0, sizeof(config));
        config.node_id = (uint16_t)(i + 1u);
        config.journal_location = paths[i];
        config.link = links[i];
        config.retry_interval_steps = 2u;
        config.max_work_per_step = 8u;
        config.random.fill = test_rng_fill;
        config.random.ctx = &rng;
        config.policy_lookup = test_policy_lookup;
        config.policy_ctx = &policy;
        CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                           &config.profile) == NINLIL_OK);
        CHECK(ninlil_open(&runtime[i], &config) == NINLIL_OK);
    }
    for (seq = 1u; seq <= 32u; seq++) {
        ninlil_submission request;
        ninlil_id id;
        ninlil_info info;
        uint8_t payload[64];
        unsigned int step, consumed = 0u;
        uint8_t side = (uint8_t)(seq % 2u), other = (uint8_t)(1u - side);
        memset(payload, (int)seq, sizeof(payload));
        ninlil_submission_defaults(&request);
        test_fill_id(&request.idempotency_key, (uint8_t)seq);
        request.target = (uint16_t)(other + 1u);
        request.service = 256u;
        request.payload = payload;
        request.payload_len = sizeof(payload);
        CHECK(ninlil_submit(runtime[side], &request, &id) == NINLIL_OK);
        test_link_drop_next(&transport, other, 1u);
        test_link_duplicate_next(&transport, side, 1u);
        for (step = 0u; step < 100u; step++) {
            ninlil_inbound inbound;
            CHECK(ninlil_step(runtime[side]) == NINLIL_OK);
            CHECK(ninlil_step(runtime[other]) == NINLIL_OK);
            if (ninlil_receive(runtime[other], &inbound) == NINLIL_OK) {
                CHECK(inbound.payload_len == sizeof(payload) &&
                      memcmp(inbound.payload, payload, sizeof(payload)) == 0);
                CHECK(ninlil_application_accept(
                          runtime[other], &inbound.message_id) == NINLIL_OK);
                consumed++;
            }
            CHECK(ninlil_query(runtime[side], &id, &info) == NINLIL_OK);
            if (info.outcome == NINLIL_OUTCOME_SATISFIED)
                break;
        }
        CHECK(step < 100u && consumed == 1u &&
              info.latest_evidence >= NINLIL_EVIDENCE_REMOTE_STORED);
    }
    for (i = 0u; i < 2u; i++) {
        ninlil_close(runtime[i]);
        ninlil_secure_mux_remove(&muxes[i], (uint16_t)(2u - i));
    }
    test_remove_directory(directory, paths[0], paths[1]);
    puts("real EDHOC material -> AES-CCM Link -> durable Core, 32 "
         "bidirectional lost-receipt deliveries PASS");
    return 0;
}

int main(void)
{
    ninlil_session_material first, second;
    ninlil_edhoc a, b;
    credential_context ca = {1, 0, 0}, cb = {0, 0, 0};
    uint8_t one[1024], two[1024], out[1024];
    size_t n1, n2, n;
    CHECK(psa_crypto_init() == PSA_SUCCESS);
    CHECK(handshake(&first, NULL) == 0 && handshake(&second, NULL) == 0);
    CHECK(memcmp(&first, &second, sizeof(first)) != 0);
    CHECK(delivery_integration(&first) == 0);
    CHECK(setup(&a, &ca) == NINLIL_OK && setup(&b, &cb) == NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1001u, one, sizeof(one), &n1) ==
          NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&b, one, n1, 1002u, two, sizeof(two), &n2) ==
          NINLIL_OK);
    ca.expired = 1;
    CHECK(ninlil_edhoc_exchange(&a, two, n2, 1003u, out, sizeof(out), &n) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(!a.opened && !a.authenticated);
    ninlil_edhoc_close(&b);
    ca.expired = 0;
    for (unsigned int failure = 0u; failure < 2u; failure++) {
        CHECK(setup(&a, &ca) == NINLIL_OK && setup(&b, &cb) == NINLIL_OK);
        CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1001u, one, sizeof(one),
                                    &n1) == NINLIL_OK);
        CHECK(ninlil_edhoc_exchange(&b, one, n1, 1002u, two, sizeof(two),
                                    &n2) == NINLIL_OK);
        if (failure == 0u)
            a.config.expected_peer[0] ^= 1u;
        else
            two[n2 - 1u] ^= 1u;
        CHECK(ninlil_edhoc_exchange(&a, two, n2, 1003u, out, sizeof(out), &n) ==
              NINLIL_ERR_UNAUTHORIZED);
        CHECK(!a.opened && !a.authenticated);
        ninlil_edhoc_close(&b);
    }
    CHECK(setup(&a, &ca) == NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1001u, one, sizeof(one), &n1) ==
          NINLIL_OK);
    for (unsigned int retry = 0u; retry < NINLIL_EDHOC_RETRY_MAX; retry++)
        CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1002u, out, sizeof(out),
                                    &n) == NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1003u, out, sizeof(out), &n) ==
          NINLIL_ERR_TIMEOUT);
    CHECK(setup(&a, &ca) == NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 999u, one, sizeof(one), &n1) ==
          NINLIL_ERR_TIMEOUT);
    CHECK(setup(&a, &ca) == NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 31001u, one, sizeof(one), &n1) ==
          NINLIL_ERR_TIMEOUT);
    ninlil_secret_clear(&first, sizeof(first));
    ninlil_secret_clear(&second, sizeof(second));
    puts("real EDHOC suite 2 mutual "
         "authentication/export/retry/freshness/expiry PASS");
    return 0;
}
