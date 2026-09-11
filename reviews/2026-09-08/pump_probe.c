/* Actual Core + routed + secure + pump, with bounded clock/radio/NOR fixtures.
 */
#include "esp_timer.h"
#include "ninlil_control_fragment.h"
#include "ninlil_network_pump.h"
#include "ninlil_wire.h"
#include "security_test_io.h"
#include "test_support.h"
#include <psa/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
typedef struct fixture {
    ninlil_routed routed;
    ninlil_esp_network_pump pump;
    ninlil_sx1262_radio radio;
    ninlil_runtime *core;
    ninlil_link link;
    ninlil_secure_session sessions[2][2];
    ninlil_counter_store counters[2][2];
    flash storage[2][2];
    test_policy policy;
    uint32_t rng;
    uint64_t route_until;
    char directory[128], path[256];
} fixture;
static int64_t now_us;
static unsigned int transmitted;
static uint8_t transmitted_frame[240];
static uint16_t transmitted_length;
int64_t esp_timer_get_time(void)
{
    return now_us;
}
int ninlil_sx1262_radio_airtime(const ninlil_sx1262_radio *r, uint16_t length,
                                uint32_t *out)
{
    (void)r;
    if (!length || length > 240u)
        return NINLIL_ERR_INVALID;
    *out = 1000u;
    return NINLIL_OK;
}
int ninlil_sx1262_radio_send(ninlil_sx1262_radio *r, const uint8_t *data,
                             uint16_t length)
{
    (void)r;
    REQUIRE(data && length > 0u && length <= sizeof(transmitted_frame));
    memcpy(transmitted_frame, data, length);
    transmitted_length = length;
    transmitted++;
    return NINLIL_OK;
}
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *r, uint8_t *data,
                                uint16_t capacity, uint16_t *length,
                                ninlil_sx1262_rx_info *info, TickType_t wait)
{
    (void)r;
    (void)data;
    (void)capacity;
    (void)length;
    (void)info;
    (void)wait;
    return NINLIL_ERR_EMPTY;
}
static ninlil_secure_session *session(void *ctx, uint16_t peer, int hop)
{
    fixture *f = ctx;
    if (peer < 2u || peer > 3u || hop < 0 || hop > 1)
        return NULL;
    return &f->sessions[peer - 2u][hop];
}
static int route(void *ctx, uint16_t source, uint16_t target, uint64_t now,
                 ninlil_network_plan *out)
{
    fixture *f = ctx;
    if (source != 1u || target != 2u)
        return NINLIL_ERR_NOT_FOUND;
    if (now >= f->route_until)
        return NINLIL_ERR_STATE;
    memset(out, 0, sizeof(*out));
    out->epoch = 1u;
    out->path.count = 2u;
    out->path.nodes[0] = source;
    out->path.nodes[1] = target;
    out->path.membership_epochs[0] = out->path.membership_epochs[1] = 1u;
    out->path.cost_us = 1000u;
    out->valid_until_ms = f->route_until;
    out->phase = NINLIL_PLAN_EFFECTIVE;
    out->prepared = out->applied = 3u;
    out->profile = 1u;
    out->rto_ms = 100u;
    return NINLIL_OK;
}
static int clock_now(void *ctx, uint64_t *now, ninlil_time_quality *quality)
{
    (void)ctx;
    *now = (uint64_t)now_us / 1000u;
    *quality = NINLIL_TIME_RESTART_SAFE;
    return NINLIL_OK;
}
static int control_current(void *ctx, const uint8_t *data, size_t length,
                           uint64_t now)
{
    (void)ctx;
    (void)now;
    return length > 16u && memcmp(data, "NF\001", 3u) == 0 ? NINLIL_OK
                                                           : NINLIL_ERR_INVALID;
}
static void setup(fixture *f)
{
    ninlil_routed_config routed = {0};
    ninlil_config core = {0};
    memset(f, 0, sizeof(*f));
    now_us = 100000;
    transmitted = 0u;
    f->rng = 10u;
    f->route_until = 5000u;
    test_policy_init(&f->policy, 0x100u, 32u);
    for (unsigned int peer = 0u; peer < 2u; peer++) {
        for (unsigned int hop = 0u; hop < 2u; hop++) {
            ninlil_session_material material;
            ninlil_counter_config counter = {0};
            ninlil_security_io io = {read_flash, write_flash, erase_flash,
                                     &f->storage[peer][hop],
                                     NINLIL_SECURITY_PARTITION_SIZE};
            memset(&material, (int)(40u + peer * 4u + hop), sizeof(material));
            material.keys[1][0]++;
            memset(f->storage[peer][hop].bytes, 255,
                   NINLIL_SECURITY_PARTITION_SIZE);
            memcpy(counter.session_fingerprint, material.fingerprint, 16u);
            counter.reservation_size = 32u;
            counter.max_counter_exclusive = 1000000u;
            REQUIRE(ninlil_counter_open(&f->counters[peer][hop], &io,
                                        NINLIL_COUNTER_CREATE_NEW,
                                        &counter) == NINLIL_OK);
            REQUIRE(ninlil_secure_open(&f->sessions[peer][hop], &material,
                                       &f->counters[peer][hop],
                                       ninlil_psa_aead(), 1u,
                                       (uint16_t)(peer + 2u), 0u) == NINLIL_OK);
            REQUIRE(ninlil_secure_bind_membership(&f->sessions[peer][hop], 1u,
                                                  1u) == NINLIL_OK);
        }
    }
    routed.local = 1u;
    routed.policy = test_policy_lookup;
    routed.policy_ctx = &f->policy;
    routed.session = session;
    routed.session_ctx = f;
    routed.route = route;
    routed.route_ctx = f;
    routed.emit = ninlil_esp_network_emit;
    routed.emit_ctx = &f->pump;
    routed.digest = ninlil_psa_packet_digest;
    REQUIRE(ninlil_routed_open(&f->routed, &routed, &f->link) == NINLIL_OK);
    REQUIRE(ninlil_esp_network_open(&f->pump, &f->radio, &f->routed,
                                    1000000u) == NINLIL_OK);
    f->pump.control_current = control_current;
    REQUIRE(test_make_directory(f->directory, sizeof(f->directory)) == 0);
    REQUIRE(test_make_path(f->path, sizeof(f->path), f->directory, "core") ==
            0);
    core.node_id = 1u;
    core.journal_location = f->path;
    core.max_work_per_step = 1u;
    core.retry_interval_steps = 1024u;
    core.link = f->link;
    core.policy_lookup = test_policy_lookup;
    core.policy_ctx = &f->policy;
    core.random.fill = test_rng_fill;
    core.random.ctx = &f->rng;
    core.clock.now = clock_now;
    REQUIRE(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                         &core.profile) == NINLIL_OK);
    REQUIRE(ninlil_open(&f->core, &core) == NINLIL_OK);
    ninlil_routed_attach(&f->routed, f->core);
}
static void cleanup(fixture *f)
{
    ninlil_close(f->core);
    test_remove_directory(f->directory, f->path, NULL);
    for (unsigned int peer = 0u; peer < 2u; peer++)
        for (unsigned int hop = 0u; hop < 2u; hop++) {
            ninlil_secure_close(&f->sessions[peer][hop]);
            ninlil_counter_close(&f->counters[peer][hop]);
        }
}
static void submit(fixture *f, uint16_t peer, uint64_t deadline)
{
    ninlil_submission s;
    ninlil_id id;
    ninlil_submission_defaults(&s);
    test_fill_id(&s.idempotency_key, 1u);
    s.target = peer;
    s.service = 0x100u;
    s.absolute_deadline_ms = deadline;
    s.payload = (const uint8_t *)"value";
    s.payload_len = 5u;
    REQUIRE(ninlil_submit(f->core, &s, &id) == NINLIL_OK);
}
static void staged_deadline(void)
{
    fixture f;
    ninlil_secure_session hop, end;
    ninlil_relay_record relay;
    ninlil_wire_data_view data;
    uint8_t plain[200];
    size_t length;
    setup(&f);
    submit(&f, 2u, 150u);
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK && transmitted == 0u);
    now_us = 200000;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK && transmitted == 1u);
    hop = f.sessions[0][1];
    hop.local = 2u;
    hop.peer = 1u;
    hop.direction = 1u;
    end = f.sessions[0][0];
    end.local = 2u;
    end.peer = 1u;
    end.direction = 1u;
    REQUIRE(ninlil_secure_unseal(&hop, transmitted_frame, transmitted_length,
                                 plain, sizeof(plain), &length) == NINLIL_OK);
    REQUIRE(ninlil_relay_decode(plain, length, &relay) == NINLIL_OK);
    REQUIRE(ninlil_secure_unseal(&end, relay.ciphertext, relay.length, plain,
                                 sizeof(plain), &length) == NINLIL_OK);
    REQUIRE(ninlil_wire_decode_data(plain, length, &data) == NINLIL_OK);
    REQUIRE(data.absolute_deadline_ms == 150u);
    puts("REPRODUCED staging: DATA with deadline=150 admitted at=100 reaches "
         "radio send at=200");
    f.route_until = 150u;
    REQUIRE(route(&f, 1u, 2u, 200u, &(ninlil_network_plan){0}) ==
            NINLIL_ERR_STATE);
    REQUIRE(ninlil_routed_frame_current(&f.routed, transmitted_frame,
                                        transmitted_length) == NINLIL_OK);
    puts("REPRODUCED staged route validation: expired route is rejected by "
         "lookup but the same queued frame remains current");
    cleanup(&f);
}
static void missing_route_blocks_control(void)
{
    fixture f;
    uint8_t frame[240];
    size_t size;
    setup(&f);
    submit(&f, 3u, 0u);
    size = ninlil_control_fragment(1u, 1u, (const uint8_t *)"handshake", 9u, 0u,
                                   frame, sizeof(frame));
    REQUIRE(size > 0u);
    REQUIRE(ninlil_esp_network_emit(&f.pump, 2u, NINLIL_TRAFFIC_CONTROL, frame,
                                    size) == NINLIL_OK);
    for (unsigned int i = 0u; i < 10u; i++) {
        now_us += 100000;
        REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_ERR_NOT_FOUND);
    }
    REQUIRE(transmitted == 0u);
    /* A pending route commonly returns STATE rather than NOT_FOUND too. */
    puts("REPRODUCED pump: one Core message without a route blocks all 10 TX "
         "opportunities for a queued current control fragment");
    cleanup(&f);
}
int main(void)
{
    REQUIRE(psa_crypto_init() == PSA_SUCCESS);
    staged_deadline();
    missing_route_blocks_control();
    return 0;
}
