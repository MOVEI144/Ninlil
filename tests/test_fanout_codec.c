#include "ninlil_fanout_store_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static uint32_t random32(uint32_t *x)
{
    *x = *x * 1664525u + 1013904223u;
    return *x;
}
int main(void)
{
    ninlil_fanout_contract c = {0}, out, before;
    ninlil_fanout_target target = {0}, decoded, prior;
    ninlil_fanout_record r = {0}, record, saved;
    uint8_t header[132], bytes[300], encoded[132];
    uint16_t count = 0u, length = 0u;
    uint32_t seed = 20260910u;
    unsigned int accepted = 0u, rejected = 0u;
    c.operation.bytes[0] = c.authority[0] = c.source[0] = c.payload_digest[0] =
        1u;
    c.authority_epoch = c.payload_reference = 1u;
    c.service = 256u;
    c.evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    c.traffic = NINLIL_TRAFFIC_NORMAL;
    ninlil_fanout_store_header_encode(&c, 511u, 256u, header);
    CHECK(ninlil_fanout_store_header_decode(header, sizeof(header), &out,
                                            &count, &length) == 0);
    CHECK(count == 511u && length == 256u &&
          ninlil_fanout_contract_equal(&c, &out));
    target.identity[0] = 2u;
    target.address = 2u;
    target.membership_epoch = target.binding_epoch = 1u;
    target.idempotency_key.bytes[0] = 3u;
    ninlil_fanout_store_target_encode(&c.operation, 3u, &target, bytes);
    CHECK(ninlil_fanout_store_target_decode(bytes, 88u, &c.operation, 3u,
                                            &decoded) == 0);
    CHECK(ninlil_fanout_target_equal(&target, &decoded));
    prior = decoded;
    CHECK(ninlil_fanout_store_target_decode(bytes, 88u, &c.operation, 4u,
                                            &decoded) == NINLIL_ERR_CORRUPT);
    CHECK(!memcmp(&decoded, &prior, sizeof(prior)));
    r.schema = 1u;
    r.contract = c;
    r.index = 4u;
    r.sequence = 2u;
    r.kind = NINLIL_FANOUT_TARGET_INTENT;
    ninlil_fanout_store_delta_encode(&r, bytes);
    CHECK(ninlil_fanout_store_delta_decode(bytes, 49u, &c, &record) == 0);
    CHECK(record.sequence == 2u && record.kind == r.kind &&
          record.index == r.index);
    saved = record;
    bytes[28] = 255u;
    CHECK(ninlil_fanout_store_delta_decode(bytes, 49u, &c, &record) ==
          NINLIL_ERR_CORRUPT);
    CHECK(!memcmp(&saved, &record, sizeof(saved)));
    for (unsigned int i = 0u; i < 20000u; i++) {
        size_t size;
        if (i % 2u) {
            memcpy(bytes, header, sizeof(header));
            size = sizeof(header);
            bytes[random32(&seed) % sizeof(header)] ^=
                (uint8_t)(1u + random32(&seed) % 255u);
        } else {
            size = random32(&seed) % sizeof(bytes);
            for (size_t j = 0u; j < sizeof(bytes); j++)
                bytes[j] = (uint8_t)(random32(&seed) >> 24);
        }
        memset(&out, 0xa5, sizeof(out));
        before = out;
        count = length = 0xaaaa;
        int rc = ninlil_fanout_store_header_decode(bytes, size, &out, &count,
                                                   &length);
        if (!rc) {
            accepted++;
            ninlil_fanout_store_header_encode(&out, count, length, encoded);
            CHECK(size == sizeof(encoded) && !memcmp(bytes, encoded, size));
        } else {
            rejected++;
            CHECK(!memcmp(&out, &before, sizeof(out)) && count == 0xaaaa &&
                  length == 0xaaaa);
        }
    }
    CHECK(accepted > 100u && rejected > 100u);
    printf("fanout codec seed=20260910, canonical mutation accepted=%u "
           "rejected=%u PASS\n",
           accepted, rejected);
    return 0;
}
