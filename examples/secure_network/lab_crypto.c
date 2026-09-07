#include "lab_crypto.h"
#include "edhoc_cipher_suite_2.h"
#include "ninlil_control_fragment.h"
#include <psa/crypto.h>
#include <string.h>

typedef struct credentials {
    lab_identity *local;
    lab_identity *peer;
    uint8_t verified;
} credentials;

int lab_identity_create(lab_identity *id, uint16_t node)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    size_t length;
    psa_status_t rc;
    memset(id, 0, sizeof(*id));
    psa_set_key_type(&attributes,
                     PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256u);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    rc = psa_generate_key(&attributes, &key);
    if (rc == PSA_SUCCESS)
        rc = psa_export_key(key, id->private_key, sizeof(id->private_key),
                            &length);
    if (rc == PSA_SUCCESS)
        rc = psa_export_public_key(key, id->public_key, sizeof(id->public_key),
                                   &length);
    if (rc == PSA_SUCCESS)
        rc = psa_hash_compute(PSA_ALG_SHA_256, id->public_key,
                              sizeof(id->public_key), id->identity,
                              sizeof(id->identity), &length);
    (void)psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);
    id->node = node;
    return rc == PSA_SUCCESS ? NINLIL_OK : NINLIL_ERR_IO;
}

static int fetch(void *ctx, struct edhoc_auth_creds *out)
{
    credentials *c = ctx;
    out->label = EDHOC_COSE_HEADER_KID;
    out->key_id.encode_type = EDHOC_ENCODE_TYPE_INTEGER;
    out->key_id.key_id_int = c->local->node;
    out->key_id.cred = c->local->public_key;
    out->key_id.cred_len = sizeof(c->local->public_key);
    out->key_id.cred_is_cbor = false;
    return edhoc_cipher_suite_2_key_import(
        NULL, EDHOC_KT_SIGNATURE, c->local->private_key,
        sizeof(c->local->private_key), out->priv_key_id);
}

static int verify(void *ctx, struct edhoc_auth_creds *cred, const uint8_t **key,
                  size_t *length)
{
    credentials *c = ctx;
    if (cred->label != EDHOC_COSE_HEADER_KID ||
        cred->key_id.encode_type != EDHOC_ENCODE_TYPE_INTEGER ||
        cred->key_id.key_id_int != c->peer->node)
        return EDHOC_ERROR_CREDENTIALS_FAILURE;
    cred->key_id.cred = c->peer->public_key;
    cred->key_id.cred_len = sizeof(c->peer->public_key);
    cred->key_id.cred_is_cbor = false;
    *key = c->peer->public_key;
    *length = sizeof(c->peer->public_key);
    c->verified = 1u;
    return EDHOC_SUCCESS;
}

static int peer_identity(void *ctx, uint8_t id[32])
{
    credentials *c = ctx;
    if (!c->verified)
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(id, c->peer->identity, 32u);
    return NINLIL_OK;
}

static int open_handshake(ninlil_edhoc *h, credentials *c, uint8_t initiator)
{
    ninlil_edhoc_config config;
    memset(&config, 0, sizeof(config));
    config.credentials.fetch = fetch;
    config.credentials.verify = verify;
    config.credential_ctx = c;
    config.peer_identity = peer_identity;
    memcpy(config.expected_peer, c->peer->identity, 32u);
    config.initiator = initiator;
    config.connection_id = (int8_t)c->local->node;
    return ninlil_edhoc_open(h, &config, 1000u);
}

static int fragment_roundtrip(uint8_t kind, uint8_t *message, size_t length)
{
    ninlil_control_reassembly state;
    uint8_t frame[240], output[NINLIL_EDHOC_MESSAGE_MAX];
    size_t count = (length + NINLIL_CONTROL_FRAGMENT_BODY - 1u) /
                   NINLIL_CONTROL_FRAGMENT_BODY;
    size_t written = 0u, i;
    int rc = NINLIL_ERR_EMPTY;
    ninlil_control_reassembly_clear(&state);
    for (i = count; i > 0u; i--) {
        size_t size =
            ninlil_control_fragment(123u, kind, message, length,
                                    (uint8_t)(i - 1u), frame, sizeof(frame));
        if (!size)
            return NINLIL_ERR_INVALID;
        rc = ninlil_control_reassemble(&state, frame, size, 1000u, output,
                                       sizeof(output), &written);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_EMPTY)
            return rc;
    }
    if (rc != NINLIL_OK || written != length ||
        memcmp(message, output, length) != 0)
        return NINLIL_ERR_CORRUPT;
    memcpy(message, output, length);
    return NINLIL_OK;
}

int lab_handshake(lab_identity *a, lab_identity *b,
                  ninlil_session_material *e2e, ninlil_session_material *hop)
{
    ninlil_edhoc h[2];
    credentials c[2] = {{a, b, 0u}, {b, a, 0u}};
    uint8_t messages[2][NINLIL_EDHOC_MESSAGE_MAX], id[32];
    size_t length = 0u, output = 0u;
    ninlil_session_material other;
    unsigned int step;
    int rc = open_handshake(&h[0], &c[0], 1u);
    if (rc != NINLIL_OK)
        return rc;
    rc = open_handshake(&h[1], &c[1], 0u);
    for (step = 0u; step < 5u && rc == NINLIL_OK; step++) {
        unsigned int side = step % 2u;
        rc = ninlil_edhoc_exchange(&h[side], step ? messages[1u - side] : NULL,
                                   length, 1001u + step, messages[side],
                                   sizeof(messages[side]), &output);
        if (rc == NINLIL_OK && output != 0u)
            rc = fragment_roundtrip((uint8_t)(step + 1u), messages[side],
                                    output);
        length = output;
    }
    if (rc == NINLIL_OK)
        rc = ninlil_edhoc_material(&h[0], e2e, id);
    if (rc == NINLIL_OK)
        rc = ninlil_edhoc_material(&h[1], &other, id);
    if (rc == NINLIL_OK && memcmp(e2e, &other, sizeof(other)) != 0)
        rc = NINLIL_ERR_CONFLICT;
    if (rc == NINLIL_OK)
        rc = ninlil_edhoc_hop_material(&h[0], hop);
    ninlil_edhoc_close(&h[0]);
    ninlil_edhoc_close(&h[1]);
    ninlil_secret_clear(messages, sizeof(messages));
    ninlil_secret_clear(&other, sizeof(other));
    return rc;
}
