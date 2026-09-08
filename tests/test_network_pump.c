/* Actual Core + routed + secure + pump, with bounded clock/radio/NOR fixtures.
 */
#include "../src/ninlil_wire.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "ninlil_network_pump.h"
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
static int transmit_result;
static uint32_t random_delay;
static unsigned int receive_calls;
uint32_t esp_random(void)
{
    return random_delay;
}
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
    return transmit_result;
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
    receive_calls++;
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
    if (!((source == 1u && target == 2u) || (source == 2u && target == 1u)))
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
    return length > NINLIL_SECURE_OVERHEAD && memcmp(data, "NS\001", 3u) == 0 &&
                   data[31] == 1u
               ? NINLIL_OK
               : NINLIL_ERR_INVALID;
}
static void setup(fixture *f)
{
    ninlil_routed_config routed = {0};
    ninlil_config core = {0};
    memset(f, 0, sizeof(*f));
    now_us = 100000;
    transmitted = 0u;
    random_delay = 0u;
    receive_calls = 0u;
    transmit_result = NINLIL_OK;
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
static void staged_validation(void)
{
    fixture f;
    uint8_t frame[240];
    size_t length = 0u;
    setup(&f);
    submit(&f, 2u, 150u);
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK && transmitted == 0u);
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
        if (f.pump.scheduler.jobs[i].used) {
            length = f.pump.scheduler.jobs[i].length;
            memcpy(frame, f.pump.scheduler.jobs[i].frame, length);
            break;
        }
    REQUIRE(length > 40u);
    REQUIRE(ninlil_routed_frame_current(&f.routed, frame, length) == NINLIL_OK);
    /* Authentication is verified, not only the clear fingerprint. */
    frame[length - 1u] ^= 1u;
    REQUIRE(ninlil_routed_frame_current(&f.routed, frame, length) != NINLIL_OK);
    frame[length - 1u] ^= 1u;
    now_us = 200000;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_ERR_EXPIRED);
    REQUIRE(transmitted == 0u);
    f.route_until = 150u;
    REQUIRE(ninlil_routed_frame_current(&f.routed, frame, length) ==
            NINLIL_ERR_STATE);
    cleanup(&f);
    setup(&f);
    submit(&f, 2u, 0u);
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK && transmitted == 0u);
    now_us = 200000;
    transmit_result = NINLIL_ERR_TIMEOUT;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_ERR_TIMEOUT &&
            transmitted == 1u);
    now_us = 300000;
    transmit_result = NINLIL_OK;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK && transmitted == 2u);
    REQUIRE(!f.pump.scheduler.busy);
    cleanup(&f);
    puts("staged authentication/deadline/route and ambiguous TX retry PASS");
}
static void missing_route_blocks_control(void)
{
    fixture f;
    uint8_t frame[240];
    size_t size;
    setup(&f);
    submit(&f, 3u, 0u);
    REQUIRE(ninlil_secure_seal_control(&f.sessions[0][0],
                                       (const uint8_t *)"control", 7u, frame,
                                       sizeof(frame), &size) == NINLIL_OK);
    REQUIRE(size > 0u);
    REQUIRE(ninlil_esp_network_emit(&f.pump, 2u, NINLIL_TRAFFIC_CONTROL, frame,
                                    size) == NINLIL_OK);
    for (unsigned int i = 0u; i < 10u; i++) {
        now_us += 100000;
        REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK);
    }
    REQUIRE(transmitted == 1u);
    /* A pending route commonly returns STATE rather than NOT_FOUND too. */
    puts("missing data route leaves control transmission available PASS");
    cleanup(&f);
}
static void backoff_preserves_receive_and_validation(void)
{
    fixture f;
    setup(&f);
    random_delay = 90000u;
    submit(&f, 2u, 150u);
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK);
    now_us = 110000;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK);
    REQUIRE(f.pump.scheduler.busy && transmitted == 0u);
    now_us = 140000;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_OK);
    REQUIRE(transmitted == 0u);
    now_us = 200000;
    REQUIRE(ninlil_esp_network_step(&f.pump) == NINLIL_ERR_EXPIRED);
    REQUIRE(!f.pump.scheduler.busy && transmitted == 0u);
    REQUIRE(receive_calls == 4u);
    cleanup(&f);
    puts("nonblocking randomized TX backoff revalidates the deadline PASS");
}
static int deliver_record(fixture *f, ninlil_secure_session *hop,
                          const ninlil_relay_record *record)
{
    uint8_t plain[200], frame[240];
    size_t size = ninlil_relay_encode(record, plain, sizeof(plain)), written;
    REQUIRE(size > 0u);
    REQUIRE(ninlil_secure_seal(hop, plain, size, frame, sizeof(frame),
                               &written) == NINLIL_OK);
    return ninlil_routed_receive(&f->routed, frame, written, 100u);
}
static void cached_final_requires_current_durable_evidence(void)
{
    fixture f;
    ninlil_secure_session sender[2];
    ninlil_counter_store counters[2];
    flash storage[2] = {0};
    ninlil_relay_record record = {0};
    ninlil_submission request;
    ninlil_id id = {{1}};
    uint8_t plain[64];
    size_t size, written;
    FILE *file;
    int byte;
    setup(&f);
    for (unsigned int i = 0u; i < 2u; i++) {
        ninlil_counter_config c = {{0}, 1u, 32u, 1000000u};
        ninlil_security_io io = {read_flash, write_flash, erase_flash,
                                 &storage[i], sizeof(storage[i].bytes)};
        memset(storage[i].bytes, 255, sizeof(storage[i].bytes));
        memcpy(c.session_fingerprint, f.sessions[0][i].material.fingerprint,
               16u);
        REQUIRE(ninlil_counter_open(&counters[i], &io,
                                    NINLIL_COUNTER_CREATE_NEW,
                                    &c) == NINLIL_OK);
        REQUIRE(ninlil_secure_open(&sender[i], &f.sessions[0][i].material,
                                   &counters[i], ninlil_psa_aead(), 2u, 1u,
                                   1u) == NINLIL_OK);
    }
    ninlil_submission_defaults(&request);
    request.target = 1u;
    request.service = 0x100u;
    request.payload_len = 5u;
    size = ninlil_wire_encode_data(plain, 2u, &request, &id,
                                   (const uint8_t *)"value");
    REQUIRE(ninlil_secure_seal(&sender[0], plain, size, record.ciphertext,
                               sizeof(record.ciphertext),
                               &written) == NINLIL_OK);
    record.length = (uint16_t)written;
    record.route_epoch = 1u;
    record.path.count = 2u;
    record.path.nodes[0] = 2u;
    record.path.nodes[1] = 1u;
    record.path.membership_epochs[0] = record.path.membership_epochs[1] = 1u;
    REQUIRE(ninlil_psa_packet_digest(record.ciphertext, written,
                                     record.packet_id) == NINLIL_OK);
    REQUIRE(deliver_record(&f, &sender[1], &record) == NINLIL_OK);
    REQUIRE(deliver_record(&f, &sender[1], &record) == NINLIL_OK);
    REQUIRE(ninlil_verify_retained(f.core) == NINLIL_OK);
    f.sessions[0][0].material.fingerprint[0] ^= 1u;
    REQUIRE(deliver_record(&f, &sender[1], &record) == NINLIL_ERR_UNAUTHORIZED);
    f.sessions[0][0].material.fingerprint[0] ^= 1u;
    file = fopen(f.path, "r+b");
    REQUIRE(file != NULL);
    REQUIRE(fseek(file, -1L, SEEK_END) == 0);
    byte = fgetc(file);
    REQUIRE(byte != EOF);
    REQUIRE(fseek(file, -1L, SEEK_END) == 0);
    REQUIRE(fputc(byte ^ 1, file) != EOF);
    REQUIRE(fclose(file) == 0);
    REQUIRE(deliver_record(&f, &sender[1], &record) == NINLIL_ERR_CORRUPT);
    for (unsigned int i = 0u; i < 2u; i++) {
        ninlil_secure_close(&sender[i]);
        ninlil_counter_close(&counters[i]);
    }
    cleanup(&f);
    puts("cached final ACK requires current crypto and durable CRC PASS");
}
static void source_retries_keep_custody_identity(void)
{
    fixture f;
    ninlil_relay_record records[2];
    uint8_t counters[2][5];
    unsigned int found = 0u;
    setup(&f);
    submit(&f, 2u, 0u);
    REQUIRE(ninlil_set_retry_interval(f.core, 1u) == NINLIL_OK);
    for (unsigned int i = 0u; i < 4u; i++)
        REQUIRE(ninlil_step(f.core) == NINLIL_OK);
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX && found < 2u; i++) {
        const ninlil_airtime_job *job = &f.pump.scheduler.jobs[i];
        uint8_t plain[200];
        size_t length = 0u;
        if (!job->used)
            continue;
        REQUIRE(ninlil_secure_inspect_tx(&f.sessions[0][1], job->frame,
                                         job->length, plain, sizeof(plain),
                                         &length) == NINLIL_OK);
        REQUIRE(ninlil_relay_decode(plain, length, &records[found]) ==
                NINLIL_OK);
        memcpy(counters[found], job->frame + 24, 5u);
        found++;
    }
    REQUIRE(found == 2u);
    REQUIRE(memcmp(records[0].packet_id, records[1].packet_id, 16u) == 0);
    REQUIRE(memcmp(counters[0], counters[1], 5u) != 0);
    for (unsigned int change = 0u; change < 2u; change++) {
        REQUIRE(ninlil_airtime_open(&f.pump.scheduler, 0u, 1000000u, 0u) ==
                NINLIL_OK);
        if (!change)
            f.sessions[0][0].material.fingerprint[0] ^= 1u;
        else {
            f.route_until = 100000u;
            f.routed.now_ms = NINLIL_ROUTED_RETRY_WINDOW_MS + 1u;
        }
        REQUIRE(ninlil_step(f.core) == NINLIL_OK);
        found = 0u;
        for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++) {
            const ninlil_airtime_job *job = &f.pump.scheduler.jobs[i];
            uint8_t plain[200];
            size_t length = 0u;
            if (!job->used)
                continue;
            REQUIRE(ninlil_secure_inspect_tx(&f.sessions[0][1], job->frame,
                                             job->length, plain, sizeof(plain),
                                             &length) == NINLIL_OK);
            REQUIRE(ninlil_relay_decode(plain, length, &records[1]) ==
                    NINLIL_OK);
            REQUIRE(memcmp(records[0].packet_id, records[1].packet_id, 16u) !=
                    0);
            found++;
        }
        REQUIRE(found > 0u);
        records[0] = records[1];
    }
    cleanup(&f);
    puts("Core retry preserves opaque custody identity; fresh session changes "
         "it PASS");
}

int main(void)
{
    static const uint8_t empty_hash[16] = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc,
                                           0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
                                           0x99, 0x6f, 0xb9, 0x24};
    uint8_t digest[16];
    REQUIRE(psa_crypto_init() == PSA_SUCCESS);
    REQUIRE(ninlil_psa_packet_digest(NULL, 0u, digest) == NINLIL_OK);
    REQUIRE(memcmp(digest, empty_hash, 16u) == 0);
    staged_validation();
    missing_route_blocks_control();
    backoff_preserves_receive_and_validation();
    cached_final_requires_current_durable_evidence();
    source_retries_keep_custody_identity();
    return 0;
}
