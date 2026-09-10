#include "ninlil_fanout.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)
/* This is a deterministic storage/Core adapter fixture, not the legacy Core.
 * The real bound-message journal adapter remains an explicit integration gate.
 */
typedef struct fixture {
    ninlil_fanout_record records[1600];
    ninlil_fanout_target snapshot[512];
    ninlil_id admitted[512];
    uint64_t bindings[512];
    unsigned int count, calls, blocked, lost_admit, write_attempts;
    int fail_before, fail_after, forged, write_error;
} fixture;
static fixture f;
static unsigned int index_of(const ninlil_fanout_target *t)
{
    return (unsigned int)t->address - 2u;
}
static int commit(void *ctx, const ninlil_fanout_record *r)
{
    fixture *x = ctx;
    x->write_attempts++;
    if (x->write_error)
        return x->write_error;
    if (x->fail_before)
        return NINLIL_ERR_IO;
    if (x->count >= 1600u)
        return NINLIL_ERR_CAPACITY;
    x->records[x->count] = *r;
    if (r->kind == NINLIL_FANOUT_START) {
        memcpy(x->snapshot, r->targets, (size_t)r->count * sizeof(*r->targets));
        x->records[x->count].targets = x->snapshot;
    }
    x->count++;
    if (x->fail_after)
        return NINLIL_ERR_IO;
    return NINLIL_OK;
}
static int eligible(void *ctx, const ninlil_fanout_contract *c,
                    const ninlil_fanout_target *t)
{
    fixture *x = ctx;
    (void)c;
    unsigned int i = index_of(t);
    if (t->binding_epoch != x->bindings[i])
        return NINLIL_ERR_UNAUTHORIZED;
    return i < x->blocked ? NINLIL_ERR_BUSY : NINLIL_OK;
}
static int admit(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, ninlil_id *id)
{
    fixture *x = ctx;
    (void)c;
    unsigned int i = index_of(t);
    if (memcmp(x->records[x->count - 1u].contract.operation.bytes,
               c->operation.bytes, 16u))
        return NINLIL_ERR_STATE;
    if (!x->admitted[i].bytes[0]) {
        x->admitted[i] = t->idempotency_key;
        x->calls++;
    }
    *id = x->admitted[i];
    if (x->lost_admit) {
        x->lost_admit = 0;
        return NINLIL_ERR_IO;
    }
    return NINLIL_OK;
}
static int query(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, const ninlil_id *id,
                 ninlil_info *info)
{
    fixture *x = ctx;
    memset(info, 0, sizeof(*info));
    info->message_id = *id;
    info->peer = t->address;
    info->service = c->service;
    info->ownership = NINLIL_OWNERSHIP_DURABLE;
    info->required_evidence = c->evidence;
    info->traffic_class = c->traffic;
    info->absolute_deadline_ms = c->deadline_ms;
    info->outcome = NINLIL_OUTCOME_SATISFIED;
    info->latest_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    if (x->forged)
        info->peer++;
    return NINLIL_OK;
}
static ninlil_fanout_target targets[512], workspace[512];
static ninlil_fanout_item items[512];
static int reopen(ninlil_fanout *o, const ninlil_fanout_callbacks *cb)
{
    int rc = ninlil_fanout_open(o, workspace, items, 512, cb);
    for (unsigned int i = 0u; rc == 0 && i < f.count; i++)
        rc = ninlil_fanout_restore(o, &f.records[i]);
    return rc;
}
int main(void)
{
    ninlil_fanout o;
    ninlil_fanout_status s;
    ninlil_fanout_contract c = {0};
    ninlil_fanout_callbacks cb = {commit, eligible, admit, query, &f};
    c.operation.bytes[0] = 1u;
    c.authority[0] = 1u;
    c.source[0] = 1u;
    c.payload_digest[0] = 1u;
    c.authority_epoch = 1u;
    c.payload_reference = 1u;
    c.service = 256u;
    c.traffic = NINLIL_TRAFFIC_NORMAL;
    c.evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    for (unsigned int i = 0u; i < 512u; i++) {
        targets[i].identity[0] = (uint8_t)((i + 1u) >> 8);
        targets[i].identity[1] = (uint8_t)(i + 1u);
        targets[i].address = (uint16_t)(i + 2u);
        targets[i].membership_epoch = targets[i].binding_epoch = 1u;
        targets[i].idempotency_key.bytes[0] = 1u;
        targets[i].idempotency_key.bytes[1] = (uint8_t)(i >> 8);
        targets[i].idempotency_key.bytes[2] = (uint8_t)i;
        f.bindings[i] = 1u;
    }
    CHECK(ninlil_fanout_open(&o, workspace, items, 512, &cb) == 0);
    /* This fixture originally reused target 256's identity as the source. */
    CHECK(ninlil_fanout_start(&o, &c, targets, 511) == NINLIL_ERR_INVALID &&
          f.count == 0);
    c.source[0] = 254u;
    CHECK(ninlil_fanout_start(&o, &c, targets, 511) == 0);
    CHECK(ninlil_fanout_start(&o, &c, targets, 511) == 0 && f.count == 1);
    f.blocked = 32u;
    for (uint64_t t = 0u; t < 60000u; t += 1000u)
        CHECK(ninlil_fanout_step(&o, t, 32) == 0);
    CHECK(ninlil_fanout_inspect(&o, &s) == 0 && s.satisfied == 479 &&
          s.pending == 32 && !s.all_terminal);
    CHECK(reopen(&o, &cb) == 0 && f.calls == 479);
    f.blocked = 0;
    /* Address reuse is not permission to deliver to the replacement. */
    f.bindings[0] = 2u;
    for (uint64_t t = 0u; t < 60000u; t += 1000u)
        CHECK(ninlil_fanout_step(&o, t, 32) == 0);
    CHECK(ninlil_fanout_inspect(&o, &s) == 0 && s.satisfied == 510 &&
          s.pending == 1);
    f.bindings[0] = 1u;
    for (uint64_t t = 60000u; t < 90000u; t += 1000u)
        CHECK(ninlil_fanout_step(&o, t, 32) == 0);
    CHECK(ninlil_fanout_inspect(&o, &s) == 0 && s.all_satisfied &&
          f.calls == 511);
    /* Core committed, but its reply was lost: replay intent, reuse exact key.
     */
    memset(&f, 0, sizeof(f));
    f.bindings[0] = 1u;
    CHECK(ninlil_fanout_open(&o, workspace, items, 512, &cb) == 0);
    CHECK(ninlil_fanout_start(&o, &c, targets, 1) == 0);
    f.lost_admit = 1u;
    CHECK(ninlil_fanout_step(&o, 0, 1) == NINLIL_ERR_IO && o.poisoned &&
          f.calls == 1);
    CHECK(reopen(&o, &cb) == 0 && items[0].phase == NINLIL_FANOUT_INTENT);
    CHECK(ninlil_fanout_step(&o, 0, 1) == 0 && f.calls == 1);
    f.forged = 1;
    CHECK(ninlil_fanout_step(&o, 1, 1) == NINLIL_ERR_CORRUPT && o.poisoned);
    f.forged = 0;
    CHECK(reopen(&o, &cb) == 0);
    f.fail_after = 1;
    CHECK(ninlil_fanout_step(&o, 0, 1) == NINLIL_ERR_IO && o.poisoned);
    f.fail_after = 0;
    CHECK(reopen(&o, &cb) == 0);
    CHECK(ninlil_fanout_inspect(&o, &s) == 0 && s.all_satisfied);
    /* A storage error remains a poisoned view even if the numeric error is
     * normally retryable for RF eligibility. Do not visit another target in
     * this same call; never return OK while the persistent owner is poisoned.
     */
    for (unsigned int fail = 0u; fail < 2u; fail++) {
        int error = fail ? NINLIL_ERR_CAPACITY : NINLIL_ERR_BUSY;
        memset(&f, 0, sizeof(f));
        for (unsigned int i = 0u; i < 7u; i++)
            f.bindings[i] = 1u;
        CHECK(ninlil_fanout_open(&o, workspace, items, 512, &cb) == 0);
        CHECK(ninlil_fanout_start(&o, &c, targets, 7) == 0);
        f.write_error = error;
        CHECK(ninlil_fanout_step(&o, 0, 32) == error);
        CHECK(o.poisoned && f.write_attempts == 2u && f.count == 1u &&
              f.calls == 0u);
        CHECK(ninlil_fanout_step(&o, 1000, 32) == NINLIL_ERR_STATE);
        CHECK(f.write_attempts == 2u);
        f.write_error = 0;
        CHECK(reopen(&o, &cb) == 0);
        CHECK(ninlil_fanout_step(&o, 0, 32) == 0 && f.calls == 7u);
        CHECK(ninlil_fanout_step(&o, 1, 32) == 0);
        CHECK(ninlil_fanout_inspect(&o, &s) == 0 && s.all_satisfied);
    }
    puts(
        "511-target fanout/partition/identity/replay/poisoned batch stop PASS");
    return 0;
}
