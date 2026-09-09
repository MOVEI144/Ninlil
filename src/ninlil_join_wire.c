#include "ninlil_join.h"

#include <string.h>

static void put(uint8_t *p, uint64_t value, size_t count)
{
    while (count != 0u) {
        p[--count] = (uint8_t)value;
        value >>= 8;
    }
}
static uint64_t get(const uint8_t *p, size_t count)
{
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < count; i++)
        value = (value << 8) | p[i];
    return value;
}

static int transaction_valid(const uint8_t transaction[16])
{
    uint8_t value = 0u;
    unsigned int i;
    for (i = 0u; i < 16u; i++)
        value |= transaction[i];
    return value != 0u;
}

size_t ninlil_join_encode(const ninlil_join_record *r, uint8_t *out,
                          size_t capacity)
{
    uint8_t bytes[NINLIL_JOIN_RECORD_MAX];
    size_t size, i;
    if (!r || !out || ninlil_join_grant_valid(&r->grant) != NINLIL_OK ||
        !transaction_valid(r->transaction) || r->state < NINLIL_JOIN_PENDING ||
        r->state > NINLIL_JOIN_REVOKED)
        return 0u;
    size = 92u + (size_t)r->grant.service_count * 8u;
    if (capacity < size)
        return 0u;
    memset(bytes, 0, size);
    memcpy(bytes, "NJ\001", 3u);
    bytes[3] = (uint8_t)r->state;
    memcpy(bytes + 4, r->grant.identity, 32u);
    memcpy(bytes + 36, r->grant.authority, 16u);
    put(bytes + 52, r->grant.node, 2u);
    put(bytes + 54, r->grant.membership_epoch, 8u);
    put(bytes + 62, r->grant.binding_epoch, 8u);
    put(bytes + 70, r->grant.capabilities, 4u);
    bytes[74] = (uint8_t)r->grant.role;
    bytes[75] = r->grant.service_count;
    memcpy(bytes + 76, r->transaction, 16u);
    for (i = 0u; i < r->grant.service_count; i++) {
        const ninlil_service_grant *s = &r->grant.services[i];
        uint8_t *p = bytes + 92u + i * 8u;
        put(p, s->service_id, 2u);
        put(p + 2, s->maximum_payload_bytes, 2u);
        put(p + 4, s->maximum_live_messages, 2u);
        p[6] = s->directions;
        p[7] = s->traffic_class_mask;
    }
    memcpy(out, bytes, size);
    return size;
}

int ninlil_join_decode(const uint8_t *bytes, size_t size,
                       ninlil_join_record *out)
{
    ninlil_join_record r;
    size_t i;
    if (!bytes || !out || size < 92u || size > NINLIL_JOIN_RECORD_MAX ||
        memcmp(bytes, "NJ\001", 3u) != 0 ||
        bytes[75] > NINLIL_JOIN_SERVICES_MAX ||
        size != 92u + (size_t)bytes[75] * 8u ||
        bytes[3] < NINLIL_JOIN_PENDING || bytes[3] > NINLIL_JOIN_REVOKED)
        return NINLIL_ERR_INVALID;
    memset(&r, 0, sizeof(r));
    r.state = (ninlil_join_state)bytes[3];
    memcpy(r.grant.identity, bytes + 4, 32u);
    memcpy(r.grant.authority, bytes + 36, 16u);
    r.grant.node = (uint16_t)get(bytes + 52, 2u);
    r.grant.membership_epoch = get(bytes + 54, 8u);
    r.grant.binding_epoch = get(bytes + 62, 8u);
    r.grant.capabilities = (uint32_t)get(bytes + 70, 4u);
    r.grant.role = (ninlil_role)bytes[74];
    r.grant.service_count = bytes[75];
    memcpy(r.transaction, bytes + 76, 16u);
    for (i = 0u; i < r.grant.service_count; i++) {
        const uint8_t *p = bytes + 92u + i * 8u;
        r.grant.services[i].service_id = (uint16_t)get(p, 2u);
        r.grant.services[i].maximum_payload_bytes = (uint16_t)get(p + 2, 2u);
        r.grant.services[i].maximum_live_messages = (uint16_t)get(p + 4, 2u);
        r.grant.services[i].directions = p[6];
        r.grant.services[i].traffic_class_mask = p[7];
    }
    if (ninlil_join_grant_valid(&r.grant) != NINLIL_OK ||
        !transaction_valid(r.transaction))
        return NINLIL_ERR_INVALID;
    *out = r;
    return NINLIL_OK;
}
