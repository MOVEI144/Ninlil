#ifndef EDHOC_TEST_CREDENTIALS_H
#define EDHOC_TEST_CREDENTIALS_H
typedef struct credential_context {
    int initiator;
    int expired;
    int verified;
} credential_context;

static int fetch_credential(void *ctx, struct edhoc_auth_creds *credential)
{
    credential_context *c = ctx;
    credential->label = EDHOC_COSE_HEADER_X509_CHAIN;
    credential->x509_chain.nr_of_certs = 1u;
    credential->x509_chain.cert[0] = c->initiator ? CRED_I : CRED_R;
    credential->x509_chain.cert_len[0] =
        c->initiator ? sizeof(CRED_I) : sizeof(CRED_R);
    return edhoc_cipher_suite_2_key_import(
        NULL, EDHOC_KT_SIGNATURE, c->initiator ? SK_I : SK_R,
        c->initiator ? sizeof(SK_I) : sizeof(SK_R), credential->priv_key_id);
}

static int verify_credential(void *ctx, struct edhoc_auth_creds *credential,
                             const uint8_t **key, size_t *key_length)
{
    credential_context *c = ctx;
    const uint8_t *expected = c->initiator ? CRED_R : CRED_I;
    size_t size = c->initiator ? sizeof(CRED_R) : sizeof(CRED_I);
    if (c->expired || credential->label != EDHOC_COSE_HEADER_X509_CHAIN ||
        credential->x509_chain.nr_of_certs != 1u ||
        credential->x509_chain.cert_len[0] != size ||
        memcmp(credential->x509_chain.cert[0], expected, size) != 0)
        return EDHOC_ERROR_CREDENTIALS_FAILURE;
    *key = c->initiator ? PK_R : PK_I;
    *key_length = c->initiator ? sizeof(PK_R) : sizeof(PK_I);
    c->verified = 1;
    return EDHOC_SUCCESS;
}

static int identity(void *ctx, uint8_t output[32])
{
    credential_context *c = ctx;
    if (!c->verified)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(output, c->initiator ? 0x22 : 0x11, 32u);
    return NINLIL_OK;
}

static int setup(ninlil_edhoc *h, credential_context *c)
{
    ninlil_edhoc_config config;
    memset(&config, 0, sizeof(config));
    config.credentials.fetch = fetch_credential;
    config.credentials.verify = verify_credential;
    config.credential_ctx = c;
    config.peer_identity = identity;
    config.initiator = (uint8_t)c->initiator;
    config.connection_id = c->initiator ? -1 : -2;
    memset(config.expected_peer, c->initiator ? 0x22 : 0x11, 32u);
    c->verified = 0;
    return ninlil_edhoc_open(h, &config, 1000u);
}

static int handshake(ninlil_session_material *material,
                     ninlil_session_material *hop)
{
    ninlil_edhoc a, b;
    credential_context ca = {1, 0, 0}, cb = {0, 0, 0};
    uint8_t one[1024], two[1024], three[1024], four[1024], retry[1024],
        peer[32];
    size_t n1, n2, n3, n4, nr;
    ninlil_session_material other;
    CHECK(setup(&a, &ca) == NINLIL_OK && setup(&b, &cb) == NINLIL_OK);
    CHECK(ninlil_edhoc_material(&a, material, peer) == NINLIL_ERR_STATE);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1001u, one, sizeof(one), &n1) ==
          NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&a, NULL, 0u, 1002u, retry, sizeof(retry),
                                &nr) == NINLIL_OK);
    CHECK(nr == n1 && memcmp(one, retry, nr) == 0);
    CHECK(ninlil_edhoc_exchange(&b, one, n1, 1003u, two, sizeof(two), &n2) ==
          NINLIL_OK);
    CHECK(ninlil_edhoc_exchange(&b, one, n1, 1004u, retry, sizeof(retry),
                                &nr) == NINLIL_OK);
    CHECK(nr == n2 && memcmp(two, retry, nr) == 0);
    CHECK(ninlil_edhoc_exchange(&a, two, n2, 1005u, three, sizeof(three),
                                &n3) == NINLIL_OK);
    CHECK(ninlil_edhoc_material(&a, material, peer) == NINLIL_ERR_STATE);
    {
        int rc = ninlil_edhoc_exchange(&b, three, n3, 1006u, four, sizeof(four),
                                       &n4);
        if (rc != NINLIL_OK)
            fprintf(stderr, "responder final rc=%d upstream=%d\n", rc,
                    b.upstream_error);
        CHECK(rc == NINLIL_OK);
    }
    CHECK(ninlil_edhoc_exchange(&b, three, n3, 1007u, retry, sizeof(retry),
                                &nr) == NINLIL_OK);
    CHECK(nr == n4 && memcmp(four, retry, nr) == 0);
    CHECK(ninlil_edhoc_exchange(&a, four, n4, 1008u, retry, sizeof(retry),
                                &nr) == NINLIL_OK);
    CHECK(nr == 0u);
    CHECK(ninlil_edhoc_material(&a, material, peer) == NINLIL_OK &&
          peer[0] == 0x22);
    CHECK(ninlil_edhoc_material(&b, &other, peer) == NINLIL_OK &&
          peer[0] == 0x11);
    CHECK(memcmp(material, &other, sizeof(other)) == 0);
    if (hop) {
        CHECK(ninlil_edhoc_hop_material(&a, hop) == NINLIL_OK);
        CHECK(ninlil_edhoc_hop_material(&b, &other) == NINLIL_OK);
        CHECK(memcmp(hop, &other, sizeof(other)) == 0);
        CHECK(memcmp(hop, material, sizeof(other)) != 0);
    }
    ninlil_edhoc_close(&a);
    ninlil_edhoc_close(&b);
    CHECK(ninlil_edhoc_material(&a, &other, peer) == NINLIL_ERR_STATE);
    return 0;
}

#endif
