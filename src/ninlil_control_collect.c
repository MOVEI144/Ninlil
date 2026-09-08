#include "ninlil_control_internal.h"
#include <stdlib.h>
#include <string.h>
typedef struct collection {
    ninlil_control_log *original, *next;
    ninlil_control_snapshot snapshot;
    void *ctx;
} collection;
static int validate(void *ctx, uint8_t type, const uint8_t *data,
                    uint16_t length, const ninlil_journal_ref *ref)
{
    ninlil_control_log *log = ctx;
    (void)ref;
    if (type == MEMBER_RECORD)
        return ninlil_control_member_valid(data, length);
    if (type == JOIN_RECORD) {
        ninlil_join_record record;
        return ninlil_join_decode(data, length, &record);
    }
    if (type == PLAN_RECORD) {
        ninlil_network_plan plan;
        return ninlil_network_plan_decode(data, length, &plan);
    }
    if (type == RELAY_RECORD) {
        ninlil_relay_record record;
        return ninlil_relay_decode(data, length, &record);
    }
    if (type == STORAGE_BINDING)
        return log->bound && length == 33u && data[0] == 1u &&
                       memcmp(data + 1, log->identity, 32u) == 0
                   ? NINLIL_OK
                   : NINLIL_ERR_CORRUPT;
    if (type == EPOCH_FENCE)
        return length == 12u && memcmp(data, "NEF\001", 4u) == 0 &&
                       memcmp(data + 4, (uint8_t[8]){0}, 8u) != 0
                   ? NINLIL_OK
                   : NINLIL_ERR_CORRUPT;
    return NINLIL_ERR_CORRUPT;
}
int ninlil_control_log_verify(ninlil_control_log *log)
{
    int rc;
    if (!log || log->poisoned)
        return NINLIL_ERR_STATE;
    rc = ninlil_journal_visit(log->journal, validate, log);
    if (rc != NINLIL_OK)
        log->poisoned = 1u;
    return rc == NINLIL_ERR_INVALID ? NINLIL_ERR_CORRUPT : rc;
}
static int snapshot(void *ctx, ninlil_journal *journal)
{
    collection *c = ctx;
    int rc = ninlil_control_log_verify(c->original);
    c->next->journal = journal;
    if (rc == NINLIL_OK && c->original->bound)
        rc = ninlil_control_log_bind(c->next, c->original->identity, 1);
    if (rc == NINLIL_OK)
        rc = c->snapshot(c->ctx, c->next);
    for (unsigned int i = 0u; rc == NINLIL_OK && i < NINLIL_RELAY_PACKETS_MAX;
         i++) {
        const packet_reference *p = &c->original->packets[i];
        uint8_t bytes[NINLIL_RELAY_FRAME_MAX];
        ninlil_relay_record record;
        if (!p->used)
            continue;
        if (p->reference.length > sizeof(bytes))
            return NINLIL_ERR_CORRUPT;
        rc = ninlil_journal_read(c->original->journal, &p->reference, 0u, bytes,
                                 p->reference.length);
        if (rc == NINLIL_OK)
            rc = ninlil_relay_decode(bytes, p->reference.length, &record);
        if (rc == NINLIL_OK)
            rc = ninlil_control_log_verify_relay(c->next, &record);
    }
    return rc;
}
int ninlil_control_log_collect(ninlil_control_log *log,
                               ninlil_control_snapshot emit, void *ctx,
                               int force)
{
    collection c = {0};
    uint64_t used, capacity;
    int rc;
    if (!log || !emit || log->poisoned || (force != 0 && force != 1))
        return NINLIL_ERR_STATE;
    rc = ninlil_journal_usage(log->journal, &used, &capacity);
    if (rc != NINLIL_OK)
        return rc;
    if (!force &&
        (used < capacity - capacity / 4u || used == log->collected_bytes))
        return NINLIL_OK;
    c.original = log;
    c.snapshot = emit;
    c.ctx = ctx;
    c.next = calloc(1u, sizeof(*c.next));
    if (!c.next)
        return NINLIL_ERR_CAPACITY;
    rc = ninlil_journal_rewrite(log->journal, snapshot, &c);
    if (rc == NINLIL_OK) {
        memcpy(log->packets, c.next->packets, sizeof(log->packets));
        rc = ninlil_journal_usage(log->journal, &used, &capacity);
    }
    free(c.next);
    if (rc == NINLIL_OK || rc == NINLIL_ERR_NOT_FOUND)
        log->collected_bytes = used;
    else if (rc != NINLIL_ERR_CAPACITY)
        log->poisoned = 1u;
    return !force && rc == NINLIL_ERR_NOT_FOUND ? NINLIL_OK : rc;
}
