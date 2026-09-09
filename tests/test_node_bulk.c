#include "test_node_bulk.h"
#include "ninlil_bulk.h"
#include "test_support.h"
#include <psa/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "secure bulk %d: %s\n", __LINE__, #x);             \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
void test_node_bulk(ninlil_node *source, ninlil_node **receiver,
                    void (*tick)(void), void (*restart_receiver)(void))
{
    char dir[128], a[256], b[256];
    ninlil_bulk *send, *recv;
    ninlil_bulk_manifest m = {0};
    ninlil_bulk_status state;
    uint8_t payload[120], actual[120];
    size_t length;
    int restarted = 0;
    REQUIRE(test_make_directory(dir, sizeof(dir)) == 0);
    REQUIRE(test_make_path(a, sizeof(a), dir, "source") == 0);
    REQUIRE(test_make_path(b, sizeof(b), dir, "receiver") == 0);
    for (unsigned int i = 0u; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;
    m.id.bytes[0] = 111u;
    m.length = sizeof(payload);
    REQUIRE(psa_hash_compute(PSA_ALG_SHA_256, payload, sizeof(payload),
                             m.sha256, 32u, &length) == PSA_SUCCESS);
    REQUIRE(ninlil_bulk_open(&send, a, 3u, 256u, 1) == NINLIL_OK);
    REQUIRE(ninlil_bulk_open(&recv, b, 1u, 256u, 0) == NINLIL_OK);
    REQUIRE(ninlil_bulk_begin(send, &m) == NINLIL_OK);
    for (uint32_t offset = 0u; offset < sizeof(payload); offset += 40u)
        REQUIRE(ninlil_bulk_write(send, offset, payload + offset, 40u) ==
                NINLIL_OK);
    REQUIRE(ninlil_bulk_seal(send) == NINLIL_OK);
    for (unsigned int i = 0u; i < 60000u; i++) {
        int rc = ninlil_bulk_step(send, ninlil_node_core(source));
        REQUIRE(rc == NINLIL_OK || rc == NINLIL_ERR_BUSY ||
                rc == NINLIL_ERR_CAPACITY || rc == NINLIL_ERR_STATE);
        rc = ninlil_bulk_step(recv, ninlil_node_core(*receiver));
        REQUIRE(rc == NINLIL_OK || rc == NINLIL_ERR_CAPACITY);
        REQUIRE(ninlil_bulk_query(recv, &state) == NINLIL_OK);
        if (!restarted && state.stored_bytes >= 40u && !state.ready) {
            ninlil_bulk_close(recv);
            restart_receiver();
            REQUIRE(ninlil_bulk_open(&recv, b, 1u, 256u, 0) == NINLIL_OK);
            restarted = 1;
        }
        tick();
        REQUIRE(ninlil_bulk_query(send, &state) == NINLIL_OK);
        if (state.remote_stored)
            break;
    }
    REQUIRE(state.remote_stored && restarted);
    REQUIRE(ninlil_bulk_read(recv, 0u, actual, sizeof(actual)) == NINLIL_OK);
    REQUIRE(!memcmp(actual, payload, sizeof(actual)));
    ninlil_bulk_close(send);
    ninlil_bulk_close(recv);
    test_remove_directory(dir, a, b);
    puts("Autonomous authenticated Relay path bulk transfer PASS");
}
