#include "ninlil_enrollment.h"
#include "ninlil_node_internal.h"
#include <string.h>

/* Tagged COSE_Sign1, protected {1: -7}, empty unprotected map, NM1 bstr,
 * 64-byte raw ECDSA signature. Accept this exact profile, not arbitrary CBOR.
 * RFC 9052 section 4 / RFC 9053 ES256; PSA supplies hashing and signatures. */
static const uint8_t prefix[] = {0xd2, 0x84, 0x43, 0xa1, 1, 0x26, 0xa0, 0x58};
static int signed_hash(const uint8_t *body, size_t length, uint8_t digest[32])
{
    uint8_t data[19u + NINLIL_MEMBER_RECORD_MAX] = {
        0x84, 0x6a, 'S',  'i',  'g', 'n',  'a',  't',  'u', 'r',
        'e',  '1',  0x43, 0xa1, 1u,  0x26, 0x40, 0x58, 0u};
    size_t written = 0u;
    if (length < 160u || length > NINLIL_MEMBER_RECORD_MAX)
        return NINLIL_ERR_INVALID;
    data[18] = (uint8_t)length;
    memcpy(data + 19, body, length);
    return psa_hash_compute(PSA_ALG_SHA_256, data, length + 19u, digest, 32u,
                            &written) == PSA_SUCCESS &&
                   written == 32u
               ? NINLIL_OK
               : NINLIL_ERR_IO;
}
int ninlil_admission_verify(const uint8_t key[65], const uint8_t *data,
                            size_t length, ninlil_node_member *out)
{
    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t verifier = 0;
    ninlil_node_member member;
    uint8_t digest[32];
    int rc;
    size_t size;
    if (!key || !data || !out || length < 235u ||
        length > NINLIL_ADMISSION_MAX || memcmp(data, prefix, sizeof(prefix)) ||
        data[8] < 160u || data[8] > NINLIL_MEMBER_RECORD_MAX ||
        length != (size_t)data[8] + 75u)
        return NINLIL_ERR_INVALID;
    size = data[8];
    if (data[9u + size] != 0x58 || data[10u + size] != 0x40 ||
        ninlil_member_decode(data + 9, size, &member) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    rc = signed_hash(data + 9, size, digest);
    psa_set_key_type(&attrs,
                     PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attrs, 256u);
    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attrs, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    if (rc == NINLIL_OK &&
        psa_import_key(&attrs, key, 65u, &verifier) != PSA_SUCCESS)
        rc = NINLIL_ERR_UNAUTHORIZED;
    if (rc == NINLIL_OK &&
        psa_verify_hash(verifier, PSA_ALG_ECDSA(PSA_ALG_SHA_256), digest,
                        sizeof(digest), data + 11u + size, 64u) != PSA_SUCCESS)
        rc = NINLIL_ERR_UNAUTHORIZED;
    if (verifier)
        (void)psa_destroy_key(verifier);
    verifier = 0;
    if (rc == NINLIL_OK && psa_import_key(&attrs, member.public_key, 65u,
                                          &verifier) != PSA_SUCCESS)
        rc = NINLIL_ERR_INVALID;
    if (verifier)
        (void)psa_destroy_key(verifier);
    psa_reset_key_attributes(&attrs);
    if (rc == NINLIL_OK)
        *out = member;
    return rc;
}
int ninlil_admission_sign(ninlil_identity *issuer, const ninlil_node_member *m,
                          uint8_t *out, size_t capacity, size_t *length)
{
    uint8_t data[NINLIL_ADMISSION_MAX], hash[32];
    size_t size, written = 0u;
    if (length)
        *length = 0u;
    if (!issuer || !issuer->signing_key || !m || !out || !length)
        return NINLIL_ERR_INVALID;
    size = ninlil_member_encode(m, data + 9, sizeof(data) - 9u);
    if (!size || capacity < size + 75u)
        return NINLIL_ERR_TOO_LARGE;
    int rc = signed_hash(data + 9, size, hash);
    if (rc != NINLIL_OK)
        return rc;
    memcpy(data, prefix, sizeof(prefix));
    data[8] = (uint8_t)size;
    data[9u + size] = 0x58;
    data[10u + size] = 0x40;
    if (psa_sign_hash(issuer->signing_key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash,
                      sizeof(hash), data + 11u + size, 64u,
                      &written) != PSA_SUCCESS ||
        written != 64u)
        return NINLIL_ERR_IO;
    memcpy(out, data, size + 75u);
    *length = size + 75u;
    return NINLIL_OK;
}
int ninlil_node_authorize(ninlil_node *n, const ninlil_node_member *m,
                          uint8_t *out, size_t capacity, size_t *length)
{
    uint8_t data[NINLIL_MEMBER_RECORD_MAX];
    size_t size;
    int rc, index;
    if (length)
        *length = 0u;
    if (!n || !m || !out || !length || n->status.fault ||
        n->config.local != n->config.root || n->config.authority_key)
        return NINLIL_ERR_UNAUTHORIZED;
    size = ninlil_member_encode(m, data, sizeof(data));
    if (!size || capacity < size + 75u)
        return NINLIL_ERR_TOO_LARGE;
    index = ninlil_node_index(n, m->grant.node);
    if (index >= 0 && n->peers[index].revoked &&
        m->grant.membership_epoch <= n->members[index].grant.membership_epoch)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = ninlil_control_log_verify(n->log);
    if (rc != NINLIL_OK) {
        n->status.fault = rc;
        return rc;
    }
    rc = ninlil_node_enroll(n, m);
    return rc == NINLIL_OK ? ninlil_admission_sign(n->config.identity, m, out,
                                                   capacity, length)
                           : rc;
}
int ninlil_node_admit(ninlil_node *n, const uint8_t *data, size_t length)
{
    ninlil_node_member member;
    int rc;
    if (!n || n->status.fault)
        return NINLIL_ERR_STATE;
    rc = ninlil_admission_verify(ninlil_node_admission_key(n), data, length,
                                 &member);
    return rc == NINLIL_OK ? ninlil_node_enroll(n, &member) : rc;
}
