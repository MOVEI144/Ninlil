#include "ninlil_routed.h"
#include "ninlil_secure.h"

#include <psa/crypto.h>
#include <string.h>

static int crypt(void *ctx, int encrypt, const uint8_t key[16],
                 const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                 const uint8_t *input, size_t input_len, uint8_t *output,
                 size_t capacity, size_t *length)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t handle = 0;
    psa_status_t result;
    psa_status_t destroyed;
    psa_algorithm_t algorithm = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 8);

    (void)ctx;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128u);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, algorithm);
    result = psa_import_key(&attributes, key, 16u, &handle);
    psa_reset_key_attributes(&attributes);
    if (result != PSA_SUCCESS)
        return NINLIL_ERR_IO;
    if (encrypt)
        result = psa_aead_encrypt(handle, algorithm, nonce, 13u, aad, aad_len,
                                  input, input_len, output, capacity, length);
    else
        result = psa_aead_decrypt(handle, algorithm, nonce, 13u, aad, aad_len,
                                  input, input_len, output, capacity, length);
    destroyed = psa_destroy_key(handle);
    if (result == PSA_ERROR_INVALID_SIGNATURE)
        return NINLIL_ERR_UNAUTHORIZED;
    return result == PSA_SUCCESS && destroyed == PSA_SUCCESS ? NINLIL_OK
                                                             : NINLIL_ERR_IO;
}

ninlil_aead ninlil_psa_aead(void)
{
    ninlil_aead backend = {crypt, NULL};
    return backend;
}

int ninlil_psa_packet_digest(const uint8_t *data, size_t length, uint8_t id[16])
{
    uint8_t hash[32];
    size_t size = 0u;
    psa_status_t rc;
    if (!data || !id || length == 0u || length > NINLIL_RELAY_CIPHERTEXT_MAX)
        return NINLIL_ERR_INVALID;
    rc = psa_hash_compute(PSA_ALG_SHA_256, data, length, hash, sizeof(hash),
                          &size);
    if (rc == PSA_SUCCESS && size == sizeof(hash))
        memcpy(id, hash, 16u);
    ninlil_secret_clear(hash, sizeof(hash));
    return rc == PSA_SUCCESS && size == sizeof(hash) ? NINLIL_OK
                                                     : NINLIL_ERR_IO;
}
