#include "ninlil_security_partitions.h"
#include "secure_bench.h"
#include <string.h>

typedef struct bench_context {
    ninlil_secure_session session;
    ninlil_counter_store counter;
    ninlil_esp_security_partition partition;
    ninlil_security_io io;
    ninlil_counter_config config;
} bench_context;

static bench_context contexts[3][2];

ninlil_secure_session *ninlil_bench_session(uint16_t peer, uint8_t hop)
{
    if (CONFIG_NINLIL_NODE_ID < 1 || CONFIG_NINLIL_NODE_ID > 3 || peer < 1u ||
        peer > 3u || peer == CONFIG_NINLIL_NODE_ID || hop > 1u)
        return NULL;
    return &contexts[peer - 1u][hop].session;
}

static int open_context(const ninlil_session_material *material, uint16_t peer,
                        uint8_t hop)
{
    bench_context *c = &contexts[peer - 1u][hop];
    uint16_t slot = (uint16_t)((peer - 1u) * 2u + hop);
    int rc;
    ninlil_secure_close(&c->session);
    ninlil_counter_close(&c->counter);
    rc = ninlil_esp_session_counter_io(&c->partition, &c->io, slot);
    memset(&c->config, 0, sizeof(c->config));
    memcpy(c->config.session_fingerprint, material->fingerprint, 16u);
    c->config.direction = (uint8_t)(CONFIG_NINLIL_NODE_ID < peer ? 0u : 1u);
    c->config.reservation_size = 32u;
    c->config.max_counter_exclusive = UINT64_C(1000000);
    /* The USB owner explicitly selected this peer and fresh EDHOC completed.
     * Only this peer/kind's dedicated lab counter slot is formatted. */
    if (rc == NINLIL_OK)
        rc = ninlil_security_format(&c->io);
    if (rc == NINLIL_OK)
        rc = ninlil_counter_open(&c->counter, &c->io, NINLIL_COUNTER_CREATE_NEW,
                                 &c->config);
    if (rc == NINLIL_OK)
        rc = ninlil_secure_open(&c->session, material, &c->counter,
                                ninlil_psa_aead(), CONFIG_NINLIL_NODE_ID, peer,
                                c->config.direction);
    return rc;
}

int ninlil_bench_sessions_open(ninlil_edhoc *h, uint16_t peer,
                               uint8_t identity[32], uint8_t fingerprint[16])
{
    ninlil_session_material material[2];
    int rc;
    if (!ninlil_bench_session(peer, 0u) || !identity || !fingerprint)
        return NINLIL_ERR_INVALID;
    rc = ninlil_edhoc_material(h, &material[0], identity);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_edhoc_hop_material(h, &material[1]);
    if (rc == NINLIL_OK)
        rc = open_context(&material[0], peer, 0u);
    if (rc == NINLIL_OK)
        rc = open_context(&material[1], peer, 1u);
    if (rc == NINLIL_OK)
        memcpy(fingerprint, material[0].fingerprint, 16u);
    else {
        ninlil_secure_close(ninlil_bench_session(peer, 0u));
        ninlil_secure_close(ninlil_bench_session(peer, 1u));
    }
    ninlil_secret_clear(material, sizeof(material));
    ninlil_edhoc_close(h);
    return rc;
}

int ninlil_bench_counter_resume(uint16_t peer, uint8_t hop)
{
    bench_context *c;
    if (!ninlil_bench_session(peer, hop))
        return NINLIL_ERR_INVALID;
    c = &contexts[peer - 1u][hop];
    if (!c->session.ready)
        return NINLIL_ERR_STATE;
    ninlil_counter_close(&c->counter);
    return ninlil_counter_open(&c->counter, &c->io,
                               NINLIL_COUNTER_RESUME_EXISTING, &c->config);
}

void ninlil_bench_sessions_close(void)
{
    unsigned int i, j;
    for (i = 0u; i < 3u; i++)
        for (j = 0u; j < 2u; j++) {
            ninlil_secure_close(&contexts[i][j].session);
            ninlil_counter_close(&contexts[i][j].counter);
        }
}
