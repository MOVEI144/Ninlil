#define _POSIX_C_SOURCE 200809L
#include "ninlil_fanout_store_internal.h"
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);            \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
/* Real filesystem journals; the downstream binding ledger is a named Core
 * adapter fixture. It does not run the actual delivery Core or physical RF. */
static ninlil_journal *armed;
static unsigned int cut_after, append_count;
static int injected_error, fail_after_write;
int __real_ninlil_journal_append(ninlil_journal *, uint8_t, const uint8_t *,
                                 uint16_t, ninlil_journal_ref *);
int __wrap_ninlil_journal_append(ninlil_journal *, uint8_t, const uint8_t *,
                                 uint16_t, ninlil_journal_ref *);
int __wrap_ninlil_journal_append(ninlil_journal *j, uint8_t type,
                                 const uint8_t *p, uint16_t length,
                                 ninlil_journal_ref *ref)
{
    int rc;
    if (j == armed && injected_error && !fail_after_write)
        return injected_error;
    rc = __real_ninlil_journal_append(j, type, p, length, ref);
    if (j == armed && rc == NINLIL_OK) {
        append_count++;
        if (cut_after && append_count == cut_after)
            _exit(71);
        if (injected_error)
            return injected_error;
    }
    return rc;
}
typedef struct adapter {
    ninlil_journal *journal;
    ninlil_fanout_target targets[512];
    unsigned int count, callbacks, blocked, admitted_calls, query_calls;
    int lost_reply, query_unknown;
} adapter;
static adapter fixture;
static unsigned int hash_calls, hash_error_at;
static ninlil_fanout_contract contract;
static ninlil_fanout_target targets[512];
static uint8_t body[256];
static int hash(void *ctx, const uint8_t *p, size_t n, uint8_t out[32])
{
    uint8_t result[EVP_MAX_MD_SIZE];
    unsigned int length = 0u;
    (void)ctx;
    hash_calls++;
    if (hash_error_at && hash_calls == hash_error_at)
        return NINLIL_ERR_BUSY;
    if (EVP_Digest(p, n, result, &length, EVP_sha256(), NULL) != 1 ||
        length != 32u)
        return NINLIL_ERR_IO;
    memcpy(out, result, 32u);
    return NINLIL_OK;
}
static int replay_adapter(void *ctx, uint8_t type, const uint8_t *data,
                          uint16_t length, const ninlil_journal_ref *ref)
{
    adapter *a = ctx;
    ninlil_fanout_target t;
    (void)ref;
    if (type != 1u || a->count >= 512u || length != 88u ||
        ninlil_fanout_store_target_decode(data, length, &contract.operation,
                                          (uint16_t)a->count, &t))
        return NINLIL_ERR_CORRUPT;
    a->targets[a->count++] = t;
    return NINLIL_OK;
}
static int eligible(void *ctx, const ninlil_fanout_contract *c,
                    const ninlil_fanout_target *t)
{
    adapter *a = ctx;
    CHECK(!memcmp(c, &contract, sizeof(contract)));
    a->callbacks++;
    return t->address < a->blocked + 2u ? NINLIL_ERR_BUSY : NINLIL_OK;
}
static int admit(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, const uint8_t *payload,
                 uint16_t length, ninlil_id *message)
{
    adapter *a = ctx;
    uint8_t data[88];
    ninlil_journal_ref ref;
    unsigned int i;
    int rc;
    (void)c;
    CHECK(length <= sizeof(body) && !memcmp(payload, body, length));
    a->admitted_calls++;
    for (i = 0u; i < a->count; i++)
        if (!memcmp(a->targets[i].idempotency_key.bytes,
                    t->idempotency_key.bytes, 16u))
            break;
    if (i < a->count) {
        if (!ninlil_fanout_target_equal(&a->targets[i], t))
            return NINLIL_ERR_CONFLICT;
    } else {
        ninlil_fanout_store_target_encode(&contract.operation,
                                          (uint16_t)a->count, t, data);
        rc = ninlil_journal_append(a->journal, 1u, data, sizeof(data), &ref);
        if (rc)
            return rc;
        CHECK(a->count < 512u);
        a->targets[a->count++] = *t;
    }
    *message = t->idempotency_key;
    if (a->lost_reply) {
        a->lost_reply = 0;
        return NINLIL_ERR_IO;
    }
    return NINLIL_OK;
}
static int query(void *ctx, const ninlil_fanout_contract *c,
                 const ninlil_fanout_target *t, const ninlil_id *message,
                 ninlil_info *info)
{
    adapter *a = ctx;
    a->query_calls++;
    memset(info, 0, sizeof(*info));
    info->message_id = *message;
    info->peer = t->address;
    info->service = c->service;
    info->payload_len = 256u;
    info->ownership = NINLIL_OWNERSHIP_DURABLE;
    info->required_evidence = c->evidence;
    info->traffic_class = c->traffic;
    info->absolute_deadline_ms = c->deadline_ms;
    info->outcome =
        a->query_unknown ? NINLIL_OUTCOME_UNKNOWN : NINLIL_OUTCOME_SATISFIED;
    info->latest_evidence = a->query_unknown
                                ? NINLIL_EVIDENCE_NONE
                                : NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    return NINLIL_OK;
}
static ninlil_fanout_store_config config(const char *path, uint16_t capacity)
{
    ninlil_fanout_store_config c = {0};
    c.location = path;
    c.target_capacity = capacity;
    c.maximum_bytes = NINLIL_FANOUT_STORE_MAX_BYTES;
    memcpy(c.source_identity, contract.source, 32u);
    c.operation = contract.operation;
    c.sha256 = hash;
    c.eligible = eligible;
    c.admit_bound = admit;
    c.query_bound = query;
    c.delivery_ctx = &fixture;
    return c;
}
static void init_adapter(const char *path)
{
    memset(&fixture, 0, sizeof(fixture));
    CHECK(ninlil_journal_open(&fixture.journal, path, 1024u * 1024u,
                              replay_adapter, &fixture) == 0);
    armed = NULL;
    injected_error = fail_after_write = 0;
    cut_after = append_count = 0u;
}
static void finish_all(ninlil_fanout_store *s, uint16_t n)
{
    ninlil_fanout_status status;
    for (uint64_t t = 0u; t < 70000u; t += 1000u)
        CHECK(ninlil_fanout_store_step(s, t, 32u) == 0);
    CHECK(ninlil_fanout_store_inspect(s, &status) == 0);
    CHECK(status.total == n && status.all_satisfied && status.all_terminal &&
          fixture.count == n);
}
static void paths(char out[256], char adapter_path[256], const char *dir,
                  unsigned int id)
{
    CHECK(snprintf(out, 256u, "%s/store-%u", dir, id) > 0);
    CHECK(snprintf(adapter_path, 256u, "%s/adapter-%u", dir, id) > 0);
}
static void start_crashes(const char *dir)
{
    for (unsigned int cut = 1u; cut <= 10u; cut++) {
        char path[256], ledger[256];
        int status;
        pid_t child;
        paths(path, ledger, dir, cut);
        child = fork();
        CHECK(child >= 0);
        if (!child) {
            ninlil_fanout_store *s;
            ninlil_fanout_store_config c = config(path, 7u);
            init_adapter(ledger);
            CHECK(ninlil_fanout_store_open(
                      &s, &c, NINLIL_FANOUT_STORE_INITIALIZE) == 0);
            armed = s->journal;
            cut_after = cut;
            (void)ninlil_fanout_store_start(s, &contract, targets, 7u, body,
                                            256u);
            _exit(72);
        }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 71);
        ninlil_fanout_store *s = NULL;
        ninlil_fanout_store_config c = config(path, 7u);
        init_adapter(ledger);
        CHECK(fixture.count == 0u);
        int rc = ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME);
        CHECK(rc == (cut == 10u ? NINLIL_OK : NINLIL_ERR_EMPTY));
        if (rc)
            CHECK(s == NULL &&
                  ninlil_fanout_store_open(
                      &s, &c, NINLIL_FANOUT_STORE_INITIALIZE) == 0);
        ninlil_fanout_contract changed = contract;
        changed.service++;
        CHECK(ninlil_fanout_store_start(s, &changed, targets, 7u, body, 256u) ==
              NINLIL_ERR_CONFLICT);
        CHECK(ninlil_fanout_store_start(s, &contract, targets, 7u, body,
                                        256u) == 0);
        CHECK(fixture.count == 0u && fixture.callbacks == 0u);
        finish_all(s, 7u);
        ninlil_fanout_store_close(s);
        ninlil_journal_close(fixture.journal);
    }
    puts("real POSIX: 10 process exits during seven-target START; no admission "
         "before seal; identical resume PASS");
}
static void delta_crashes(const char *dir)
{
    for (unsigned int cut = 1u; cut <= 3u; cut++) {
        char path[256], ledger[256];
        int status;
        pid_t child;
        paths(path, ledger, dir, 20u + cut);
        child = fork();
        CHECK(child >= 0);
        if (!child) {
            ninlil_fanout_store *s;
            ninlil_fanout_store_config c = config(path, 1u);
            init_adapter(ledger);
            CHECK(ninlil_fanout_store_open(
                      &s, &c, NINLIL_FANOUT_STORE_INITIALIZE) == 0);
            CHECK(ninlil_fanout_store_start(s, &contract, targets, 1u, body,
                                            256u) == 0);
            armed = s->journal;
            cut_after = cut;
            CHECK(ninlil_fanout_store_step(s, 0u, 1u) == 0);
            (void)ninlil_fanout_store_step(s, 1000u, 1u);
            _exit(72);
        }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 71);
        ninlil_fanout_store *s;
        ninlil_fanout_store_config c = config(path, 1u);
        init_adapter(ledger);
        CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME) ==
              0);
        ninlil_fanout_target target;
        ninlil_fanout_item item;
        CHECK(ninlil_fanout_store_target(s, 0u, &target, &item) == 0);
        CHECK(item.phase == (ninlil_fanout_phase)cut);
        finish_all(s, 1u);
        ninlil_fanout_store_close(s);
        ninlil_journal_close(fixture.journal);
    }
    puts("real POSIX: process exits after INTENT/ADMITTED/TERMINAL; "
         "independent adapter ledger replay PASS");
}
static void lost_reply(const char *dir)
{
    char path[256], ledger[256];
    paths(path, ledger, dir, 30u);
    ninlil_fanout_store *s;
    ninlil_fanout_store_config c = config(path, 1u);
    init_adapter(ledger);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_INITIALIZE) ==
          0);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 1u, body, 256u) ==
          0);
    fixture.lost_reply = 1;
    CHECK(ninlil_fanout_store_step(s, 0u, 1u) == NINLIL_ERR_IO &&
          fixture.count == 1u);
    CHECK(ninlil_fanout_store_step(s, 1000u, 1u) == NINLIL_ERR_STATE);
    ninlil_fanout_store_close(s);
    ninlil_journal_close(fixture.journal);
    init_adapter(ledger);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME) == 0);
    finish_all(s, 1u);
    CHECK(fixture.admitted_calls == 1u && fixture.count == 1u);
    ninlil_fanout_store_close(s);
    ninlil_journal_close(fixture.journal);
    puts("real POSIX: adapter committed but response lost; exact bound key "
         "returns same ID once PASS");
}
static void ambiguous_write(const char *dir)
{
    for (unsigned int mode = 0u; mode < 3u; mode++) {
        char path[256], ledger[256];
        paths(path, ledger, dir, 40u + mode);
        ninlil_fanout_store *s;
        ninlil_fanout_store_config c = config(path, 7u);
        init_adapter(ledger);
        CHECK(ninlil_fanout_store_open(&s, &c,
                                       NINLIL_FANOUT_STORE_INITIALIZE) == 0);
        CHECK(ninlil_fanout_store_start(s, &contract, targets, 7u, body,
                                        256u) == 0);
        armed = s->journal;
        injected_error = mode == 0u   ? NINLIL_ERR_BUSY
                         : mode == 1u ? NINLIL_ERR_CAPACITY
                                      : NINLIL_ERR_IO;
        fail_after_write = mode == 2u;
        CHECK(ninlil_fanout_store_step(s, 0u, 7u) == injected_error);
        CHECK(fixture.callbacks == 1u && fixture.admitted_calls == 0u);
        ninlil_fanout_store_close(s);
        armed = NULL;
        injected_error = 0;
        CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME) ==
              0);
        finish_all(s, 7u);
        ninlil_fanout_store_close(s);
        ninlil_journal_close(fixture.journal);
    }
    puts("real POSIX: BUSY/CAPACITY/committed-then-IO poison the batch before "
         "another target PASS");
}
static void mutate(const char *path, uint64_t offset)
{
    uint8_t b;
    int fd = open(path, O_RDWR);
    CHECK(fd >= 0);
    CHECK(pread(fd, &b, 1u, (off_t)offset) == 1);
    b ^= 0x80u;
    CHECK(pwrite(fd, &b, 1u, (off_t)offset) == 1 && fsync(fd) == 0);
    CHECK(close(fd) == 0);
}
static void corruption(const char *dir)
{
    for (unsigned int part = 0u; part < 5u; part++) {
        char path[256], ledger[256];
        paths(path, ledger, dir, 50u + part);
        ninlil_fanout_store *s, *reopened = NULL;
        ninlil_fanout_store_config c = config(path, 1u);
        init_adapter(ledger);
        CHECK(ninlil_fanout_store_open(&s, &c,
                                       NINLIL_FANOUT_STORE_INITIALIZE) == 0);
        CHECK(ninlil_fanout_store_start(s, &contract, targets, 1u, body,
                                        256u) == 0);
        CHECK(ninlil_fanout_store_step(s, 0u, 1u) == 0);
        uint64_t positions[5] = {s->header.offset + 36u,
                                 s->refs[0].target.offset + 24u,
                                 s->payload.offset + 22u, s->seal.offset + 20u,
                                 s->refs[0].state.offset + 31u};
        mutate(path, positions[part]);
        fixture.callbacks = fixture.admitted_calls = fixture.query_calls = 0u;
        ninlil_fanout_status status, previous;
        memset(&status, 0xa5, sizeof(status));
        previous = status;
        CHECK(ninlil_fanout_store_inspect(s, &status) == NINLIL_ERR_CORRUPT);
        CHECK(!memcmp(&status, &previous, sizeof(status)));
        CHECK(ninlil_fanout_store_step(s, 1000u, 1u) == NINLIL_ERR_STATE);
        CHECK(!fixture.callbacks && !fixture.admitted_calls &&
              !fixture.query_calls);
        ninlil_fanout_store_close(s);
        CHECK(ninlil_fanout_store_open(&reopened, &c,
                                       NINLIL_FANOUT_STORE_RESUME) ==
                  NINLIL_ERR_CORRUPT &&
              !reopened);
        ninlil_journal_close(fixture.journal);
    }
    puts("real POSIX: committed header/target/payload/seal/state corruption "
         "blocks metadata and delivery PASS");
}
static void scale_and_bounds(const char *dir)
{
    char path[256], ledger[256];
    paths(path, ledger, dir, 60u);
    ninlil_fanout_store *s, *other = NULL;
    ninlil_fanout_store_config c = config(path, 512u);
    ninlil_fanout_status status;
    struct stat before, after;
    init_adapter(ledger);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_INITIALIZE) ==
          0);
    CHECK(ninlil_fanout_store_open(
              &other, &c, NINLIL_FANOUT_STORE_INITIALIZE) == NINLIL_ERR_BUSY &&
          !other);
    uint8_t wrong[256];
    memcpy(wrong, body, sizeof(wrong));
    wrong[0]++;
    CHECK(stat(path, &before) == 0);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 511u, wrong, 256u) ==
          NINLIL_ERR_CONFLICT);
    CHECK(stat(path, &after) == 0 && before.st_size == after.st_size);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 511u, body, 256u) ==
          0);
    fixture.blocked = 32u;
    for (uint64_t t = 0u; t < 40000u; t += 1000u)
        CHECK(ninlil_fanout_store_step(s, t, 32u) == 0);
    CHECK(ninlil_fanout_store_inspect(s, &status) == 0 &&
          status.satisfied == 479u && status.pending == 32u);
    ninlil_fanout_store_close(s);
    ninlil_journal_close(fixture.journal);
    init_adapter(ledger);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME) == 0);
    finish_all(s, 511u);
    CHECK(fixture.count == 511u);
    CHECK(stat(path, &before) == 0);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 511u, body, 256u) ==
          0);
    CHECK(stat(path, &after) == 0 && before.st_size == after.st_size);
    printf("real POSIX: 511 targets, 32 blocked then recovered, file=%lld "
           "bytes, requested RAM=%zu bytes PASS\n",
           (long long)before.st_size, ninlil_fanout_store_memory(512u));
    ninlil_fanout_store_close(s);
    c.operation.bytes[0]++;
    CHECK(ninlil_fanout_store_open(&other, &c, NINLIL_FANOUT_STORE_RESUME) ==
              NINLIL_ERR_CONFLICT &&
          !other);
    ninlil_journal_close(fixture.journal);
}
static void torn_start(const char *dir)
{
    char golden[256], ledger[256], path[256], unused[256];
    uint8_t bytes[4096];
    ninlil_fanout_store *store;
    ninlil_fanout_status status;
    paths(golden, ledger, dir, 61u);
    paths(path, unused, dir, 62u);
    ninlil_fanout_store_config c = config(golden, 7u);
    CHECK(ninlil_fanout_store_open(&store, &c,
                                   NINLIL_FANOUT_STORE_INITIALIZE) == 0);
    CHECK(ninlil_fanout_store_start(store, &contract, targets, 7u, body,
                                    256u) == 0);
    ninlil_fanout_store_close(store);
    int fd = open(golden, O_RDONLY);
    CHECK(fd >= 0);
    ssize_t n = read(fd, bytes, sizeof(bytes));
    CHECK(n > 0 && n < (ssize_t)sizeof(bytes));
    CHECK(close(fd) == 0);
    c.location = path;
    fixture.callbacks = fixture.admitted_calls = fixture.query_calls = 0u;
    for (size_t cut = 0u; cut <= (size_t)n; cut++) {
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        CHECK(fd >= 0);
        CHECK(write(fd, bytes, cut) == (ssize_t)cut && fsync(fd) == 0 &&
              close(fd) == 0);
        store = NULL;
        int rc =
            ninlil_fanout_store_open(&store, &c, NINLIL_FANOUT_STORE_RESUME);
        CHECK(rc == (cut == (size_t)n ? NINLIL_OK : NINLIL_ERR_EMPTY));
        if (rc)
            CHECK(!store &&
                  ninlil_fanout_store_open(
                      &store, &c, NINLIL_FANOUT_STORE_INITIALIZE) == 0);
        CHECK(ninlil_fanout_store_start(store, &contract, targets, 7u, body,
                                        256u) == 0);
        CHECK(ninlil_fanout_store_inspect(store, &status) == 0 &&
              status.pending == 7u && !status.all_terminal);
        ninlil_fanout_store_close(store);
        CHECK(unlink(path) == 0);
    }
    CHECK(!fixture.callbacks && !fixture.admitted_calls &&
          !fixture.query_calls);
    printf("real POSIX: all %zu byte-prefix torn START states resume without "
           "delivering a partial set PASS\n",
           (size_t)n + 1u);
}
static void real_capacity_and_unknown(const char *dir)
{
    char path[256], ledger[256];
    paths(path, ledger, dir, 63u);
    ninlil_fanout_store *s;
    ninlil_fanout_store_config c = config(path, 7u);
    ninlil_fanout_status status;
    init_adapter(ledger);
    c.maximum_bytes = 512u;
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_INITIALIZE) ==
          0);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 7u, body, 256u) ==
          NINLIL_ERR_CAPACITY);
    CHECK(!fixture.callbacks &&
          ninlil_fanout_store_step(s, 0u, 7u) == NINLIL_ERR_STATE);
    ninlil_fanout_store_close(s);
    c.maximum_bytes = NINLIL_FANOUT_STORE_MAX_BYTES;
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_INITIALIZE) ==
          0);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 7u, body, 256u) ==
          0);
    fixture.query_unknown = 1;
    CHECK(ninlil_fanout_store_step(s, 0u, 7u) == 0);
    CHECK(ninlil_fanout_store_step(s, 1000u, 7u) == 0);
    CHECK(ninlil_fanout_store_inspect(s, &status) == 0 && status.all_terminal &&
          !status.all_satisfied && status.unknown == 7u);
    ninlil_fanout_store_close(s);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME) == 0);
    CHECK(ninlil_fanout_store_inspect(s, &status) == 0 &&
          status.unknown == 7u && !status.all_satisfied);
    uint8_t output[256];
    uint16_t length = 0xaaaa;
    memset(output, 0xa5, sizeof(output));
    CHECK(ninlil_fanout_store_payload(s, output, 255u, &length) ==
              NINLIL_ERR_TOO_LARGE &&
          length == 0xaaaa && output[0] == 0xa5);
    CHECK(ninlil_fanout_store_payload(s, output, sizeof(output), &length) ==
              0 &&
          length == 256u && !memcmp(output, body, sizeof(body)));
    ninlil_fanout_store_close(s);
    ninlil_journal_close(fixture.journal);
    puts("real POSIX: actual file-capacity exhaustion, partial START "
         "continuation, UNKNOWN persistence PASS");
}
static void callback_read_failure(const char *dir)
{
    char path[256], ledger[256];
    paths(path, ledger, dir, 65u);
    ninlil_fanout_store *s;
    ninlil_fanout_store_config c = config(path, 7u);
    init_adapter(ledger);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_INITIALIZE) ==
          0);
    CHECK(ninlil_fanout_store_start(s, &contract, targets, 7u, body, 256u) ==
          0);
    hash_calls = 0u;
    hash_error_at = 2u;
    CHECK(ninlil_fanout_store_step(s, 0u, 7u) == NINLIL_ERR_BUSY);
    CHECK(!fixture.callbacks && !fixture.admitted_calls &&
          !fixture.query_calls);
    CHECK(s->owner.poisoned && s->fault == NINLIL_ERR_BUSY);
    hash_error_at = 0u;
    ninlil_fanout_store_close(s);
    CHECK(ninlil_fanout_store_open(&s, &c, NINLIL_FANOUT_STORE_RESUME) == 0);
    finish_all(s, 7u);
    ninlil_fanout_store_close(s);
    ninlil_journal_close(fixture.journal);
    puts("real POSIX: integrity-backend BUSY is NOT a per-target eligibility "
         "deferral PASS");
}
static void empty_payload(const char *dir)
{
    char path[256], ledger[256];
    paths(path, ledger, dir, 64u);
    ninlil_fanout_contract c = contract;
    ninlil_fanout_store_config cfg = config(path, 1u);
    ninlil_fanout_store *s;
    uint16_t length = 55u;
    CHECK(hash(NULL, body, 0u, c.payload_digest) == 0);
    CHECK(ninlil_fanout_store_open(&s, &cfg, NINLIL_FANOUT_STORE_INITIALIZE) ==
          0);
    CHECK(ninlil_fanout_store_start(s, &c, targets, 1u, NULL, 0u) == 0);
    CHECK(ninlil_fanout_store_payload(s, NULL, 0u, &length) == 0 && !length);
    ninlil_fanout_store_close(s);
    CHECK(ninlil_fanout_store_open(&s, &cfg, NINLIL_FANOUT_STORE_RESUME) == 0);
    CHECK(ninlil_fanout_store_payload(s, NULL, 0u, &length) == 0 && !length);
    ninlil_fanout_store_close(s);
    cfg.source_identity[0]++;
    CHECK(ninlil_fanout_store_open(&s, &cfg, NINLIL_FANOUT_STORE_RESUME) ==
              NINLIL_ERR_CONFLICT &&
          !s);
    puts("real POSIX: empty body canonical SHA-256 and source identity fence "
         "PASS");
}
int main(void)
{
    char dir[] = "/tmp/ninlil-fanout-store-XXXXXX";
    CHECK(mkdtemp(dir));
    memset(&contract, 0, sizeof(contract));
    contract.operation.bytes[0] = 1u;
    contract.authority[0] = 1u;
    contract.source[0] = 254u;
    contract.authority_epoch = contract.payload_reference = 1u;
    contract.service = 256u;
    contract.traffic = NINLIL_TRAFFIC_NORMAL;
    contract.evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    for (unsigned int i = 0u; i < 256u; i++)
        body[i] = (uint8_t)i;
    CHECK(hash(NULL, body, sizeof(body), contract.payload_digest) == 0);
    for (unsigned int i = 0u; i < 512u; i++) {
        targets[i].identity[0] = (uint8_t)((i + 1u) >> 8);
        targets[i].identity[1] = (uint8_t)(i + 1u);
        targets[i].address = (uint16_t)(i + 2u);
        targets[i].membership_epoch = targets[i].binding_epoch = 1u;
        targets[i].idempotency_key.bytes[0] = 1u;
        targets[i].idempotency_key.bytes[1] = (uint8_t)(i >> 8);
        targets[i].idempotency_key.bytes[2] = (uint8_t)i;
    }
    start_crashes(dir);
    delta_crashes(dir);
    lost_reply(dir);
    ambiguous_write(dir);
    corruption(dir);
    scale_and_bounds(dir);
    torn_start(dir);
    real_capacity_and_unknown(dir);
    empty_payload(dir);
    callback_read_failure(dir);
    /* Remove only the files this unique test directory owns, without a shell.
     */
    for (unsigned int i = 0u; i <= 65u; i++) {
        char p[256], q[256];
        paths(p, q, dir, i);
        (void)unlink(p);
        (void)unlink(q);
    }
    CHECK(rmdir(dir) == 0);
    puts("durable fanout POSIX/replay matrix PASS");
    return 0;
}
