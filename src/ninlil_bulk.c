#include "ninlil_bulk_internal.h"
#include <psa/crypto.h>
#include <string.h>
static uint16_t count(ninlil_bulk *b)
{
    return (uint16_t)((b->state.manifest.length + NINLIL_BULK_CHUNK - 1u) /
                      NINLIL_BULK_CHUNK);
}
static int receive(ninlil_bulk *b, ninlil_runtime *core)
{
    ninlil_inbound *in = &b->pending;
    const uint8_t *data = in->payload;
    uint16_t index;
    int rc;
    if (!b->offered) {
        rc = ninlil_receive_class(core, b->service, NINLIL_TRAFFIC_BULK, in);
        if (rc == NINLIL_ERR_EMPTY)
            return NINLIL_OK;
        if (rc != NINLIL_OK)
            return rc;
        b->offered = 1u;
    }
    if (in->source != b->peer || in->ownership != NINLIL_OWNERSHIP_DURABLE ||
        in->required_evidence != NINLIL_EVIDENCE_APPLICATION_ACCEPTED ||
        in->traffic_class != NINLIL_TRAFFIC_BULK || in->absolute_deadline_ms ||
        in->payload_len < 24u || memcmp(data, "NBK\001", 4u) || data[5])
        return NINLIL_ERR_INVALID;
    index = (uint16_t)ninlil_bulk_get(data + 22, 2u);
    if (data[4] == 1u && index == 0u && in->payload_len == 60u) {
        ninlil_bulk_manifest m;
        memcpy(m.id.bytes, data + 6, 16u);
        m.length = ninlil_bulk_get(data + 24, 4u);
        memcpy(m.sha256, data + 28, 32u);
        rc = ninlil_bulk_begin(b, &m);
    } else {
        if (!b->begun || memcmp(data + 6, b->state.manifest.id.bytes, 16u))
            return NINLIL_ERR_CONFLICT;
        if (data[4] == 2u && index && index <= count(b))
            rc =
                ninlil_bulk_write(b, (uint32_t)(index - 1u) * NINLIL_BULK_CHUNK,
                                  data + 24, (uint16_t)(in->payload_len - 24u));
        else if (data[4] == 3u && index == count(b) + 1u &&
                 in->payload_len == 24u)
            rc = ninlil_bulk_seal(b);
        else
            return NINLIL_ERR_INVALID;
    }
    if (rc == NINLIL_OK)
        rc = ninlil_application_accept(core, &in->message_id);
    if (rc == NINLIL_OK)
        b->offered = 0u;
    return rc;
}
static int transmit(ninlil_bulk *b, ninlil_runtime *core)
{
    uint8_t data[64] = {0}, key[68], digest[32];
    uint16_t cursor = b->state.acknowledged_frames;
    ninlil_submission s;
    ninlil_info info;
    ninlil_id id;
    size_t length = 0u;
    int rc;
    if (!b->state.ready || b->state.remote_stored)
        return NINLIL_OK;
    ninlil_submission_defaults(&s);
    memcpy(data, "NBK\001", 4u);
    memcpy(data + 6, b->state.manifest.id.bytes, 16u);
    ninlil_bulk_put(data + 22, cursor, 2u);
    s.payload_len = 24u;
    if (!cursor) {
        data[4] = 1u;
        s.payload_len = 60u;
        ninlil_bulk_put(data + 24, b->state.manifest.length, 4u);
        memcpy(data + 28, b->state.manifest.sha256, 32u);
    } else if (cursor <= count(b)) {
        uint32_t offset = (uint32_t)(cursor - 1u) * NINLIL_BULK_CHUNK;
        uint16_t size =
            (uint16_t)(b->state.manifest.length - offset > NINLIL_BULK_CHUNK
                           ? NINLIL_BULK_CHUNK
                           : b->state.manifest.length - offset);
        data[4] = 2u;
        s.payload_len = (uint16_t)(24u + size);
        rc = ninlil_bulk_read(b, offset, data + 24, size);
        if (rc != NINLIL_OK)
            return rc;
    } else
        data[4] = 3u;
    ninlil_bulk_put(key, b->peer, 2u);
    ninlil_bulk_put(key + 2, b->service, 2u);
    memcpy(key + 4, data, s.payload_len);
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, key, (size_t)s.payload_len + 4u,
                         digest, sizeof(digest), &length) != PSA_SUCCESS ||
        length != 32u)
        return NINLIL_ERR_IO;
    memcpy(s.idempotency_key.bytes, digest, 16u);
    s.target = b->peer;
    s.service = b->service;
    s.payload = data;
    s.traffic_class = NINLIL_TRAFFIC_BULK;
    s.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    rc = ninlil_submit(core, &s, &id);
    if (rc == NINLIL_OK)
        rc = ninlil_query(core, &id, &info);
    if (rc != NINLIL_OK)
        return rc;
    if (info.outcome == NINLIL_OUTCOME_ACTIVE)
        return NINLIL_OK;
    if (info.outcome != NINLIL_OUTCOME_SATISFIED ||
        info.latest_evidence != NINLIL_EVIDENCE_APPLICATION_ACCEPTED)
        return NINLIL_ERR_STATE;
    return ninlil_bulk_ack(b, (uint16_t)(cursor + 1u));
}
int ninlil_bulk_step(ninlil_bulk *b, ninlil_runtime *core)
{
    if (!b || !core)
        return NINLIL_ERR_INVALID;
    if (b->fault)
        return b->fault;
    return b->state.sending ? transmit(b, core) : receive(b, core);
}
