/* Actual Core and journal; peer binding/policy and Link are explicit models.
 * 511 retained contracts do NOT represent 511 radio sessions or RF receivers.
 */
#include "ninlil_internal.h"
#include "ninlil_maintenance.h"
#include "test_support.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)
#define TARGETS 511u

typedef struct fixture {
    ninlil_runtime *core;
    ninlil_config config;
    test_policy policy;
    ninlil_id ids[TARGETS];
    unsigned int sent[TARGETS], calls;
    uint32_t random;
    uint16_t blocked, rebound;
    int fault;
    uint8_t last_packet[NINLIL_WIRE_DATA_MAX];
    size_t last_length;
    char directory[128], journal[256];
} fixture;
static int binding(void *ctx, uint16_t peer, ninlil_delivery_binding *out)
{
    fixture *f = ctx;
    ninlil_delivery_binding b = {0};
    if (peer < 2u || peer > TARGETS + 1u)
        return NINLIL_ERR_NOT_FOUND;
    b.source_identity[0] = 251u;
    b.peer_identity[0] = (uint8_t)(peer >> 8);
    b.peer_identity[1] = (uint8_t)peer;
    b.authority[0] = 77u;
    b.authority_epoch = 1u;
    b.membership_epoch = b.binding_epoch = peer == f->rebound ? 2u : 1u;
    *out = b;
    return NINLIL_OK;
}
static int send_frame(void *ctx, const uint8_t *data, size_t length)
{
    fixture *f = ctx;
    ninlil_wire_data_view v;
    if (ninlil_wire_decode_data(data, length, &v) != NINLIL_OK ||
        v.target < 2u || v.target > TARGETS + 1u ||
        length > sizeof(f->last_packet))
        return f->fault = NINLIL_ERR_CORRUPT;
    f->calls++;
    memcpy(f->last_packet, data, length);
    f->last_length = length;
    if (v.target - 2u < f->blocked)
        return NINLIL_ERR_BUSY;
    f->sent[v.target - 2u]++;
    return NINLIL_OK; /* Link handoff, not delivery evidence. */
}
static int receive_none(void *ctx, uint8_t *p, size_t capacity, size_t *length)
{
    (void)ctx;
    (void)p;
    (void)capacity;
    (void)length;
    return 0;
}
static int setup(fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->random = 199u;
    CHECK(test_make_directory(f->directory, sizeof(f->directory)) == 0);
    CHECK(test_make_path(f->journal, sizeof(f->journal), f->directory,
                         "spool") == 0);
    test_policy_init(&f->policy, 256u, 4u);
    f->config.journal_location = f->journal;
    f->config.node_id = 1u;
    f->config.random = (ninlil_random){test_rng_fill, &f->random};
    f->config.link =
        (ninlil_link){send_frame, receive_none, f, NINLIL_WIRE_DATA_MAX};
    f->config.policy_lookup = test_policy_lookup;
    f->config.policy_ctx = &f->policy;
    f->config.binding_lookup = binding;
    f->config.binding_ctx = f;
    f->config.max_work_per_step = 4u;
    f->config.retry_interval_steps = 1024u;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &f->config.profile) == 0);
    f->config.spool = (ninlil_spool_limits){576u, 32u, 608u, 262144u};
    CHECK(ninlil_open(&f->core, &f->config) == 0);
    CHECK(ninlil_bind_storage(f->core, (uint8_t[32]){251u}, 1) == 0);
    return 0;
}
static int submit(fixture *f, unsigned int i, ninlil_id *out)
{
    ninlil_submission s;
    ninlil_delivery_binding b;
    ninlil_submission_defaults(&s);
    s.target = (uint16_t)(i + 2u);
    s.service = 256u;
    s.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    s.payload = (const uint8_t *)"retained";
    s.payload_len = 8u;
    s.idempotency_key.bytes[0] = 1u;
    s.idempotency_key.bytes[1] = (uint8_t)(i >> 8);
    s.idempotency_key.bytes[2] = (uint8_t)i;
    CHECK(binding(f, s.target, &b) == 0);
    return ninlil_submit_bound(f->core, &s, &b, out);
}
static int tick(fixture *f, unsigned int steps)
{
    for (unsigned int i = 0u; i < steps; i++) {
        int rc = ninlil_step(f->core);
        CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_BUSY ||
              rc == NINLIL_ERR_UNAUTHORIZED);
        CHECK(!f->fault && ninlil_health(f->core) == 0);
    }
    return 0;
}
static int verify_contracts(fixture *f)
{
    for (unsigned int i = 0u; i < TARGETS; i++) {
        ninlil_info info;
        ninlil_delivery_binding b, saved;
        ninlil_id duplicate;
        CHECK(ninlil_query(f->core, &f->ids[i], &info) == 0);
        CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE && info.peer == i + 2u);
        CHECK(info.required_evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
        CHECK(info.latest_evidence == NINLIL_EVIDENCE_NONE);
        CHECK(ninlil_query_binding(f->core, &f->ids[i], &saved) == 0);
        CHECK(binding(f, (uint16_t)(i + 2u), &b) == 0);
        CHECK(ninlil_delivery_binding_equal(&saved, &b));
        CHECK(submit(f, i, &duplicate) == 0 &&
              ninlil_id_equal(&duplicate, &f->ids[i]));
    }
    return 0;
}
static int large_backlog(void)
{
    fixture f;
    ninlil_service_info state;
    CHECK(setup(&f) == 0);
    for (unsigned int i = 0u; i < TARGETS; i++)
        CHECK(submit(&f, i, &f.ids[i]) == 0);
    f.blocked = 32u;
    CHECK(tick(&f, 700u) == 0);
    for (unsigned int i = 0u; i < TARGETS; i++)
        CHECK(f.sent[i] == (i < 32u ? 0u : 1u));
    CHECK(ninlil_service_query(f.core, &f.ids[0], &state) == 0);
    CHECK(state.owned_outbound == TARGETS && state.service_capacity == 32u &&
          state.servicing <= 32u);
    CHECK(verify_contracts(&f) == 0);
    CHECK(ninlil_collect(f.core) == 0);
    ninlil_close(f.core);
    /* Smaller limits fail closed; they do not discard the retained prefix. */
    f.config.spool.owned_outbound = 64u;
    f.config.spool.total_owned = 96u;
    CHECK(ninlil_open(&f.core, &f.config) != 0 && !f.core);
    f.config.spool.owned_outbound = 576u;
    f.config.spool.total_owned = 608u;
    CHECK(ninlil_open(&f.core, &f.config) == 0);
    CHECK(verify_contracts(&f) == 0);
    memset(f.sent, 0, sizeof(f.sent));
    f.blocked = 0u;
    f.rebound = 2u;
    CHECK(tick(&f, 700u) == 0);
    CHECK(!f.sent[0]);
    for (unsigned int i = 1u; i < TARGETS; i++)
        CHECK(f.sent[i] == 1u);
    f.rebound = 0u;
    CHECK(tick(&f, 40u) == 0 && f.sent[0] == 1u);
    CHECK(ninlil_service_pause(f.core, &f.ids[0]) == 0);
    ninlil_close(f.core);
    CHECK(ninlil_open(&f.core, &f.config) == 0);
    CHECK(ninlil_service_query(f.core, &f.ids[0], &state) == 0 &&
          state.state == NINLIL_SERVICE_ELIGIBLE);
    CHECK(verify_contracts(&f) == 0);
    ninlil_close(f.core);
    test_remove_directory(f.directory, f.journal, NULL);
    puts("511 actual Core contracts, 32 hot slots: blocked prefix bypass, "
         "replay, binding fence PASS");
    return 0;
}
static int paused_staged_packet(void)
{
    fixture f;
    uint8_t receipt[NINLIL_WIRE_RECEIPT_SIZE];
    ninlil_info info;
    CHECK(setup(&f) == 0);
    CHECK(submit(&f, 0u, &f.ids[0]) == 0 && tick(&f, 1u) == 0);
    CHECK(f.sent[0] == 1u && f.last_length);
    CHECK(ninlil_transmit_check(f.core, f.last_packet, f.last_length) == 0);
    CHECK(ninlil_service_pause(f.core, &f.ids[0]) == 0);
    CHECK(ninlil_transmit_check(f.core, f.last_packet, f.last_length) ==
          NINLIL_ERR_BUSY);
    CHECK(ninlil_service_resume(f.core, &f.ids[0]) == 0);
    CHECK(ninlil_transmit_check(f.core, f.last_packet, f.last_length) == 0);
    CHECK(ninlil_service_pause(f.core, &f.ids[0]) == 0);
    size_t size = ninlil_wire_encode_receipt(
        receipt, 2u, 1u, &f.ids[0], NINLIL_RECEIPT_EVIDENCE,
        NINLIL_EVIDENCE_APPLICATION_ACCEPTED);
    CHECK(ninlil_ingest(f.core, receipt, size) == 0);
    CHECK(ninlil_query(f.core, &f.ids[0], &info) == 0 &&
          info.outcome == NINLIL_OUTCOME_SATISFIED);
    CHECK(ninlil_release_bound(f.core, &f.ids[0]) == 0);
    CHECK(submit(&f, 1u, &f.ids[1]) == 0);
    ninlil_service_info state;
    CHECK(ninlil_service_query(f.core, &f.ids[1], &state) == 0 &&
          state.state == NINLIL_SERVICE_ELIGIBLE);
    /* The cold index is scanned, not rescanned from zero on every request. */
    unsigned int rounds =
        (state.owned_capacity + NINLIL_SERVICE_SCAN_MAX - 1u) /
        NINLIL_SERVICE_SCAN_MAX;
    CHECK(tick(&f, rounds + 1u) == 0 && f.sent[1] == 1u);
    ninlil_close(f.core);
    test_remove_directory(f.directory, f.journal, NULL);
    puts("pause fences already staged DATA, retains receipt progress, no "
         "reused-slot pause PASS");
    return 0;
}
static int invalid_limits(void)
{
    fixture f;
    ninlil_config c;
    ninlil_runtime *out = NULL;
    struct stat st;
    CHECK(setup(&f) == 0);
    c = f.config;
    char missing[256];
    CHECK(test_make_path(missing, sizeof(missing), f.directory, "invalid") ==
          0);
    c.journal_location = missing;
    const ninlil_spool_limits bad[] = {
        {0u, 1u, 0u, 0u},           {1025u, 1u, 1025u, 262144u},
        {576u, 33u, 608u, 262144u}, {576u, 32u, 575u, 262144u},
        {576u, 32u, 609u, 262144u}, {576u, 32u, 608u, 1048577u}};
    for (unsigned int i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        c.spool = bad[i];
        CHECK(ninlil_open(&out, &c) == NINLIL_ERR_INVALID && !out);
        CHECK(stat(missing, &st) != 0);
    }
    c.spool =
        (ninlil_spool_limits){1024u, 32u, 1056u, c.profile.dram_ceiling_bytes};
    CHECK(ninlil_open(&out, &c) == NINLIL_ERR_CAPACITY && !out &&
          stat(missing, &st) != 0);
    ninlil_close(f.core);
    test_remove_directory(f.directory, f.journal, NULL);
    puts("invalid/over-budget spool configuration rejected before journal "
         "creation PASS");
    return 0;
}
int main(void)
{
    CHECK(large_backlog() == 0);
    CHECK(paused_staged_packet() == 0);
    CHECK(invalid_limits() == 0);
    return 0;
}
