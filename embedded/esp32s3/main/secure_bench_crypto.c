#include "edhoc_cipher_suite_2.h"
#include "secure_bench.h"
#include <psa/crypto.h>
#include <string.h>

static uint8_t private_key[32], public_key[65], remote_key[65];
static uint8_t remote_identity[32];
static int generated, verified;
static uint16_t remote_node;

int ninlil_bench_identity(uint8_t output[65])
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    psa_status_t rc;
    size_t length = 0u;
    if (generated) {
        memcpy(output, public_key, 65u);
        return NINLIL_OK;
    }
    if (psa_crypto_init() != PSA_SUCCESS)
        return NINLIL_ERR_IO;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&a, 256u);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&a, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    rc = psa_generate_key(&a, &key);
    if (rc == PSA_SUCCESS)
        rc = psa_export_key(key, private_key, sizeof(private_key), &length);
    if (rc == PSA_SUCCESS && length != sizeof(private_key))
        rc = PSA_ERROR_CORRUPTION_DETECTED;
    if (rc == PSA_SUCCESS)
        rc =
            psa_export_public_key(key, public_key, sizeof(public_key), &length);
    (void)psa_destroy_key(key);
    psa_reset_key_attributes(&a);
    if (rc != PSA_SUCCESS || length != sizeof(public_key)) {
        ninlil_secret_clear(private_key, sizeof(private_key));
        return NINLIL_ERR_IO;
    }
    generated = 1;
    memcpy(output, public_key, 65u);
    return NINLIL_OK;
}

static int fetch(void *ctx, struct edhoc_auth_creds *out)
{
    (void)ctx;
    out->label = EDHOC_COSE_HEADER_KID;
    out->key_id.encode_type = EDHOC_ENCODE_TYPE_INTEGER;
    out->key_id.key_id_int = CONFIG_NINLIL_NODE_ID;
    out->key_id.cred = public_key;
    out->key_id.cred_len = sizeof(public_key);
    out->key_id.cred_is_cbor = false;
    return edhoc_cipher_suite_2_key_import(NULL, EDHOC_KT_SIGNATURE,
                                           private_key, sizeof(private_key),
                                           out->priv_key_id);
}

static int verify(void *ctx, struct edhoc_auth_creds *cred, const uint8_t **key,
                  size_t *length)
{
    (void)ctx;
    if (cred->label != EDHOC_COSE_HEADER_KID ||
        cred->key_id.encode_type != EDHOC_ENCODE_TYPE_INTEGER ||
        cred->key_id.key_id_int != remote_node)
        return EDHOC_ERROR_CREDENTIALS_FAILURE;
    cred->key_id.cred = remote_key;
    cred->key_id.cred_len = sizeof(remote_key);
    cred->key_id.cred_is_cbor = false;
    *key = remote_key;
    *length = sizeof(remote_key);
    verified = 1;
    return EDHOC_SUCCESS;
}

static int identity(void *ctx, uint8_t output[32])
{
    (void)ctx;
    if (!verified)
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(output, remote_identity, 32u);
    return NINLIL_OK;
}

int ninlil_bench_handshake(ninlil_edhoc *h, const uint8_t peer[65],
                           uint16_t node, uint64_t now_ms)
{
    ninlil_edhoc_config c = {0};
    size_t length;
    if (!generated || peer[0] != 4u || node < 1u || node > 3u ||
        node == CONFIG_NINLIL_NODE_ID)
        return NINLIL_ERR_STATE;
    memcpy(remote_key, peer, sizeof(remote_key));
    if (psa_hash_compute(PSA_ALG_SHA_256, peer, 65u, remote_identity, 32u,
                         &length) != PSA_SUCCESS ||
        length != 32u)
        return NINLIL_ERR_IO;
    verified = 0;
    remote_node = node;
    c.credentials.fetch = fetch;
    c.credentials.verify = verify;
    c.peer_identity = identity;
    memcpy(c.expected_peer, remote_identity, 32u);
    c.initiator = CONFIG_NINLIL_NODE_ID < node ? 1u : 0u;
    c.connection_id = (int8_t)CONFIG_NINLIL_NODE_ID;
    return ninlil_edhoc_open(h, &c, now_ms);
}
