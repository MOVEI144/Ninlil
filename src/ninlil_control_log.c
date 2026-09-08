#include "ninlil_control_log.h"
#include "ninlil_journal.h"

#include <stdlib.h>
#include <string.h>

// Record numbers are local to this separate journal; typed payload magics
// prevent confusing it with a delivery journal. Existing envelopes stay v4/v5.
#define JOIN_RECORD 1u
#define PLAN_RECORD 2u
#define RELAY_RECORD 3u
#define STORAGE_BINDING 4u

typedef struct packet_reference {
    uint8_t id[16];
    ninlil_journal_ref reference;
    uint8_t used;
} packet_reference;

struct ninlil_control_log {
    ninlil_journal *journal;
    ninlil_control_replay replay;
    packet_reference packets[NINLIL_RELAY_PACKETS_MAX];
    uint8_t poisoned;
    uint8_t identity[32];
    uint8_t bound;
    uint8_t has_records;
};

static int index_record(ninlil_control_log *log, const ninlil_relay_record *r,
                        const ninlil_journal_ref *ref)
{
    packet_reference *slot = NULL, *empty = NULL;
    unsigned int i;
    if (r->control)
        return NINLIL_OK;
    for (i = 0u; i < NINLIL_RELAY_PACKETS_MAX; i++) {
        packet_reference *p = &log->packets[i];
        if (p->used && memcmp(p->id, r->packet_id, 16u) == 0)
            slot = p;
        if (!p->used)
            empty = p;
    }
    if (r->done) {
        if (!slot)
            return NINLIL_ERR_CORRUPT;
        memset(slot, 0, sizeof(*slot));
        return NINLIL_OK;
    }
    if (!slot)
        slot = empty;
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    memcpy(slot->id, r->packet_id, 16u);
    slot->reference = *ref;
    slot->used = 1u;
    return NINLIL_OK;
}

static int replay_record(void *ctx, uint8_t type, const uint8_t *data,
                         uint16_t length, const ninlil_journal_ref *ref)
{
    ninlil_control_log *log = ctx;
    if (type == STORAGE_BINDING) {
        if (log->has_records || length != 33u || data[0] != 1u ||
            memcmp(data + 1, (uint8_t[32]){0}, 32u) == 0)
            return NINLIL_ERR_CORRUPT;
        memcpy(log->identity, data + 1, 32u);
        log->bound = log->has_records = 1u;
        return NINLIL_OK;
    }
    log->has_records = 1u;
    if (type == JOIN_RECORD) {
        ninlil_join_record r;
        if (ninlil_join_decode(data, length, &r) != NINLIL_OK ||
            !log->replay.join)
            return NINLIL_ERR_CORRUPT;
        return log->replay.join(log->replay.ctx, &r);
    }
    if (type == PLAN_RECORD) {
        ninlil_network_plan p;
        if (ninlil_network_plan_decode(data, length, &p) != NINLIL_OK ||
            !log->replay.plan)
            return NINLIL_ERR_CORRUPT;
        return log->replay.plan(log->replay.ctx, &p);
    }
    if (type == RELAY_RECORD) {
        ninlil_relay_record r;
        int rc;
        if (ninlil_relay_decode(data, length, &r) != NINLIL_OK ||
            !log->replay.relay)
            return NINLIL_ERR_CORRUPT;
        rc = index_record(log, &r, ref);
        if (rc != NINLIL_OK)
            return rc;
        return log->replay.relay(log->replay.ctx, &r);
    }
    return NINLIL_ERR_CORRUPT;
}

int ninlil_control_log_open(ninlil_control_log **out, const char *location,
                            uint64_t maximum_bytes,
                            ninlil_control_replay replay)
{
    ninlil_control_log *log;
    int rc;
    if (!out || !location || maximum_bytes == 0u ||
        maximum_bytes > NINLIL_CONTROL_LOG_MAX)
        return NINLIL_ERR_INVALID;
    log = calloc(1u, sizeof(*log));
    if (!log)
        return NINLIL_ERR_CAPACITY;
    log->replay = replay;
    rc = ninlil_journal_open(&log->journal, location, maximum_bytes,
                             replay_record, log);
    if (rc != NINLIL_OK) {
        free(log);
        return rc;
    }
    *out = log;
    return NINLIL_OK;
}

void ninlil_control_log_close(ninlil_control_log *log)
{
    if (log) {
        ninlil_journal_close(log->journal);
        free(log);
    }
}

static int append(ninlil_control_log *log, uint8_t type, const uint8_t *data,
                  size_t size, ninlil_journal_ref *ref)
{
    uint8_t checked[NINLIL_RELAY_FRAME_MAX];
    int rc;
    if (!log || log->poisoned || !size || size > sizeof(checked))
        return NINLIL_ERR_STATE;
    rc = ninlil_journal_append(log->journal, type, data, (uint16_t)size, ref);
    if (rc == NINLIL_OK)
        log->has_records = 1u;
    if (rc == NINLIL_OK)
        rc =
            ninlil_journal_read(log->journal, ref, 0u, checked, (uint16_t)size);
    if (rc == NINLIL_OK && memcmp(data, checked, size) != 0)
        rc = NINLIL_ERR_CORRUPT;
    if (rc != NINLIL_OK)
        log->poisoned = 1u;
    return rc;
}

int ninlil_control_log_bind(ninlil_control_log *log, const uint8_t identity[32],
                            int initialize)
{
    ninlil_journal_ref ref;
    uint8_t data[33];
    int rc;
    if (!log || log->poisoned || !identity ||
        memcmp(identity, (uint8_t[32]){0}, 32u) == 0)
        return NINLIL_ERR_INVALID;
    if (log->bound)
        return memcmp(log->identity, identity, 32u) == 0 ? NINLIL_OK
                                                         : NINLIL_ERR_CONFLICT;
    if (!initialize || log->has_records)
        return NINLIL_ERR_CORRUPT;
    data[0] = 1u;
    memcpy(data + 1, identity, 32u);
    rc = append(log, STORAGE_BINDING, data, sizeof(data), &ref);
    if (rc == NINLIL_OK) {
        memcpy(log->identity, identity, 32u);
        log->bound = 1u;
    }
    return rc;
}

int ninlil_control_log_join(void *ctx, const ninlil_join_record *r)
{
    uint8_t bytes[NINLIL_JOIN_RECORD_MAX];
    ninlil_journal_ref ref;
    size_t n = ninlil_join_encode(r, bytes, sizeof(bytes));
    return n ? append(ctx, JOIN_RECORD, bytes, n, &ref) : NINLIL_ERR_INVALID;
}

int ninlil_control_log_plan(void *ctx, const ninlil_network_plan *p)
{
    uint8_t bytes[NINLIL_NETWORK_PLAN_MAX];
    ninlil_journal_ref ref;
    size_t n = ninlil_network_plan_encode(p, bytes, sizeof(bytes));
    return n ? append(ctx, PLAN_RECORD, bytes, n, &ref) : NINLIL_ERR_INVALID;
}

int ninlil_control_log_relay(void *ctx, const ninlil_relay_record *r)
{
    ninlil_control_log *log = ctx;
    uint8_t bytes[NINLIL_RELAY_FRAME_MAX];
    ninlil_journal_ref ref;
    size_t n = ninlil_relay_encode(r, bytes, sizeof(bytes));
    int rc;
    if (!log || !n)
        return NINLIL_ERR_INVALID;
    // Capacity is checked before append so failure cannot create an unindexed
    // owned packet. Existing IDs and drain controls need no new index slot.
    if (!r->control && !r->done) {
        unsigned int i;
        int space = 0;
        for (i = 0u; i < NINLIL_RELAY_PACKETS_MAX; i++)
            if (!log->packets[i].used ||
                memcmp(log->packets[i].id, r->packet_id, 16u) == 0)
                space = 1;
        if (!space)
            return NINLIL_ERR_CAPACITY;
    }
    rc = append(log, RELAY_RECORD, bytes, n, &ref);
    if (rc == NINLIL_OK)
        rc = index_record(log, r, &ref);
    if (rc != NINLIL_OK)
        log->poisoned = 1u;
    return rc;
}

int ninlil_control_log_verify_relay(void *ctx, const ninlil_relay_record *r)
{
    ninlil_control_log *log = ctx;
    uint8_t expected[NINLIL_RELAY_FRAME_MAX], actual[NINLIL_RELAY_FRAME_MAX];
    ninlil_relay_record restored;
    size_t length;
    unsigned int i;
    int rc = NINLIL_ERR_NOT_FOUND;
    if (!log || log->poisoned || !r || r->control || r->done)
        return NINLIL_ERR_STATE;
    length = ninlil_relay_encode(r, expected, sizeof(expected));
    if (!length)
        return NINLIL_ERR_INVALID;
    for (i = 0u; i < NINLIL_RELAY_PACKETS_MAX; i++) {
        packet_reference *p = &log->packets[i];
        if (!p->used || memcmp(p->id, r->packet_id, 16u) != 0)
            continue;
        if (p->reference.length > sizeof(actual)) {
            rc = NINLIL_ERR_CORRUPT;
            break;
        }
        rc = ninlil_journal_read(log->journal, &p->reference, 0u, actual,
                                 p->reference.length);
        /* Canonical comparison also revalidates retained NRv1 journals. */
        if (rc == NINLIL_OK &&
            (ninlil_relay_decode(actual, p->reference.length, &restored) !=
                 NINLIL_OK ||
             ninlil_relay_encode(&restored, actual, sizeof(actual)) != length ||
             memcmp(actual, expected, length) != 0))
            rc = NINLIL_ERR_CORRUPT;
        break;
    }
    if (rc != NINLIL_OK)
        log->poisoned = 1u;
    return rc;
}
