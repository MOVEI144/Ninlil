#include "ninlil_internal.h"
#include "ninlil_retry.h"
#include "test_support.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)
typedef struct fixture {
    ninlil_runtime *core;
    ninlil_config config;
    test_policy policy;
    unsigned int sent[2];
    uint32_t rng;
    int result;
    char directory[128], path[256];
} fixture;
static int send_frame(void *ctx, const uint8_t *data, size_t size)
{
    fixture *f = ctx;
    ninlil_wire_data_view view;
    if (ninlil_wire_decode_data(data, size, &view) || view.target < 2u ||
        view.target > 3u)
        return NINLIL_ERR_INVALID;
    f->sent[view.target - 2u]++;
    return f->result;
}
static int receive_none(void *ctx, uint8_t *p, size_t cap, size_t *size)
{
    (void)ctx;
    (void)p;
    (void)cap;
    (void)size;
    return 0;
}
static int setup(fixture *f, int spool)
{
    const ninlil_retry_policy p = {1000u, 160u, 30000u};
    memset(f, 0, sizeof(*f));
    f->rng = 17u;
    CHECK(test_make_directory(f->directory, sizeof(f->directory)) == 0);
    CHECK(test_make_path(f->path, sizeof(f->path), f->directory, "retry") == 0);
    test_policy_init(&f->policy, 0x100u, 32u);
    f->config.node_id = 1u;
    f->config.journal_location = f->path;
    f->config.max_work_per_step = 8u;
    f->config.retry_interval_steps = 1u;
    f->config.link = (ninlil_link){send_frame, receive_none, f, 320u};
    f->config.policy_lookup = test_policy_lookup;
    f->config.policy_ctx = &f->policy;
    f->config.random = (ninlil_random){test_rng_fill, &f->rng};
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &f->config.profile) == 0);
    if (spool)
        f->config.spool = (ninlil_spool_limits){64u, 8u, 96u, 65536u};
    CHECK(ninlil_open(&f->core, &f->config) == 0);
    CHECK(ninlil_retry_enable(f->core, &p, 0u) == 0);
    return 0;
}
static void cleanup(fixture *f)
{
    ninlil_close(f->core);
    test_remove_directory(f->directory, f->path, NULL);
}
static int submit(fixture *f, uint16_t target, ninlil_id *id)
{
    ninlil_submission s;
    ninlil_submission_defaults(&s);
    s.target = target;
    s.service = 0x100u;
    s.payload = (const uint8_t *)"value";
    s.payload_len = 5u;
    test_fill_id(&s.idempotency_key, (uint8_t)target);
    return ninlil_submit(f->core, &s, id);
}
static int timed(int spool)
{
    fixture f;
    ninlil_id a, b;
    ninlil_info info;
    ninlil_retry_info timing, unchanged;
    CHECK(setup(&f, spool) == 0);
    CHECK(submit(&f, 2u, &a) == 0 && submit(&f, 3u, &b) == 0);
    CHECK(ninlil_retry_set_message(f.core, &a, 100u) == 0);
    CHECK(ninlil_retry_set_message(f.core, &b, 5000u) == 0);
    CHECK(ninlil_retry_tx_done(f.core, &a, 0u) == NINLIL_ERR_STATE);
    CHECK(ninlil_step_at(f.core, 0u) == 0);
    CHECK(f.sent[0] == 1u && f.sent[1] == 1u);
    CHECK(ninlil_retry_query(f.core, &a, &timing) == 0);
    CHECK(timing.waiting_for_tx && timing.next_due_ms == 30000u &&
          timing.rto_ms == 100u);
    unchanged = timing;
    CHECK(ninlil_retry_query(f.core, &(ninlil_id){{0}}, &timing) ==
          NINLIL_ERR_NOT_FOUND);
    CHECK(memcmp(&unchanged, &timing, sizeof(timing)) == 0);
    for (unsigned int i = 0u; i < 1000u; i++)
        CHECK(ninlil_step_at(f.core, 0u) == 0);
    CHECK(f.sent[0] == 1u && f.sent[1] == 1u);
    CHECK(ninlil_step(f.core) == NINLIL_ERR_STATE);
    CHECK(ninlil_step_at(f.core, 10000u) == 0); /* Queue delay is NOT an RTO. */
    CHECK(f.sent[0] == 1u && f.sent[1] == 1u);
    CHECK(ninlil_retry_tx_done(f.core, &a, 10000u) == 0);
    CHECK(ninlil_retry_tx_done(f.core, &b, 10000u) == 0);
    CHECK(ninlil_retry_tx_done(f.core, &a, 10000u) == NINLIL_ERR_STATE);
    CHECK(ninlil_step_at(f.core, 10100u) == 0);
    CHECK(f.sent[0] == 2u && f.sent[1] == 1u);
    CHECK(ninlil_step_at(f.core, 15000u) == 0);
    CHECK(f.sent[0] == 2u && f.sent[1] == 2u);
    CHECK(ninlil_step_at(f.core, 14999u) == NINLIL_ERR_STATE);
    CHECK(ninlil_step_at(f.core, UINT64_MAX) == NINLIL_ERR_STATE);
    CHECK(f.core->retry_now_ms == 15000u);
    CHECK(ninlil_query(f.core, &a, &info) == 0 &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(info.latest_evidence == NINLIL_EVIDENCE_NONE);
    /* Restart resets timers, not durable identity/attempt/evidence. */
    ninlil_close(f.core);
    CHECK(ninlil_open(&f.core, &f.config) == 0);
    CHECK(ninlil_retry_enable(
              f.core, &(ninlil_retry_policy){1000u, 160u, 30000u}, 0u) == 0);
    CHECK(ninlil_query(f.core, &a, &info) == 0 &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(ninlil_step_at(f.core, 0u) == 0);
    CHECK(f.sent[0] == 3u && f.sent[1] == 3u);
    CHECK(ninlil_step_at(f.core, 29999u) == 0 && f.sent[0] == 3u);
    CHECK(ninlil_step_at(f.core, 30000u) == 0 &&
          f.sent[0] == 4u); /* Staging watchdog. */
    cleanup(&f);
    return 0;
}
static int blocked(int spool)
{
    fixture f;
    ninlil_id id;
    CHECK(setup(&f, spool) == 0 && submit(&f, 2u, &id) == 0);
    f.result = NINLIL_ERR_BUSY;
    CHECK(ninlil_step_at(f.core, 0u) == NINLIL_ERR_BUSY && f.sent[0] == 1u);
    for (unsigned int i = 0u; i < 100u; i++)
        CHECK(ninlil_step_at(f.core, 0u) == 0);
    CHECK(ninlil_step_at(f.core, 159u) == 0 && f.sent[0] == 1u);
    CHECK(ninlil_step_at(f.core, 160u) == NINLIL_ERR_BUSY && f.sent[0] == 2u);
    cleanup(&f);
    return 0;
}
int main(void)
{
    CHECK(timed(0) == 0 && timed(1) == 0 && blocked(0) == 0 && blocked(1) == 0);
    puts(
        "monotonic per-message RTO / staged vs TX_DONE / spool / restart PASS");
    return 0;
}
