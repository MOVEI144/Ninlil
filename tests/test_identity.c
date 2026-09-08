#define _POSIX_C_SOURCE 200809L
#include "ninlil_identity_file.h"
#include "ninlil_identity_flash.h"
#include "security_test_io.h"
#include "test_support.h"
#include <stdio.h>
#include <unistd.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int handshake(ninlil_identity *a, ninlil_identity *b,
                     uint8_t fingerprint[16])
{
    ninlil_identity_peer peers[2];
    ninlil_edhoc_config configs[2];
    ninlil_edhoc h[2];
    ninlil_session_material left, right;
    uint8_t one[NINLIL_EDHOC_MESSAGE_MAX], two[NINLIL_EDHOC_MESSAGE_MAX],
        identity[32];
    size_t n1, n2;
    CHECK(ninlil_identity_credentials(&peers[0], a, 1u, 2u, b->public_key,
                                      b->identity, 1,
                                      &configs[0]) == NINLIL_OK);
    CHECK(ninlil_identity_credentials(&peers[1], b, 2u, 1u, a->public_key,
                                      a->identity, 0,
                                      &configs[1]) == NINLIL_OK);
    CHECK(ninlil_edhoc_open(&h[0], &configs[0], 0u) == NINLIL_OK);
    CHECK(ninlil_edhoc_open(&h[1], &configs[1], 0u) == NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&h[0], NULL, 0u, 1u, one, sizeof(one), &n1) ==
          NINLIL_OK);
    CHECK(n1 <= 224u);
    CHECK(ninlil_edhoc_exchange(&h[1], one, n1, 2u, two, sizeof(two), &n2) ==
          NINLIL_OK);
    CHECK(n2 <= 224u);
    CHECK(ninlil_edhoc_exchange(&h[0], two, n2, 3u, one, sizeof(one), &n1) ==
          NINLIL_OK);
    CHECK(n1 <= 224u);
    CHECK(ninlil_edhoc_exchange(&h[1], one, n1, 4u, two, sizeof(two), &n2) ==
          NINLIL_OK);
    CHECK(n2 <= 224u);
    CHECK(ninlil_edhoc_exchange(&h[0], two, n2, 5u, one, sizeof(one), &n1) ==
          NINLIL_OK);
    CHECK(ninlil_edhoc_material(&h[0], &left, identity) == NINLIL_OK);
    CHECK(memcmp(identity, b->identity, 32u) == 0);
    CHECK(ninlil_edhoc_material(&h[1], &right, identity) == NINLIL_OK);
    CHECK(memcmp(identity, a->identity, 32u) == 0);
    CHECK(memcmp(&left, &right, sizeof(left)) == 0);
    memcpy(fingerprint, left.fingerprint, 16u);
    ninlil_edhoc_close(&h[0]);
    ninlil_edhoc_close(&h[1]);
    ninlil_secret_clear(&left, sizeof(left));
    ninlil_secret_clear(&right, sizeof(right));
    return 0;
}

int main(void)
{
    flash storage = {0};
    ninlil_flash_io flash_io = {read_flash, write_flash, erase_flash, &storage,
                                sizeof(storage.bytes)};
    ninlil_identity_flash flash_store;
    ninlil_identity_file file, competing;
    ninlil_identity_io io[2], unused;
    ninlil_identity a, b, restored;
    char directory[128], path[256], lock[256];
    uint8_t public_key[65], stable[32], first[16], next[16];
    CHECK(psa_crypto_init() == PSA_SUCCESS);
    memset(storage.bytes, 255, sizeof(storage.bytes));
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "identity.bin") == 0);
    CHECK(test_make_path(lock, sizeof(lock), directory, ".identity.lock") == 0);
    CHECK(ninlil_identity_file_open(&file, directory, &io[0]) == NINLIL_OK);
    CHECK(ninlil_identity_file_open(&competing, directory, &unused) ==
          NINLIL_ERR_BUSY);
    CHECK(ninlil_identity_flash_open(&flash_store, &flash_io, &io[1]) ==
          NINLIL_OK);
    CHECK(ninlil_identity_open(&a, io[0]) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_identity_provision(&a, io[0]) == NINLIL_OK);
    CHECK(ninlil_identity_provision(&b, io[1]) == NINLIL_OK);
    CHECK(ninlil_identity_provision(&restored, io[0]) == NINLIL_ERR_CONFLICT);
    CHECK(handshake(&a, &b, first) == 0);
    memcpy(public_key, a.public_key, 65u);
    memcpy(stable, a.identity, 32u);
    ninlil_identity_close(&a);
    ninlil_identity_file_close(&file);
    CHECK(ninlil_identity_file_open(&file, directory, &io[0]) == NINLIL_OK);
    CHECK(ninlil_identity_open(&a, io[0]) == NINLIL_OK);
    CHECK(a.generation == 1u && memcmp(public_key, a.public_key, 65u) == 0);
    /* More handshakes than typical PSA key slots: credential fetch borrows the
     * one owned key handle instead of leaking a new import per handshake. */
    for (unsigned int i = 0u; i < 40u; i++) {
        CHECK(handshake(&a, &b, next) == 0);
        CHECK(memcmp(first, next, 16u) != 0);
        memcpy(first, next, 16u);
    }
    CHECK(ninlil_identity_rotate(&a, 2u) == NINLIL_ERR_STATE);
    CHECK(ninlil_identity_rotate(&a, 1u) == NINLIL_OK && a.generation == 2u);
    CHECK(memcmp(public_key, a.public_key, 65u) != 0);
    CHECK(memcmp(stable, a.identity, 32u) == 0);
    CHECK(handshake(&a, &b, next) == 0);
    storage.fail = 1;
    CHECK(ninlil_identity_rotate(&b, 1u) == NINLIL_ERR_IO && !b.signing_key);
    storage.fail = 0;
    CHECK(ninlil_identity_open(&b, io[1]) == NINLIL_OK && b.generation == 1u);
    CHECK(ninlil_identity_rotate(&b, 1u) == NINLIL_OK && b.generation == 2u);
    storage.bytes[24] ^= 1u;
    CHECK(ninlil_identity_mark_initialized(&b) == NINLIL_ERR_CORRUPT &&
          !b.signing_key);
    CHECK(ninlil_identity_open(&b, io[1]) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_identity_provision(&b, io[1]) == NINLIL_ERR_CORRUPT);
    ninlil_identity_close(&a);
    CHECK(unlink(path) == 0);
    CHECK(ninlil_identity_open(&a, io[0]) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_identity_provision(&a, io[0]) == NINLIL_ERR_CORRUPT);
    ninlil_identity_file_close(&file);
    test_remove_directory(directory, path, lock);
    puts("persistent private identity/atomic reopen/exclusive "
         "lock/rotation/real EDHOC PASS");
    return 0;
}
