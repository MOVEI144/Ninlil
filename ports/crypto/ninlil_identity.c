#include "ninlil_identity.h"
#include <string.h>

static int digest(const uint8_t *data, size_t length, uint8_t out[32])
{
    size_t written = 0u;
    return psa_hash_compute(PSA_ALG_SHA_256, data, length, out, 32u,
                            &written) == PSA_SUCCESS &&
                   written == 32u
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}

static int import_record(ninlil_identity *id, const uint8_t *record)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    uint8_t hash[32];
    size_t i, length = 0u;
    int rc = digest(record, 76u, hash);
    if (rc != NINLIL_OK)
        return rc;
    if ((memcmp(record, "NI\002\000", 4u) != 0 &&
         (memcmp(record, "NI\003", 3u) != 0 || record[3] > 1u) &&
         (memcmp(record, "NI\004", 3u) != 0 || record[3] != 3u) &&
         (memcmp(record, "NI\005", 3u) != 0 ||
          (record[3] != 5u && record[3] != 7u))) ||
        memcmp(hash, record + 76, 32u) != 0)
        return NINLIL_ERR_CORRUPT;
    /* Old NIv2 owners are conservatively treated as already provisioned. */
    id->initialized = record[2] == 2u ? 1u : record[3];
    for (i = 4u; i < 12u; i++)
        id->generation = (id->generation << 8) | record[i];
    memcpy(id->identity, record + 12, 32u);
    if (!id->generation || memcmp(id->identity, (uint8_t[32]){0}, 32u) == 0)
        return NINLIL_ERR_CORRUPT;
    psa_set_key_type(&attributes,
                     PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    if (psa_import_key(&attributes, record + 44, 32u, &id->signing_key) !=
        PSA_SUCCESS)
        rc = NINLIL_ERR_CORRUPT;
    psa_reset_key_attributes(&attributes);
    if (rc == NINLIL_OK &&
        (psa_export_public_key(id->signing_key, id->public_key, 65u, &length) !=
             PSA_SUCCESS ||
         length != 65u))
        rc = NINLIL_ERR_IO;
    if (rc == NINLIL_OK)
        rc = digest(id->public_key, 65u, id->fingerprint);
    return rc;
}

void ninlil_identity_close(ninlil_identity *id)
{
    if (id) {
        if (id->signing_key)
            (void)psa_destroy_key(id->signing_key);
        ninlil_secret_clear(id, sizeof(*id));
    }
}

int ninlil_identity_open(ninlil_identity *id, ninlil_identity_io io)
{
    uint8_t record[NINLIL_IDENTITY_RECORD_SIZE];
    int rc;
    if (!id)
        return NINLIL_ERR_INVALID;
    memset(id, 0, sizeof(*id));
    if (!io.read || !io.commit)
        return NINLIL_ERR_INVALID;
    id->io = io;
    rc = io.read(io.ctx, record);
    if (rc == NINLIL_OK)
        rc = import_record(id, record);
    ninlil_secret_clear(record, sizeof(record));
    if (rc != NINLIL_OK)
        ninlil_identity_close(id);
    return rc;
}

static int generate_record(uint8_t record[NINLIL_IDENTITY_RECORD_SIZE],
                           uint64_t generation, const uint8_t identity[32],
                           uint8_t initialized)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    psa_status_t status;
    size_t i, length = 0u;
    memset(record, 0, NINLIL_IDENTITY_RECORD_SIZE);
    memcpy(record,
           initialized & 4u    ? "NI\005"
           : initialized == 3u ? "NI\004"
                               : "NI\003",
           3u);
    record[3] = initialized;
    if (identity)
        memcpy(record + 12, identity, 32u);
    else if (psa_generate_random(record + 12, 32u) != PSA_SUCCESS)
        return NINLIL_ERR_IO;
    if (memcmp(record + 12, (uint8_t[32]){0}, 32u) == 0)
        return NINLIL_ERR_IO;
    for (i = 0u; i < 8u; i++)
        record[11u - i] = (uint8_t)(generation >> (8u * i));
    psa_set_key_type(&attributes,
                     PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256u);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_generate_key(&attributes, &key);
    if (status == PSA_SUCCESS)
        status = psa_export_key(key, record + 44, 32u, &length);
    if (key)
        (void)psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);
    return status == PSA_SUCCESS && length == 32u
               ? digest(record, 76u, record + 76)
               : NINLIL_ERR_IO;
}

static int persist(ninlil_identity *id, ninlil_identity_io io,
                   const uint8_t record[NINLIL_IDENTITY_RECORD_SIZE])
{
    uint8_t verified[NINLIL_IDENTITY_RECORD_SIZE];
    int rc = io.commit(io.ctx, record);
    if (rc == NINLIL_OK)
        rc = io.read(io.ctx, verified);
    if (rc == NINLIL_OK && memcmp(record, verified, sizeof(verified)) != 0)
        rc = NINLIL_ERR_CORRUPT;
    ninlil_identity_close(id);
    if (rc == NINLIL_OK) {
        id->io = io;
        rc = import_record(id, record);
        if (rc != NINLIL_OK)
            ninlil_identity_close(id);
    }
    ninlil_secret_clear(verified, sizeof(verified));
    return rc;
}

static int replace(ninlil_identity *id, ninlil_identity_io io,
                   uint64_t generation, const uint8_t identity[32])
{
    uint8_t record[NINLIL_IDENTITY_RECORD_SIZE];
    int rc = generate_record(record, generation, identity, id->initialized);
    if (rc == NINLIL_OK)
        rc = persist(id, io, record);
    ninlil_secret_clear(record, sizeof(record));
    return rc;
}

static int mark(ninlil_identity *id, uint8_t flags)
{
    ninlil_identity stored = {0};
    uint8_t record[NINLIL_IDENTITY_RECORD_SIZE];
    int rc;
    if (!id || !id->signing_key || id->generation == UINT64_MAX)
        return NINLIL_ERR_STATE;
    rc = id->io.read(id->io.ctx, record);
    if (rc == NINLIL_OK)
        rc = import_record(&stored, record);
    if (rc == NINLIL_OK &&
        (stored.generation != id->generation ||
         memcmp(stored.identity, id->identity, 32u) != 0 ||
         memcmp(stored.public_key, id->public_key, 65u) != 0))
        rc = NINLIL_ERR_CONFLICT;
    if (rc == NINLIL_OK && (stored.initialized & flags) != flags) {
        record[3] |= flags;
        record[2] = record[3] & 4u ? 5u : record[3] == 3u ? 4u : 3u;
        for (unsigned int i = 0u; i < 8u; i++)
            record[11u - i] = (uint8_t)((id->generation + 1u) >> (8u * i));
        rc = digest(record, 76u, record + 76);
        if (rc == NINLIL_OK)
            rc = persist(id, id->io, record);
    }
    ninlil_identity_close(&stored);
    ninlil_secret_clear(record, sizeof(record));
    if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_CORRUPT ||
        rc == NINLIL_ERR_FAULT)
        ninlil_identity_close(id);
    return rc;
}

int ninlil_identity_mark_initialized(ninlil_identity *id)
{
    return mark(id, 1u);
}
int ninlil_identity_mark_deployed(ninlil_identity *id)
{
    return id && (id->initialized & 1u) ? mark(id, 3u) : NINLIL_ERR_STATE;
}
int ninlil_identity_mark_root(ninlil_identity *id)
{
    return id && (id->initialized & 1u) ? mark(id, 5u) : NINLIL_ERR_STATE;
}

int ninlil_identity_provision(ninlil_identity *id, ninlil_identity_io io)
{
    ninlil_identity existing = {0};
    int rc;
    if (!id || !io.read || !io.commit)
        return NINLIL_ERR_INVALID;
    rc = ninlil_identity_open(&existing, io);
    ninlil_identity_close(&existing);
    if (rc != NINLIL_ERR_EMPTY)
        return rc == NINLIL_OK ? NINLIL_ERR_CONFLICT : rc;
    memset(id, 0, sizeof(*id));
    return replace(id, io, 1u, NULL);
}

int ninlil_identity_rotate(ninlil_identity *id, uint64_t expected)
{
    ninlil_identity stored = {0};
    int rc;
    if (!id || !id->signing_key || !expected || expected != id->generation ||
        expected == UINT64_MAX)
        return NINLIL_ERR_STATE;
    rc = ninlil_identity_open(&stored, id->io);
    if (rc == NINLIL_OK &&
        (stored.generation != expected ||
         memcmp(stored.identity, id->identity, 32u) != 0 ||
         memcmp(stored.public_key, id->public_key, 65u) != 0))
        rc = NINLIL_ERR_CONFLICT;
    ninlil_identity_close(&stored);
    if (rc != NINLIL_OK) {
        if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_CORRUPT ||
            rc == NINLIL_ERR_FAULT)
            ninlil_identity_close(id);
        return rc;
    }
    return replace(id, id->io, expected + 1u, id->identity);
}

static int fetch(void *ctx, struct edhoc_auth_creds *out)
{
    ninlil_identity_peer *p = ctx;
    _Static_assert(sizeof(p->local->signing_key) <= sizeof(out->priv_key_id),
                   "PSA key identifier fits");
    if (!p->local->signing_key)
        return EDHOC_ERROR_CREDENTIALS_FAILURE;
    out->label = EDHOC_COSE_HEADER_KID;
    out->key_id.encode_type = EDHOC_ENCODE_TYPE_INTEGER;
    out->key_id.key_id_int = p->local_node;
    out->key_id.cred = p->local->public_key;
    out->key_id.cred_len = 65u;
    out->key_id.cred_is_cbor = false;
    memcpy(out->priv_key_id, &p->local->signing_key,
           sizeof(p->local->signing_key));
    return EDHOC_SUCCESS;
}

static int verify(void *ctx, struct edhoc_auth_creds *cred, const uint8_t **key,
                  size_t *length)
{
    ninlil_identity_peer *p = ctx;
    if (cred->label != EDHOC_COSE_HEADER_KID ||
        cred->key_id.encode_type != EDHOC_ENCODE_TYPE_INTEGER ||
        cred->key_id.key_id_int != p->peer_node)
        return EDHOC_ERROR_CREDENTIALS_FAILURE;
    cred->key_id.cred = p->public_key;
    cred->key_id.cred_len = 65u;
    cred->key_id.cred_is_cbor = false;
    *key = p->public_key;
    *length = 65u;
    p->verified = 1u;
    return EDHOC_SUCCESS;
}

static int peer_identity(void *ctx, uint8_t identity[32])
{
    ninlil_identity_peer *p = ctx;
    if (!p->verified)
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(identity, p->identity, 32u);
    return NINLIL_OK;
}

int ninlil_identity_credentials(ninlil_identity_peer *p,
                                const ninlil_identity *local, uint16_t node,
                                uint16_t remote, const uint8_t trusted[65],
                                const uint8_t trusted_identity[32],
                                int initiator, ninlil_edhoc_config *config)
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    psa_status_t status;
    if (!p || !local || !local->signing_key || !config || !trusted ||
        !trusted_identity || !node || node == UINT16_MAX || !remote ||
        remote == UINT16_MAX || node == remote)
        return NINLIL_ERR_INVALID;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&a, 256u);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&a, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&a, trusted, 65u, &key);
    if (key)
        (void)psa_destroy_key(key);
    psa_reset_key_attributes(&a);
    if (status != PSA_SUCCESS)
        return NINLIL_ERR_INVALID;
    memset(p, 0, sizeof(*p));
    memset(config, 0, sizeof(*config));
    p->local = local;
    p->local_node = node;
    p->peer_node = remote;
    memcpy(p->public_key, trusted, 65u);
    memcpy(p->identity, trusted_identity, 32u);
    if (memcmp(p->identity, (uint8_t[32]){0}, 32u) == 0 ||
        memcmp(p->identity, local->identity, 32u) == 0)
        return NINLIL_ERR_INVALID;
    config->credentials.fetch = fetch;
    config->credentials.verify = verify;
    config->credential_ctx = p;
    config->peer_identity = peer_identity;
    memcpy(config->expected_peer, p->identity, 32u);
    config->initiator = initiator ? 1u : 0u;
    config->connection_id = initiator ? 1 : 2;
    return NINLIL_OK;
}
