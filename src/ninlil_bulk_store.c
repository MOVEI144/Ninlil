#include "ninlil_bulk_internal.h"
#include <psa/crypto.h>
#include <stdlib.h>
#include <string.h>
#define BULK_BYTES (256u * 1024u)
uint32_t ninlil_bulk_get(const uint8_t *p, unsigned int size)
{
    uint32_t n = 0u;
    for (unsigned int i = 0u; i < size; i++)
        n = (n << 8) | p[i];
    return n;
}
void ninlil_bulk_put(uint8_t *p, uint32_t n, unsigned int size)
{
    for (unsigned int i = 0u; i < size; i++)
        p[size - i - 1u] = (uint8_t)(n >> (8u * i));
}
static void header(ninlil_bulk *b, uint8_t data[62])
{
    memset(data, 0, 62u);
    data[0] = 1u;
    data[1] = b->state.sending;
    ninlil_bulk_put(data + 2, b->peer, 2u);
    ninlil_bulk_put(data + 4, b->service, 2u);
    memcpy(data + 6, b->state.manifest.id.bytes, 16u);
    ninlil_bulk_put(data + 22, b->state.manifest.length, 4u);
    memcpy(data + 26, b->state.manifest.sha256, 32u);
}
static int replay(void *ctx, uint8_t type, const uint8_t *data, uint16_t len,
                  const ninlil_journal_ref *ref)
{
    ninlil_bulk *b = ctx;
    if (type == 1u) {
        if (b->begun || len != 62u || data[0] != 1u ||
            data[1] != b->state.sending ||
            ninlil_bulk_get(data + 2, 2u) != b->peer ||
            ninlil_bulk_get(data + 4, 2u) != b->service ||
            ninlil_bulk_get(data + 58, 4u) != 0u)
            return NINLIL_ERR_CORRUPT;
        b->state.manifest.length = ninlil_bulk_get(data + 22, 4u);
        if (!b->state.manifest.length ||
            b->state.manifest.length > NINLIL_BULK_MAX ||
            memcmp(data + 6, (uint8_t[16]){0}, 16u) == 0)
            return NINLIL_ERR_CORRUPT;
        memcpy(b->state.manifest.id.bytes, data + 6, 16u);
        memcpy(b->state.manifest.sha256, data + 26, 32u);
        b->begun = 1u;
        return NINLIL_OK;
    }
    if (!b->begun)
        return NINLIL_ERR_CORRUPT;
    if (type == 2u) {
        uint32_t offset = len >= 4u ? ninlil_bulk_get(data, 4u) : UINT32_MAX;
        uint32_t remaining = b->state.manifest.length - b->state.stored_bytes;
        uint32_t expected =
            remaining > NINLIL_BULK_CHUNK ? NINLIL_BULK_CHUNK : remaining;
        if (b->state.ready || offset != b->state.stored_bytes || !expected ||
            len != expected + 4u)
            return NINLIL_ERR_CORRUPT;
        b->parts[offset / NINLIL_BULK_CHUNK] = *ref;
        b->state.stored_bytes += expected;
        return NINLIL_OK;
    }
    if (type == 3u && len == 1u && data[0] == 1u && !b->state.ready &&
        b->state.stored_bytes == b->state.manifest.length) {
        b->state.ready = 1u;
        return NINLIL_OK;
    }
    if (type == 4u && len == 2u && b->state.ready && b->state.sending) {
        uint32_t cursor = ninlil_bulk_get(data, 2u);
        uint32_t count = (b->state.manifest.length + NINLIL_BULK_CHUNK - 1u) /
                             NINLIL_BULK_CHUNK +
                         2u;
        if (!cursor || cursor < b->state.acknowledged_frames || cursor > count)
            return NINLIL_ERR_CORRUPT;
        b->state.acknowledged_frames = (uint16_t)cursor;
        b->state.remote_stored = (uint8_t)(cursor == count);
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}
static int append(ninlil_bulk *b, uint8_t type, const uint8_t *data,
                  uint16_t len)
{
    ninlil_journal_ref ref;
    uint64_t used, capacity;
    int rc = ninlil_journal_usage(b->journal, &used, &capacity);
    if (rc == NINLIL_OK && used >= capacity - capacity / 4u)
        rc = ninlil_bulk_collect(b);
    if (rc == NINLIL_OK)
        rc = ninlil_journal_append(b->journal, type, data, len, &ref);
    if (rc == NINLIL_OK)
        rc = replay(b, type, data, len, &ref);
    if (rc != NINLIL_OK && rc != NINLIL_ERR_CAPACITY)
        b->fault = rc;
    return rc;
}
int ninlil_bulk_open(ninlil_bulk **out, const char *location, uint16_t peer,
                     uint16_t service, int sending)
{
    ninlil_bulk *b;
    int rc;
    if (!out || !location || !peer ||
        service < NINLIL_APPLICATION_SERVICE_MIN ||
        (sending != 0 && sending != 1))
        return NINLIL_ERR_INVALID;
    *out = NULL;
    b = calloc(1u, sizeof(*b));
    if (!b)
        return NINLIL_ERR_CAPACITY;
    b->peer = peer;
    b->service = service;
    b->state.sending = (uint8_t)sending;
    rc = ninlil_journal_open(&b->journal, location, BULK_BYTES, replay, b);
    if (rc == NINLIL_OK && b->state.ready) {
        uint8_t digest[32];
        rc = ninlil_bulk_digest(b, digest);
        if (rc == NINLIL_OK && memcmp(digest, b->state.manifest.sha256, 32u))
            rc = NINLIL_ERR_CORRUPT;
    }
    if (rc != NINLIL_OK) {
        ninlil_bulk_close(b);
        return rc;
    }
    *out = b;
    return NINLIL_OK;
}
void ninlil_bulk_close(ninlil_bulk *b)
{
    if (b) {
        ninlil_journal_close(b->journal);
        free(b);
    }
}
int ninlil_bulk_begin(ninlil_bulk *b, const ninlil_bulk_manifest *m)
{
    uint8_t data[62];
    if (!b || !m || !m->length || m->length > NINLIL_BULK_MAX ||
        memcmp(m->id.bytes, (uint8_t[16]){0}, 16u) == 0)
        return NINLIL_ERR_INVALID;
    if (b->fault)
        return b->fault;
    if (b->begun)
        return b->state.manifest.length == m->length &&
                       !memcmp(b->state.manifest.id.bytes, m->id.bytes, 16u) &&
                       !memcmp(b->state.manifest.sha256, m->sha256, 32u)
                   ? NINLIL_OK
                   : NINLIL_ERR_CONFLICT;
    b->state.manifest = *m;
    header(b, data);
    return append(b, 1u, data, sizeof(data));
}
int ninlil_bulk_read(ninlil_bulk *b, uint32_t offset, uint8_t *data,
                     uint16_t len)
{
    uint16_t done = 0u;
    if (!b || !data || offset > b->state.stored_bytes ||
        len > b->state.stored_bytes - offset)
        return NINLIL_ERR_INVALID;
    if (b->fault)
        return b->fault;
    while (done < len) {
        uint32_t position = offset + done;
        uint16_t local = (uint16_t)(position % NINLIL_BULK_CHUNK);
        uint16_t size = (uint16_t)(NINLIL_BULK_CHUNK - local);
        int rc;
        if (size > len - done)
            size = (uint16_t)(len - done);
        rc = ninlil_journal_read(b->journal,
                                 &b->parts[position / NINLIL_BULK_CHUNK],
                                 (uint16_t)(local + 4u), data + done, size);
        if (rc != NINLIL_OK) {
            b->fault = rc;
            return rc;
        }
        done = (uint16_t)(done + size);
    }
    return NINLIL_OK;
}
int ninlil_bulk_write(ninlil_bulk *b, uint32_t offset, const uint8_t *data,
                      uint16_t len)
{
    uint8_t record[4u + NINLIL_BULK_CHUNK];
    uint32_t expected;
    if (!b || !b->begun || !data || offset >= b->state.manifest.length ||
        offset % NINLIL_BULK_CHUNK)
        return NINLIL_ERR_INVALID;
    if (b->fault)
        return b->fault;
    expected = b->state.manifest.length - offset;
    if (expected > NINLIL_BULK_CHUNK)
        expected = NINLIL_BULK_CHUNK;
    if (len != expected)
        return NINLIL_ERR_INVALID;
    if (offset < b->state.stored_bytes) {
        int rc = ninlil_bulk_read(b, offset, record, len);
        return rc != NINLIL_OK             ? rc
               : memcmp(record, data, len) ? NINLIL_ERR_CONFLICT
                                           : NINLIL_OK;
    }
    if (offset != b->state.stored_bytes || b->state.ready)
        return NINLIL_ERR_BUSY;
    ninlil_bulk_put(record, offset, 4u);
    memcpy(record + 4, data, len);
    return append(b, 2u, record, (uint16_t)(len + 4u));
}
int ninlil_bulk_digest(ninlil_bulk *b, uint8_t digest[32])
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    uint8_t bytes[256];
    size_t actual = 0u;
    int rc = NINLIL_OK;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS)
        return NINLIL_ERR_IO;
    for (uint32_t offset = 0u;
         rc == NINLIL_OK && offset < b->state.stored_bytes;) {
        uint16_t size =
            (uint16_t)(b->state.stored_bytes - offset > sizeof(bytes)
                           ? sizeof(bytes)
                           : b->state.stored_bytes - offset);
        rc = ninlil_bulk_read(b, offset, bytes, size);
        if (rc == NINLIL_OK && psa_hash_update(&op, bytes, size) != PSA_SUCCESS)
            rc = NINLIL_ERR_IO;
        offset += size;
    }
    if (rc == NINLIL_OK &&
        (psa_hash_finish(&op, digest, 32u, &actual) != PSA_SUCCESS ||
         actual != 32u))
        rc = NINLIL_ERR_IO;
    if (psa_hash_abort(&op) != PSA_SUCCESS)
        rc = NINLIL_ERR_IO;
    return rc;
}
int ninlil_bulk_seal(ninlil_bulk *b)
{
    uint8_t digest[32], ready = 1u;
    int rc;
    if (!b || !b->begun || b->state.stored_bytes != b->state.manifest.length)
        return NINLIL_ERR_STATE;
    if (b->fault)
        return b->fault;
    rc = ninlil_bulk_digest(b, digest);
    if (rc == NINLIL_OK && memcmp(digest, b->state.manifest.sha256, 32u))
        rc = NINLIL_ERR_CONFLICT;
    if (rc == NINLIL_OK && !b->state.ready)
        rc = append(b, 3u, &ready, 1u);
    return rc;
}
int ninlil_bulk_ack(ninlil_bulk *b, uint16_t cursor)
{
    uint8_t data[2];
    ninlil_bulk_put(data, cursor, 2u);
    return append(b, 4u, data, sizeof(data));
}
int ninlil_bulk_query(ninlil_bulk *b, ninlil_bulk_status *out)
{
    if (!b || !out)
        return NINLIL_ERR_INVALID;
    if (b->fault)
        return b->fault;
    *out = b->state;
    return NINLIL_OK;
}
static int valid_record(void *ctx, uint8_t type, const uint8_t *data,
                        uint16_t len, const ninlil_journal_ref *ref)
{
    (void)ctx;
    (void)data;
    (void)len;
    (void)ref;
    return type >= 1u && type <= 4u ? NINLIL_OK : NINLIL_ERR_CORRUPT;
}
static int snapshot(void *ctx, ninlil_journal *out)
{
    ninlil_bulk *b = ctx;
    uint8_t data[62];
    int rc;
    header(b, data);
    rc = ninlil_journal_visit(b->journal, valid_record, NULL);
    if (rc == NINLIL_OK)
        rc = ninlil_journal_append(out, 1u, data, 62u, NULL);
    for (uint32_t offset = 0u;
         rc == NINLIL_OK && offset < b->state.stored_bytes;) {
        uint16_t size =
            (uint16_t)(b->state.stored_bytes - offset > NINLIL_BULK_CHUNK
                           ? NINLIL_BULK_CHUNK
                           : b->state.stored_bytes - offset);
        ninlil_bulk_put(data, offset, 4u);
        rc = ninlil_bulk_read(b, offset, data + 4, size);
        if (rc == NINLIL_OK)
            rc = ninlil_journal_append(out, 2u, data, (uint16_t)(size + 4u),
                                       NULL);
        offset += size;
    }
    data[0] = 1u;
    if (rc == NINLIL_OK && b->state.ready)
        rc = ninlil_journal_append(out, 3u, data, 1u, NULL);
    ninlil_bulk_put(data, b->state.acknowledged_frames, 2u);
    if (rc == NINLIL_OK && b->state.acknowledged_frames)
        rc = ninlil_journal_append(out, 4u, data, 2u, NULL);
    return rc;
}
int ninlil_bulk_collect(ninlil_bulk *b)
{
    int rc;
    if (!b || !b->begun)
        return NINLIL_ERR_STATE;
    if (b->fault)
        return b->fault;
    rc = ninlil_journal_rewrite(b->journal, snapshot, b);
    if (rc == NINLIL_OK) {
        b->begun = 0u;
        b->state.ready = 0u;
        b->state.stored_bytes = 0u;
        b->state.acknowledged_frames = 0u;
        b->state.remote_stored = 0u;
        rc = ninlil_journal_visit(b->journal, replay, b);
    }
    if (rc != NINLIL_OK)
        b->fault = rc;
    return rc;
}
