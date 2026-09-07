#include "ninlil_secure.h"

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

#include "security_test_io.h"

int main(void)
{
    flash f[2];
    ninlil_counter_store counters[2];
    ninlil_secure_session a, b;
    ninlil_session_material material;
    uint8_t frames[70][NINLIL_SECURE_FRAME_MAX];
    uint8_t tampered[NINLIL_SECURE_FRAME_MAX], output[200];
    const uint8_t input[] = {1, 2, 3, 4};
    size_t sizes[70], written, i;

    CHECK(psa_crypto_init() == PSA_SUCCESS);
    memset(&material, 0x42, sizeof(material));
    material.keys[1][0] = 0x43;
    for (i = 0; i < 2; i++) {
        ninlil_security_io io = {read_flash, write_flash, erase_flash, &f[i],
                                 sizeof(f[i].bytes)};
        ninlil_counter_config config;
        memset(&config, 0, sizeof(config));
        memset(f[i].bytes, 255, sizeof(f[i].bytes));
        f[i].fail = 0;
        memcpy(config.session_fingerprint, material.fingerprint, 16u);
        config.direction = (uint8_t)i;
        config.reservation_size = 4u;
        config.max_counter_exclusive = 1000u;
        CHECK(ninlil_counter_open(&counters[i], &io, NINLIL_COUNTER_CREATE_NEW,
                                  &config) == NINLIL_OK);
    }
    CHECK(ninlil_secure_open(&a, &material, &counters[0], ninlil_psa_aead(), 1u,
                             2u, 0u) == NINLIL_OK);
    CHECK(ninlil_secure_open(&b, &material, &counters[1], ninlil_psa_aead(), 2u,
                             1u, 1u) == NINLIL_OK);
    for (i = 0; i < 70; i++)
        CHECK(ninlil_secure_seal(&a, input, sizeof(input), frames[i],
                                 sizeof(frames[i]), &sizes[i]) == NINLIL_OK);
    // Every tampered byte must fail without publishing plaintext or advancing
    // the replay window, including a forged high counter and the tag itself.
    for (i = 0; i < sizes[0]; i++) {
        memcpy(tampered, frames[0], sizes[0]);
        tampered[i] ^= 1u;
        memset(output, 0xA5, sizeof(output));
        written = 777u;
        CHECK(ninlil_secure_unseal(&b, tampered, sizes[0], output,
                                   sizeof(output), &written) != NINLIL_OK);
        CHECK(written == 777u && output[0] == 0xA5 && b.rx_bitmap == 0u);
    }
    CHECK(ninlil_secure_unseal(&b, frames[2], sizes[2], output, sizeof(output),
                               &written) == NINLIL_OK);
    CHECK(written == sizeof(input) &&
          memcmp(output, input, sizeof(input)) == 0);
    CHECK(ninlil_secure_unseal(&b, frames[0], sizes[0], output, sizeof(output),
                               &written) == NINLIL_OK);
    CHECK(ninlil_secure_unseal(&b, frames[0], sizes[0], output, sizeof(output),
                               &written) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_secure_unseal(&b, frames[69], sizes[69], output,
                               sizeof(output), &written) == NINLIL_OK);
    CHECK(ninlil_secure_unseal(&b, frames[1], sizes[1], output, sizeof(output),
                               &written) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_secure_unseal(&a, frames[68], sizes[68], output,
                               sizeof(output),
                               &written) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_secure_seal(&b, input, sizeof(input), tampered,
                             sizeof(tampered), &written) == NINLIL_OK);
    CHECK(ninlil_secure_unseal(&a, tampered, written, output, sizeof(output),
                               &written) == NINLIL_OK);
    CHECK(ninlil_secure_seal_control(&b, input, sizeof(input), tampered,
                                     sizeof(tampered), &written) == NINLIL_OK);
    CHECK(ninlil_secure_unseal(&a, tampered, written, output, sizeof(output),
                               &written) == NINLIL_ERR_INVALID);
    tampered[31] = 0u;
    CHECK(ninlil_secure_unseal(&a, tampered, written, output, sizeof(output),
                               &written) != NINLIL_OK);
    tampered[31] = 1u;
    CHECK(ninlil_secure_unseal_control(&a, tampered, written, output,
                                       sizeof(output), &written) == NINLIL_OK);
    f[0].fail = 1;
    // Finish the reservation; the next durable reservation must close TX.
    CHECK(ninlil_secure_seal(&a, input, sizeof(input), tampered,
                             sizeof(tampered), &written) == NINLIL_OK);
    CHECK(ninlil_secure_seal(&a, input, sizeof(input), tampered,
                             sizeof(tampered), &written) == NINLIL_OK);
    memset(tampered, 0xA5, sizeof(tampered));
    written = 777u;
    CHECK(ninlil_secure_seal(&a, input, sizeof(input), tampered,
                             sizeof(tampered), &written) != NINLIL_OK);
    CHECK(a.ready == 0u && written == 777u && tampered[0] == 0xA5);
    ninlil_secure_close(&b);
    CHECK(ninlil_secure_unseal(&b, frames[68], sizes[68], output,
                               sizeof(output), &written) == NINLIL_ERR_INVALID);
    ninlil_secret_clear(&material, sizeof(material));
    puts("secure envelope tamper/replay/direction/storage-failure PASS");
    return 0;
}
