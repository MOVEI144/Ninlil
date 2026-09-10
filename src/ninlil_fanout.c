#include "ninlil_fanout_internal.h"
#include <string.h>
static int nonzero(const uint8_t *p, size_t length)
{
    uint8_t any = 0u;
    for (size_t i = 0u; i < length; i++)
        any |= p[i];
    return any != 0u;
}
int ninlil_fanout_contract_valid(const ninlil_fanout_contract *c)
{
    return c && nonzero(c->operation.bytes, 16u) &&
           nonzero(c->authority, 16u) && nonzero(c->source, 32u) &&
           nonzero(c->payload_digest, 32u) && c->authority_epoch &&
           c->payload_reference &&
           c->service >= NINLIL_APPLICATION_SERVICE_MIN &&
           c->traffic >= NINLIL_TRAFFIC_CRITICAL &&
           c->traffic <= NINLIL_TRAFFIC_BULK &&
           (c->evidence == NINLIL_EVIDENCE_REMOTE_STORED ||
            c->evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
}
int ninlil_fanout_contract_equal(const ninlil_fanout_contract *a,
                                 const ninlil_fanout_contract *b)
{
    return !memcmp(a->operation.bytes, b->operation.bytes, 16u) &&
           !memcmp(a->authority, b->authority, 16u) &&
           !memcmp(a->source, b->source, 32u) &&
           !memcmp(a->payload_digest, b->payload_digest, 32u) &&
           a->authority_epoch == b->authority_epoch &&
           a->deadline_ms == b->deadline_ms &&
           a->payload_reference == b->payload_reference &&
           a->service == b->service && a->traffic == b->traffic &&
           a->evidence == b->evidence;
}
int ninlil_fanout_target_equal(const ninlil_fanout_target *a,
                               const ninlil_fanout_target *b)
{
    return !memcmp(a->identity, b->identity, 32u) &&
           a->membership_epoch == b->membership_epoch &&
           a->binding_epoch == b->binding_epoch && a->address == b->address &&
           !memcmp(a->idempotency_key.bytes, b->idempotency_key.bytes, 16u);
}
int ninlil_fanout_snapshot_valid(const ninlil_fanout_target *t, uint16_t count,
                                 const uint8_t source[32])
{
    if (!t || !count)
        return 0;
    for (unsigned int i = 0u; i < count; i++) {
        if (!memcmp(t[i].identity, source, 32u) ||
            !nonzero(t[i].identity, 32u) || !t[i].membership_epoch ||
            !t[i].binding_epoch || !t[i].address ||
            t[i].address == UINT16_MAX ||
            !nonzero(t[i].idempotency_key.bytes, 16u) ||
            (i && memcmp(t[i - 1u].identity, t[i].identity, 32u) >= 0))
            return 0;
        for (unsigned int j = 0u; j < i; j++)
            if (t[i].address == t[j].address ||
                !memcmp(t[i].idempotency_key.bytes, t[j].idempotency_key.bytes,
                        16u))
                return 0;
    }
    return 1;
}
int ninlil_fanout_open(ninlil_fanout *o, ninlil_fanout_target *targets,
                       ninlil_fanout_item *items, uint16_t capacity,
                       const ninlil_fanout_callbacks *cb)
{
    if (!o || !targets || !items || !capacity ||
        capacity > NINLIL_FANOUT_TARGETS || !cb || !cb->commit ||
        !cb->eligible || !cb->admit_bound || !cb->query_bound)
        return NINLIL_ERR_INVALID;
    memset(o, 0, sizeof(*o));
    o->targets = targets;
    o->items = items;
    o->capacity = capacity;
    o->callbacks = *cb;
    memset(items, 0, (size_t)capacity * sizeof(*items));
    return NINLIL_OK;
}
static int commit(ninlil_fanout *o, const ninlil_fanout_record *r)
{
    int rc = ninlil_fanout_record_check(o, r);
    if (rc == NINLIL_OK)
        rc = o->callbacks.commit(o->callbacks.ctx, r);
    if (rc != NINLIL_OK)
        o->poisoned = 1u;
    return rc;
}
int ninlil_fanout_record_check(const ninlil_fanout *o,
                               const ninlil_fanout_record *r)
{
    const ninlil_fanout_item *item;
    if (!o || !o->capacity || o->poisoned || !r ||
        r->schema != NINLIL_FANOUT_SCHEMA || o->record_sequence == UINT64_MAX ||
        r->sequence != o->record_sequence + 1u ||
        !ninlil_fanout_contract_valid(&r->contract))
        return NINLIL_ERR_CORRUPT;
    if (r->kind == NINLIL_FANOUT_START) {
        return !o->started && r->count && r->count <= o->capacity &&
                       !r->index && !nonzero(r->message.bytes, 16u) &&
                       !r->outcome && !r->evidence &&
                       ninlil_fanout_snapshot_valid(r->targets, r->count,
                                                    r->contract.source)
                   ? NINLIL_OK
                   : NINLIL_ERR_CORRUPT;
    }
    if (!o->started ||
        !ninlil_fanout_contract_equal(&o->contract, &r->contract) ||
        r->targets || r->count || r->index >= o->count)
        return NINLIL_ERR_CORRUPT;
    item = &o->items[r->index];
    if (r->kind == NINLIL_FANOUT_TARGET_INTENT)
        return item->phase == NINLIL_FANOUT_PENDING &&
                       !nonzero(r->message.bytes, 16u) && !r->outcome &&
                       !r->evidence
                   ? NINLIL_OK
                   : NINLIL_ERR_CORRUPT;
    if (r->kind == NINLIL_FANOUT_TARGET_ADMITTED) {
        if (item->phase != NINLIL_FANOUT_INTENT ||
            !nonzero(r->message.bytes, 16u) || r->outcome || r->evidence)
            return NINLIL_ERR_CORRUPT;
        for (unsigned int i = 0u; i < o->count; i++)
            if (i != r->index && nonzero(o->items[i].message.bytes, 16u) &&
                !memcmp(o->items[i].message.bytes, r->message.bytes, 16u))
                return NINLIL_ERR_CORRUPT;
        return NINLIL_OK;
    }
    if (r->kind != NINLIL_FANOUT_TARGET_TERMINAL ||
        item->phase != NINLIL_FANOUT_ADMITTED ||
        memcmp(item->message.bytes, r->message.bytes, 16u) ||
        r->outcome <= NINLIL_OUTCOME_ACTIVE ||
        r->outcome > NINLIL_OUTCOME_UNKNOWN ||
        r->evidence < NINLIL_EVIDENCE_NONE ||
        r->evidence > NINLIL_EVIDENCE_APPLICATION_ACCEPTED ||
        (r->outcome == NINLIL_OUTCOME_SATISFIED &&
         r->evidence < o->contract.evidence))
        return NINLIL_ERR_CORRUPT;
    return NINLIL_OK;
}
static int publish(ninlil_fanout *o, const ninlil_fanout_record *r)
{
    int rc = ninlil_fanout_record_check(o, r);
    if (rc != NINLIL_OK)
        return rc;
    if (r->kind == NINLIL_FANOUT_START) {
        memmove(o->targets, r->targets, (size_t)r->count * sizeof(*o->targets));
        o->contract = r->contract;
        o->count = r->count;
        o->started = 1u;
    } else {
        ninlil_fanout_item *item = &o->items[r->index];
        item->phase =
            r->kind == NINLIL_FANOUT_TARGET_INTENT     ? NINLIL_FANOUT_INTENT
            : r->kind == NINLIL_FANOUT_TARGET_ADMITTED ? NINLIL_FANOUT_ADMITTED
                                                       : NINLIL_FANOUT_TERMINAL;
        item->message = r->message;
        item->outcome = r->outcome;
        item->latest = r->evidence;
    }
    o->record_sequence = r->sequence;
    return NINLIL_OK;
}
int ninlil_fanout_restore(ninlil_fanout *o, const ninlil_fanout_record *r)
{
    int rc;
    if (!o || !o->capacity || o->poisoned)
        return NINLIL_ERR_STATE;
    rc = publish(o, r);
    if (rc != NINLIL_OK)
        o->poisoned = 1u;
    return rc;
}
int ninlil_fanout_start(ninlil_fanout *o, const ninlil_fanout_contract *c,
                        const ninlil_fanout_target *targets, uint16_t count)
{
    ninlil_fanout_record r = {0};
    int rc;
    if (!o || !o->capacity || o->poisoned || !ninlil_fanout_contract_valid(c) ||
        count > o->capacity ||
        !ninlil_fanout_snapshot_valid(targets, count, c->source))
        return NINLIL_ERR_INVALID;
    if (o->started) {
        if (count != o->count || !ninlil_fanout_contract_equal(&o->contract, c))
            return NINLIL_ERR_CONFLICT;
        for (unsigned int i = 0u; i < count; i++)
            if (!ninlil_fanout_target_equal(&o->targets[i], &targets[i]))
                return NINLIL_ERR_CONFLICT;
        return NINLIL_OK;
    }
    r.schema = NINLIL_FANOUT_SCHEMA;
    r.sequence = 1u;
    r.kind = NINLIL_FANOUT_START;
    r.contract = *c;
    r.targets = targets;
    r.count = count;
    rc = commit(o, &r);
    return rc == NINLIL_OK ? ninlil_fanout_restore(o, &r) : rc;
}
static int retryable(int rc)
{
    return rc == NINLIL_ERR_BUSY || rc == NINLIL_ERR_CAPACITY ||
           rc == NINLIL_ERR_NOT_FOUND || rc == NINLIL_ERR_UNAUTHORIZED;
}
static int append(ninlil_fanout *o, uint16_t index,
                  ninlil_fanout_record_kind kind, const ninlil_id *id,
                  ninlil_outcome outcome, ninlil_evidence evidence)
{
    ninlil_fanout_record r = {0};
    int rc;
    if (o->record_sequence == UINT64_MAX)
        return NINLIL_ERR_CAPACITY;
    if (kind == NINLIL_FANOUT_TARGET_ADMITTED) {
        for (unsigned int i = 0u; i < o->count; i++)
            if (i != index &&
                !memcmp(o->items[i].message.bytes, id->bytes, 16u))
                return NINLIL_ERR_CORRUPT; /* Reject before committing a bad
                                              binding. */
    }
    r.schema = NINLIL_FANOUT_SCHEMA;
    r.sequence = o->record_sequence + 1u;
    r.kind = kind;
    r.contract = o->contract;
    r.index = index;
    r.outcome = outcome;
    r.evidence = evidence;
    if (id)
        r.message = *id;
    rc = commit(o, &r);
    return rc == NINLIL_OK ? ninlil_fanout_restore(o, &r) : rc;
}
static int service(ninlil_fanout *o, uint16_t index)
{
    ninlil_fanout_item *item = &o->items[index];
    const ninlil_fanout_target *t = &o->targets[index];
    int rc;
    if (item->phase == NINLIL_FANOUT_ADMITTED) {
        ninlil_info info = {0};
        rc = o->callbacks.query_bound(o->callbacks.ctx, &o->contract, t,
                                      &item->message, &info);
        if (rc != NINLIL_OK)
            return rc;
        if (memcmp(info.message_id.bytes, item->message.bytes, 16u) ||
            info.peer != t->address || info.service != o->contract.service ||
            info.required_evidence != o->contract.evidence ||
            info.traffic_class != o->contract.traffic ||
            info.ownership != NINLIL_OWNERSHIP_DURABLE ||
            info.absolute_deadline_ms != o->contract.deadline_ms ||
            info.outcome < NINLIL_OUTCOME_ACTIVE ||
            info.outcome > NINLIL_OUTCOME_UNKNOWN ||
            info.latest_evidence < NINLIL_EVIDENCE_NONE ||
            info.latest_evidence > NINLIL_EVIDENCE_APPLICATION_ACCEPTED)
            return NINLIL_ERR_CORRUPT;
        if (info.outcome == NINLIL_OUTCOME_ACTIVE)
            return NINLIL_ERR_BUSY;
        if (info.outcome == NINLIL_OUTCOME_SATISFIED &&
            info.latest_evidence < o->contract.evidence)
            return NINLIL_ERR_CORRUPT;
        return append(o, index, NINLIL_FANOUT_TARGET_TERMINAL, &item->message,
                      info.outcome, info.latest_evidence);
    }
    rc = o->callbacks.eligible(o->callbacks.ctx, &o->contract, t);
    if (rc != NINLIL_OK)
        return rc;
    if (item->phase == NINLIL_FANOUT_PENDING) {
        rc = append(o, index, NINLIL_FANOUT_TARGET_INTENT, NULL,
                    NINLIL_OUTCOME_ACTIVE, NINLIL_EVIDENCE_NONE);
        if (rc != NINLIL_OK)
            return rc;
    }
    if (item->phase == NINLIL_FANOUT_INTENT) {
        ninlil_id message = {{0}};
        rc = o->callbacks.admit_bound(o->callbacks.ctx, &o->contract, t,
                                      &message);
        if (rc != NINLIL_OK)
            return rc;
        if (!nonzero(message.bytes, 16u))
            return NINLIL_ERR_CORRUPT;
        return append(o, index, NINLIL_FANOUT_TARGET_ADMITTED, &message,
                      NINLIL_OUTCOME_ACTIVE, NINLIL_EVIDENCE_NONE);
    }
    return NINLIL_ERR_CORRUPT;
}
int ninlil_fanout_step(ninlil_fanout *o, uint64_t now, unsigned int work)
{
    if (!o || !o->started || o->poisoned || !work ||
        work > NINLIL_FANOUT_WORK || now < o->now_ms ||
        now > UINT64_MAX - 1000u)
        return NINLIL_ERR_STATE;
    o->now_ms = now;
    for (unsigned int done = 0u; done < work && done < o->count; done++) {
        uint16_t index = o->cursor;
        ninlil_fanout_item *item = &o->items[index];
        int rc;
        o->cursor = (uint16_t)((index + 1u) % o->count);
        if (item->phase == NINLIL_FANOUT_TERMINAL || now < item->next_ms)
            continue;
        rc = service(o, index);
        item->wait_reason = rc;
        /* A storage callback may use the same BUSY/CAPACITY code as an
         * eligibility deferral. commit() already poisoned an uncertain write:
         * stop this batch before any callback for another target. */
        if (o->poisoned)
            return rc == NINLIL_OK ? NINLIL_ERR_STATE : rc;
        if (retryable(rc))
            item->next_ms = now + 1000u;
        else if (rc != NINLIL_OK) {
            o->poisoned = 1u;
            return rc;
        }
    }
    return NINLIL_OK;
}
int ninlil_fanout_inspect(const ninlil_fanout *o, ninlil_fanout_status *out)
{
    ninlil_fanout_status s = {0};
    if (!o || !out || !o->started || o->poisoned)
        return NINLIL_ERR_STATE;
    s.total = o->count;
    for (unsigned int i = 0u; i < o->count; i++) {
        const ninlil_fanout_item *item = &o->items[i];
        if (item->phase == NINLIL_FANOUT_PENDING)
            s.pending++;
        else if (item->phase == NINLIL_FANOUT_INTENT)
            s.intent++;
        else if (item->phase == NINLIL_FANOUT_ADMITTED)
            s.active++;
        else if (item->outcome == NINLIL_OUTCOME_SATISFIED)
            s.satisfied++;
        else if (item->outcome == NINLIL_OUTCOME_UNKNOWN)
            s.unknown++;
        else
            s.terminal_other++;
    }
    s.all_terminal = (s.satisfied + s.unknown + s.terminal_other) == s.total;
    s.all_satisfied = s.satisfied == s.total;
    *out = s;
    return NINLIL_OK;
}
