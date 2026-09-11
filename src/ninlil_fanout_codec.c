#include "ninlil_fanout_store_internal.h"
#include <string.h>
static void put(uint8_t *out, uint64_t value, size_t n)
{
    while (n) {
        out[--n] = (uint8_t)value;
        value >>= 8;
    }
}
static uint64_t get(const uint8_t *data, size_t n)
{
    uint64_t value = 0u;
    for (size_t i = 0u; i < n; i++)
        value = (value << 8) | data[i];
    return value;
}
static int prefix(const uint8_t *data, size_t size, const char magic[4],
                  const ninlil_id *op)
{
    return data && size >= 20u && !memcmp(data, magic, 4u) &&
           !memcmp(data + 4, op->bytes, 16u);
}
void ninlil_fanout_store_header_encode(const ninlil_fanout_contract *c,
                                       uint16_t count, uint16_t length,
                                       uint8_t out[132])
{
    memcpy(out, "NFH\001", 4u);
    memcpy(out + 4, c->operation.bytes, 16u);
    memcpy(out + 20, c->authority, 16u);
    memcpy(out + 36, c->source, 32u);
    memcpy(out + 68, c->payload_digest, 32u);
    put(out + 100, c->authority_epoch, 8u);
    put(out + 108, c->deadline_ms, 8u);
    put(out + 116, c->payload_reference, 8u);
    put(out + 124, c->service, 2u);
    out[126] = (uint8_t)c->traffic;
    out[127] = (uint8_t)c->evidence;
    put(out + 128, count, 2u);
    put(out + 130, length, 2u);
}
int ninlil_fanout_store_header_decode(const uint8_t *d, size_t size,
                                      ninlil_fanout_contract *out,
                                      uint16_t *count, uint16_t *length)
{
    ninlil_fanout_contract c = {0};
    uint16_t n, len;
    if (!d || !out || !count || !length || size != 132u ||
        memcmp(d, "NFH\001", 4u))
        return NINLIL_ERR_CORRUPT;
    memcpy(c.operation.bytes, d + 4, 16u);
    memcpy(c.authority, d + 20, 16u);
    memcpy(c.source, d + 36, 32u);
    memcpy(c.payload_digest, d + 68, 32u);
    c.authority_epoch = get(d + 100, 8u);
    c.deadline_ms = get(d + 108, 8u);
    c.payload_reference = get(d + 116, 8u);
    c.service = (uint16_t)get(d + 124, 2u);
    c.traffic = (ninlil_traffic_class)d[126];
    c.evidence = (ninlil_evidence)d[127];
    n = (uint16_t)get(d + 128, 2u);
    len = (uint16_t)get(d + 130, 2u);
    if (!ninlil_fanout_contract_valid(&c) || !n || n > NINLIL_FANOUT_TARGETS ||
        len > 256u)
        return NINLIL_ERR_CORRUPT;
    *out = c;
    *count = n;
    *length = len;
    return NINLIL_OK;
}
void ninlil_fanout_store_target_encode(const ninlil_id *op, uint16_t index,
                                       const ninlil_fanout_target *t,
                                       uint8_t out[88])
{
    memcpy(out, "NFT\001", 4u);
    memcpy(out + 4, op->bytes, 16u);
    put(out + 20, index, 2u);
    put(out + 22, t->address, 2u);
    memcpy(out + 24, t->identity, 32u);
    put(out + 56, t->membership_epoch, 8u);
    put(out + 64, t->binding_epoch, 8u);
    memcpy(out + 72, t->idempotency_key.bytes, 16u);
}
int ninlil_fanout_store_target_decode(const uint8_t *d, size_t size,
                                      const ninlil_id *op, uint16_t index,
                                      ninlil_fanout_target *out)
{
    ninlil_fanout_target t = {0};
    if (!out || !op || !prefix(d, size, "NFT\001", op) || size != 88u ||
        get(d + 20, 2u) != index)
        return NINLIL_ERR_CORRUPT;
    t.address = (uint16_t)get(d + 22, 2u);
    memcpy(t.identity, d + 24, 32u);
    t.membership_epoch = get(d + 56, 8u);
    t.binding_epoch = get(d + 64, 8u);
    memcpy(t.idempotency_key.bytes, d + 72, 16u);
    if (!ninlil_fanout_snapshot_valid(&t, 1u, (uint8_t[32]){0}))
        return NINLIL_ERR_CORRUPT;
    *out = t;
    return NINLIL_OK;
}
void ninlil_fanout_store_seal_encode(const ninlil_id *op, uint16_t count,
                                     uint16_t length, uint8_t out[24])
{
    memcpy(out, "NFC\001", 4u);
    memcpy(out + 4, op->bytes, 16u);
    put(out + 20, count, 2u);
    put(out + 22, length, 2u);
}
void ninlil_fanout_store_payload_encode(const ninlil_id *op, const uint8_t *p,
                                        uint16_t length,
                                        uint8_t out[NFS_MAX_RECORD])
{
    memcpy(out, "NFP\001", 4u);
    memcpy(out + 4, op->bytes, 16u);
    put(out + 20, length, 2u);
    if (length)
        memcpy(out + 22, p, length);
}
int ninlil_fanout_store_payload_decode(const uint8_t *d, size_t size,
                                       const ninlil_id *op, uint16_t length)
{
    return op && length <= 256u && prefix(d, size, "NFP\001", op) &&
                   size == 22u + length && get(d + 20, 2u) == length
               ? NINLIL_OK
               : NINLIL_ERR_CORRUPT;
}
void ninlil_fanout_store_delta_encode(const ninlil_fanout_record *r,
                                      uint8_t out[49])
{
    memcpy(out, "NFD\001", 4u);
    memcpy(out + 4, r->contract.operation.bytes, 16u);
    put(out + 20, r->sequence, 8u);
    out[28] = (uint8_t)r->kind;
    put(out + 29, r->index, 2u);
    memcpy(out + 31, r->message.bytes, 16u);
    out[47] = (uint8_t)r->outcome;
    out[48] = (uint8_t)r->evidence;
}
int ninlil_fanout_store_delta_decode(const uint8_t *d, size_t size,
                                     const ninlil_fanout_contract *c,
                                     ninlil_fanout_record *out)
{
    ninlil_fanout_record r = {0};
    if (!out || !c || !prefix(d, size, "NFD\001", &c->operation) ||
        size != 49u || d[28] < NINLIL_FANOUT_TARGET_INTENT ||
        d[28] > NINLIL_FANOUT_TARGET_TERMINAL ||
        d[47] > NINLIL_OUTCOME_UNKNOWN ||
        d[48] > NINLIL_EVIDENCE_APPLICATION_ACCEPTED)
        return NINLIL_ERR_CORRUPT;
    r.schema = NINLIL_FANOUT_SCHEMA;
    r.contract = *c;
    r.sequence = get(d + 20, 8u);
    r.kind = (ninlil_fanout_record_kind)d[28];
    r.index = (uint16_t)get(d + 29, 2u);
    memcpy(r.message.bytes, d + 31, 16u);
    r.outcome = (ninlil_outcome)d[47];
    r.evidence = (ninlil_evidence)d[48];
    if (r.sequence < 2u || r.index >= NINLIL_FANOUT_TARGETS)
        return NINLIL_ERR_CORRUPT;
    *out = r;
    return NINLIL_OK;
}
