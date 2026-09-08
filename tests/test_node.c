#define _POSIX_C_SOURCE 200809L
#include "ninlil_enrollment.h"
#include "ninlil_identity_file.h"
#include "ninlil_node_internal.h"
#include "security_test_io.h"
#include "test_node_bulk.h"
#include "test_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
#ifndef NODES
#define NODES 3u
#endif
#define QUEUE 64u
typedef struct packet {
    uint8_t bytes[240];
    size_t length;
} packet;
typedef struct device {
    ninlil_node *node;
    ninlil_node_config config;
    ninlil_identity identity;
    ninlil_identity_file identity_file;
    ninlil_identity_io identity_io;
    uint64_t boot_at;
    uint64_t tx_at;
    flash counters[NODES * 2u];
    packet queue[QUEUE];
    unsigned int head, count;
    char identity_dir[128], core_dir[128], control_dir[128];
    char core_path[256], control_path[256];
} device;
static device devices[NODES];
static ninlil_node_member members[NODES];
static flash era_flash;
static ninlil_counter_store eras;
static uint64_t now;
static unsigned int delivered_app, transmissions;
static unsigned int drain_reports, remove_reports;
static unsigned int drop_drain_ack = 1u, drop_remove_ack = 1u;
static int weak_direct = 1;
static int direct_bootstrap;
static int root_relay;
static int lose_join_ack;
static int slow_radio;
static int hold_final;
static int drop_core_receipt = 1;
static unsigned int lose_every;
static uint32_t timing_seed = 1u;
#include "test_node_enrollment.h"
static int random_fill(void *ctx, uint8_t *bytes, size_t length)
{
    (void)ctx;
    /* Reproducible scheduling/IDs only. PSA still generates all private and
     * ephemeral cryptographic keys; production Node entropy is unchanged. */
    for (size_t i = 0u; i < length; i++) {
        timing_seed ^= timing_seed << 13;
        timing_seed ^= timing_seed >> 17;
        timing_seed ^= timing_seed << 5;
        bytes[i] = (uint8_t)timing_seed;
    }
    return NINLIL_OK;
}
static int counter_io(void *ctx, uint16_t slot, ninlil_security_io *io)
{
    device *d = ctx;
    if (slot >= NODES * 2u)
        return NINLIL_ERR_INVALID;
    *io = (ninlil_security_io){read_flash, write_flash, erase_flash,
                               &d->counters[slot],
                               sizeof(d->counters[slot].bytes)};
    return NINLIL_OK;
}
static int emit(void *ctx, uint16_t peer, ninlil_traffic_class traffic,
                const uint8_t *frame, size_t length)
{
    device *d = ctx;
    packet *p;
    (void)traffic;
    CHECK(peer > 0u && peer <= NODES && length <= 240u);
    /* Measurement traffic must not consume reserved protocol-control slots. */
    if (length == 240u && memcmp(frame, "NS\001", 3u) == 0 && frame[31] == 2u)
        CHECK(traffic == NINLIL_TRAFFIC_NORMAL);
    if (d->count == QUEUE || (slow_radio && d->count >= 6u))
        return NINLIL_ERR_CAPACITY;
    /* Like the physical scheduler, keep capacity for local protocol replies. */
    if (slow_radio && traffic >= NINLIL_TRAFFIC_NORMAL && d->count >= 2u)
        return NINLIL_ERR_CAPACITY;
    p = &d->queue[(d->head + d->count) % QUEUE];
    memcpy(p->bytes, frame, length);
    p->length = length;
    d->count++;
    return NINLIL_OK;
}
static void setup(void)
{
    ninlil_security_io era_io = {read_flash, write_flash, erase_flash,
                                 &era_flash, sizeof(era_flash.bytes)};
    ninlil_counter_config counter = {{1}, 0u, 1u, UINT32_MAX - 1u};
    CHECK(psa_crypto_init() == PSA_SUCCESS);
    memset(era_flash.bytes, 255, sizeof(era_flash.bytes));
    CHECK(ninlil_counter_open(&eras, &era_io, NINLIL_COUNTER_CREATE_NEW,
                              &counter) == NINLIL_OK);
    for (unsigned int i = 0u; i < NODES; i++) {
        device *d = &devices[i];
        ninlil_identity_io io;
        ninlil_join_grant *g = &members[i].grant;
        CHECK(test_make_directory(d->identity_dir, sizeof(d->identity_dir)) ==
              0);
        CHECK(test_make_directory(d->core_dir, sizeof(d->core_dir)) == 0);
        CHECK(test_make_directory(d->control_dir, sizeof(d->control_dir)) == 0);
        CHECK(test_make_path(d->core_path, sizeof(d->core_path), d->core_dir,
                             "core") == 0);
        CHECK(test_make_path(d->control_path, sizeof(d->control_path),
                             d->control_dir, "control") == 0);
        CHECK(ninlil_identity_file_open(&d->identity_file, d->identity_dir,
                                        &io) == NINLIL_OK);
        CHECK(ninlil_identity_provision(&d->identity, io) == NINLIL_OK);
        d->identity_io = io;
        memcpy(g->identity, d->identity.identity, 32u);
        memcpy(members[i].public_key, d->identity.public_key, 65u);
        memset(g->authority, 7, 16u);
        g->node = (uint16_t)(i + 1u);
        g->membership_epoch = g->binding_epoch = 1u;
        g->role = i == 0u   ? NINLIL_ROLE_SITE_GATEWAY
                  : i == 1u ? NINLIL_ROLE_POWERED_RELAY_CANDIDATE
                            : NINLIL_ROLE_POWERED_ENDPOINT;
        g->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
        if (i == 1u || (root_relay && i == 0u))
            g->capabilities |= NINLIL_CAP_RELAY_CUSTODY;
        g->service_count = 1u;
        g->services[0] =
            (ninlil_service_grant){256u, 64u, 16u, NINLIL_SERVICE_BOTH, 15u};
    }
    for (unsigned int i = 0u; i < NODES; i++) {
        device *d = &devices[i];
        ninlil_node_config *c = &d->config;
        c->local = (uint16_t)(i + 1u);
        c->root = 1u;
        c->members = members;
        c->member_count = NODES;
        enrollment_config(c, i);
        c->identity = &d->identity;
        c->journal_location = d->core_path;
        c->control_location = d->control_path;
        c->control_max_bytes = 1048576u;
        c->permitted_profile = 1u;
        CHECK(ninlil_role_profile_standard(members[i].grant.role,
                                           &c->resources) == NINLIL_OK);
        c->random.fill = random_fill;
        c->root_eras = &eras;
        c->counter_io = counter_io;
        c->counter_ctx = c->emit_ctx = d;
        c->emit = emit;
        CHECK(ninlil_node_open(&d->node, c, 0u) == NINLIL_ERR_CORRUPT);
        CHECK(ninlil_node_provision_stores(c) == NINLIL_OK);
        CHECK(ninlil_node_open(&d->node, c, 0u) == NINLIL_OK);
    }
}
static void tick(void)
{
    now += 20u;
    for (unsigned int i = 0u; i < NODES; i++)
        if (devices[i].node)
            CHECK(ninlil_node_step(devices[i].node, now - devices[i].boot_at) ==
                  NINLIL_OK);
    for (unsigned int i = 0u; i < NODES; i++) {
        device *d = &devices[i];
        packet p;
        if (!d->node || !d->count || (slow_radio && now < d->tx_at))
            continue;
        d->tx_at = now + 500u;
        p = d->queue[d->head];
        d->head = (d->head + 1u) % QUEUE;
        d->count--;
        if (ninlil_node_frame_current(d->node, p.bytes, p.length,
                                      now - d->boot_at) != NINLIL_OK)
            continue;
        transmissions++;
        ninlil_node_transmitted(d->node, p.bytes, p.length, NINLIL_OK, 380000u,
                                now - d->boot_at);
        if (memcmp(p.bytes, "NB\001", 3u) == 0 && (p.bytes[3] & 31u) == 5u) {
            uint8_t plain[200];
            size_t size = 0u;
            int peer = ninlil_node_index(
                d->node, (uint16_t)ninlil_node_get(p.bytes + 6, 2u));
            if (peer >= 0 &&
                ninlil_secure_inspect_tx(&d->node->peers[peer].sessions[0],
                                         p.bytes + 16, p.length - 16u, plain,
                                         sizeof(plain), &size) == NINLIL_OK &&
                size) {
                if (plain[0] == NODE_CORE_RECEIPT && drop_core_receipt) {
                    drop_core_receipt = 0;
                    continue;
                }
                if (plain[0] == NODE_EFFECTIVE_ACK && i == 2u &&
                    getenv("NINLIL_TEST_LOST_EFFECTIVE_ACK"))
                    continue;
                if (plain[0] == NODE_DRAIN)
                    drain_reports++;
                if (plain[0] == NODE_REMOVE_READY)
                    remove_reports++;
                if (plain[0] == NODE_DRAIN_ACK && drop_drain_ack) {
                    drop_drain_ack = 0u;
                    continue;
                }
                if (plain[0] == NODE_REMOVE_READY_ACK && drop_remove_ack) {
                    drop_remove_ack = 0u;
                    continue;
                }
                if (lose_join_ack && plain[0] == NODE_JOIN_ACK)
                    continue;
            }
        }
        for (unsigned int j = 0u; j < NODES; j++) {
            if (i == j || !devices[j].node ||
                (lose_every && transmissions % lose_every == 0u) ||
                (hold_final && j == 2u && memcmp(p.bytes, "NS\001", 3u) == 0 &&
                 p.bytes[31] == 0u) ||
                (weak_direct &&
                 (!direct_bootstrap || memcmp(p.bytes, "NB\001", 3u) != 0) &&
                 ((i == 0u && j == 2u) || (i == 2u && j == 0u))))
                continue;
            (void)ninlil_node_receive(devices[j].node, p.bytes, p.length,
                                      now - devices[j].boot_at);
        }
    }
}
static void wait_ms(uint64_t duration)
{
    uint64_t until = now + duration;
    while (now < until)
        tick();
}
static void show(void)
{
    for (unsigned int i = 0u; i < NODES; i++) {
        ninlil_node *n = devices[i].node;
        ninlil_node_status status;
        if (ninlil_node_inspect(devices[i].node, &status) != NINLIL_OK)
            continue;
        fprintf(
            stderr,
            "node=%u time=%llu joined=%u peers=%u fault=%d handshake=%u/%u\n",
            i + 1u, (unsigned long long)now, status.joined,
            status.active_members, status.fault, status.handshake_peer,
            status.handshake_stage);
        fprintf(stderr,
                "clock=%u pending=%llu/%u prepare=%u apply=%u proof=%u "
                "wants=%u ready=%u authority=%u local=%u\n",
                status.lease_clock_ready,
                (unsigned long long)devices[i].node->coordinator.pending.epoch,
                devices[i].node->coordinator.pending.phase,
                devices[i].node->coordinator.prepared_live,
                devices[i].node->coordinator.applied_live,
                devices[i].node->proof_mask, status.wanted_routes,
                status.effective_routes, status.authority_routes,
                status.local_routes);
        for (unsigned int j = 0u; j < NINLIL_NETWORK_FLOWS_MAX; j++) {
            if (n->local_plans[j].epoch)
                fprintf(stderr,
                        " flow=%u epoch=%llu retired=%llu ready=%u notice=%u "
                        "proofepoch=%llu live=%u phase=%u end=%llu\n",
                        j, (unsigned long long)n->local_plans[j].epoch,
                        (unsigned long long)n->retired[j], n->local_ready[j],
                        n->effective_notified[j],
                        (unsigned long long)n->effective_epoch[j],
                        n->coordinator.flows[j].reconciled,
                        n->local_plans[j].phase,
                        (unsigned long long)n->local_plans[j].valid_until_ms);
        }
        for (unsigned int j = 0u; j < n->config.member_count; j++) {
            node_peer *p = &devices[i].node->peers[j];
            if (j != n->local_index)
                fprintf(stderr,
                        " peer=%u attempts=%u delivered=%u bits=%u age=%llu "
                        "active=%u secure=%u drain=%u\n",
                        n->members[j].grant.node, p->attempts, p->delivered,
                        p->probe_window,
                        (unsigned long long)(devices[i].node->now_ms -
                                             p->observed_at),
                        p->member_active, p->sessions[0].ready, p->draining);
        }
        if (i == 0u)
            for (unsigned int e = 0u; e < NODE_EDGES_MAX; e++) {
                const ninlil_network_edge *edge = &devices[i].node->edges[e];
                if (edge->used)
                    fprintf(
                        stderr, " edge=%u-%u delivery=%u epoch=%llu age=%llu\n",
                        edge->from, edge->to, edge->delivered,
                        (unsigned long long)edge->membership_epoch,
                        (unsigned long long)(devices[i].node->clock.last_ms -
                                             edge->observed_ms));
            }
    }
}
static void restart(unsigned int index)
{
    device *d = &devices[index];
    CHECK(ninlil_node_collect(d->node) == NINLIL_OK);
    ninlil_node_close(d->node);
    d->node = NULL;
    ninlil_identity_close(&d->identity);
    CHECK(ninlil_identity_open(&d->identity, d->identity_io) == NINLIL_OK);
    d->head = d->count = 0u;
    d->boot_at = now;
    CHECK(ninlil_node_open(&d->node, &d->config, 0u) == NINLIL_OK);
    CHECK(!d->node->peers[index ? 0u : 1u].sessions[0].ready);
}
static void restart_bulk_receiver(void)
{
    restart(2u);
}
static int owned_delivery(const ninlil_id *id)
{
    ninlil_node *n = devices[1].node;
    for (unsigned int i = 0u; i < n->relay.capacity; i++) {
        const ninlil_relay_record *record = &n->custody[i].record;
        uint8_t plain[200];
        size_t length = 0u;
        if (n->custody[i].used && record->path.nodes[0] == 1u &&
            record->path.nodes[record->path.count - 1u] == 3u &&
            ninlil_secure_inspect_tx(&devices[0].node->peers[2].sessions[0],
                                     record->ciphertext, record->length, plain,
                                     sizeof(plain), &length) == NINLIL_OK &&
            length >= 40u && plain[3] == 1u &&
            memcmp(plain + 10, id->bytes, NINLIL_ID_BYTES) == 0)
            return (int)i;
    }
    return -1;
}
static void awaiting_effective_preserves_ciphertext(unsigned int custody)
{
    ninlil_node *n = devices[1].node;
    int slot = ninlil_node_local_plan(n, 1u, 3u, 0);
    uint8_t saved[NINLIL_NETWORK_PATH_MAX][16];
    uint8_t ready;
    CHECK(custody < n->relay.capacity);
    n->member_cursor = (uint8_t)custody;
    if (slot < 0) {
        n->recovery_at = 0u;
        CHECK(ninlil_node_recovery_step(n) == NINLIL_ERR_BUSY);
        return;
    }
    ready = n->local_ready[slot];
    memcpy(saved, n->plan_bindings[slot], sizeof(saved));
    /* Missing EFFECTIVE confirmation does not make owned DATA obsolete. */
    n->local_ready[slot] = 1u;
    memset(n->plan_bindings[slot], 0, sizeof(saved));
    n->recovery_at = 0u;
    CHECK(ninlil_node_recovery_step(n) == NINLIL_ERR_BUSY);
    n->local_ready[slot] = ready;
    memcpy(n->plan_bindings[slot], saved, sizeof(saved));
}

static void delivery(unsigned int sequence, unsigned int restart_mode)
{
    ninlil_submission request;
    ninlil_id id;
    ninlil_info info;
    uint8_t payload[64] = {42};
    uint64_t until = now + 180000u;
    unsigned int before = delivered_app;
    int restarted = 0;
    ninlil_submission_defaults(&request);
    request.idempotency_key.bytes[0] = (uint8_t)sequence;
    request.target = 3u;
    request.service = 256u;
    request.payload = payload;
    request.payload_len = sizeof(payload);
    request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    hold_final = restart_mode ? 1 : 0;
    CHECK(ninlil_submit(devices[0].node->core, &request, &id) == NINLIL_OK);
    while (now < until) {
        ninlil_inbound inbound;
        tick();
        if (restart_mode && !restarted) {
            ninlil_node_status status;
            int custody = owned_delivery(&id);
            CHECK(ninlil_node_inspect(devices[1].node, &status) == NINLIL_OK);
            if (custody >= 0) {
                CHECK(ninlil_query(devices[0].node->core, &id, &info) ==
                      NINLIL_OK);
                CHECK(info.outcome == NINLIL_OUTCOME_ACTIVE);
                if (restart_mode == 3u) {
                    weak_direct = 0;
                    CHECK(ninlil_node_drain(devices[1].node) == NINLIL_OK);
                    /* No usable lease remains. Draining must still return
                     * owned ciphertext to its authenticated durable source. */
                    wait_ms(61000u);
                } else if (restart_mode == 1u) {
                    awaiting_effective_preserves_ciphertext(
                        (unsigned int)custody);
                    restart(1u);
                    CHECK(ninlil_node_inspect(devices[1].node, &status) ==
                          NINLIL_OK);
                    CHECK(status.relay_owned > 0u);
                    restart(2u);
                } else
                    enrollment_restart(restart_mode, &id);
                restarted = 1;
                hold_final = 0;
            }
        }
        if (ninlil_receive(devices[2].node->core, &inbound) == NINLIL_OK) {
            CHECK(inbound.payload_len == sizeof(payload) &&
                  memcmp(inbound.payload, payload, sizeof(payload)) == 0);
            CHECK(ninlil_application_accept(devices[2].node->core,
                                            &inbound.message_id) == NINLIL_OK);
            delivered_app++;
        }
        CHECK(ninlil_query(devices[0].node->core, &id, &info) == NINLIL_OK);
        if (info.outcome == NINLIL_OUTCOME_SATISFIED)
            break;
    }
    if (now >= until) {
        show();
        fprintf(stderr,
                "message outcome=%u evidence=%u applications=%u before=%u\n",
                info.outcome, info.latest_evidence, delivered_app, before);
    }
    CHECK(now < until && delivered_app == before + 1u);
    CHECK(!restart_mode || restarted);
    wait_ms(5000u);
    printf("Autonomous sequence %u restart mode %u PASS\n", sequence,
           restart_mode);
}
static void replacement_waits_for_effective(ninlil_node *n, unsigned int slot)
{
    ninlil_node *copy;
    ninlil_network_plan plan = n->local_plans[slot];
    uint8_t proof[48];
    uint64_t lease;
    if (n->config.local != n->config.root)
        return;
    copy = malloc(sizeof(*copy));
    CHECK(copy != NULL);
    memcpy(copy, n, sizeof(*copy));
    for (unsigned int i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        ninlil_network_plan *active = &copy->coordinator.flows[i].active;
        uint8_t request[13] = {0};
        if (!active->epoch)
            continue;
        ninlil_node_put(request, active->path.nodes[0], 2u);
        ninlil_node_put(request + 2,
                        active->path.nodes[active->path.count - 1u], 2u);
        request[12] = 1u;
        for (unsigned int other = 0u; other < 2u; other++) {
            copy->proof_epoch = active->epoch + other;
            copy->proof_mask = 7u;
            ninlil_node_put(request + 4, UINT64_MAX - 1u + other, 8u);
            CHECK(ninlil_node_plan_receive(copy, active->path.nodes[1],
                                           NODE_FLOW_REQUEST, request,
                                           sizeof(request)) == NINLIL_OK);
            CHECK(copy->proof_epoch == active->epoch + other);
            CHECK(copy->proof_mask == (other ? 7u : 0u));
            CHECK(!copy->effective_notified[i]);
        }
    }
    CHECK(ninlil_node_lease(copy, &lease) == NINLIL_OK);
    copy->now_ms += plan.valid_until_ms - lease + 1u;
    plan.epoch = copy->local_plan_epoch + 1u;
    plan.valid_until_ms += NINLIL_NETWORK_LEASE_MAX_MS;
    plan.phase = NINLIL_PLAN_COMMITTED;
    copy->prepared = plan;
    copy->prepared.phase = NINLIL_PLAN_STAGED;
    copy->prepared.prepared = copy->prepared.applied = 0u;
    /* Root-local apply is pure in this isolated copy; borrowed IO/keys are
     * neither used nor closed. The old lease has expired before replacement. */
    CHECK(ninlil_node_apply(copy, &plan, proof) == NINLIL_OK);
    CHECK(copy->local_ready[slot] == 1u);
    free(copy);
}
static void delayed_apply_preserves_effective(void)
{
    unsigned int checked = 0u;
    for (unsigned int i = 0u; i < NODES; i++) {
        ninlil_node *n = devices[i].node;
        for (unsigned int j = 0u; j < NINLIL_NETWORK_FLOWS_MAX; j++) {
            ninlil_network_plan p = n->local_plans[j];
            uint8_t proof[48];
            uint64_t lease;
            if (n->local_ready[j] != 3u ||
                ninlil_node_lease(n, &lease) != NINLIL_OK ||
                p.valid_until_ms <= lease)
                continue;
            p.phase = NINLIL_PLAN_COMMITTED;
            CHECK(ninlil_node_apply(n, &p, proof) == NINLIL_OK);
            CHECK(n->local_ready[j] == 3u);
            replacement_waits_for_effective(n, j);
            checked++;
        }
    }
    CHECK(checked > 0u);
}
static void delayed_probe_keeps_response_window(void)
{
    device *d = &devices[0];
    node_peer *p = &d->node->peers[1];
    packet queued;
    uint64_t token;
    unsigned int at = (d->head + d->count) % QUEUE;
    p->probe_report = 0u;
    p->probe_at = d->node->now_ms;
    CHECK(ninlil_node_links_step(d->node) == NINLIL_OK);
    queued = d->queue[at];
    CHECK(queued.length == 240u);
    d->count--;
    token = p->probe_token;
    /* Scheduler/CCA delayed a measurement until just before its old timer.
     * TX_DONE is the start of the response window, regardless of admission. */
    now += 9990u;
    ninlil_node_transmitted(d->node, queued.bytes, queued.length, NINLIL_OK,
                            380000u, now - d->boot_at);
    now += 20u;
    CHECK(ninlil_node_step(d->node, now - d->boot_at) == NINLIL_OK);
    CHECK(p->probe_token == token && p->probe_sent &&
          p->probe_at >= d->node->now_ms + 3000u);
}
static void lifecycle(void)
{
    ninlil_node_status status;
    ninlil_network_plan plan;
    uint64_t lease;
    packet staged;
    uint8_t bytes[NINLIL_JOIN_RECORD_MAX];
    size_t size;
    unsigned int at;
    weak_direct = 0;
    CHECK(ninlil_node_drain(devices[1].node) == NINLIL_OK);
    CHECK(!ninlil_node_ready_remove(devices[1].node));
    wait_ms(90000u);
    if (!ninlil_node_ready_remove(devices[1].node))
        show();
    CHECK(ninlil_node_ready_remove(devices[1].node));
    CHECK(ninlil_node_inspect(devices[1].node, &status) == NINLIL_OK);
    CHECK(status.relay_owned == 0u);
    CHECK(ninlil_node_drain(devices[1].node) == NINLIL_OK);
    CHECK(ninlil_node_ready_remove(devices[1].node));
    {
        unsigned int reports = drain_reports, notices = remove_reports;
        wait_ms(20000u);
        CHECK(drain_reports == reports && remove_reports == notices);
    }
    delivery(4u, 0u);
    CHECK(ninlil_node_lease(devices[0].node, &lease) == NINLIL_OK);
    CHECK(ninlil_node_route(devices[0].node, 1u, 3u, lease, &plan) ==
          NINLIL_OK);
    CHECK(plan.path.count == 2u);
    CHECK(ninlil_node_relay_drain(devices[1].node, 0) == NINLIL_OK);
    CHECK(!ninlil_node_ready_remove(devices[1].node));
    wait_ms(10000u);
    {
        unsigned int reports = drain_reports;
        wait_ms(20000u);
        CHECK(drain_reports == reports);
    }
    restart(0u);
    wait_ms(90000u);
    CHECK(devices[0].node->peers[1].drain_epoch ==
          devices[1].node->relay.drain_epoch);
    CHECK(!devices[0].node->peers[1].draining);
    CHECK(ninlil_node_revoke(devices[2].node, 2u, 1u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_node_revoke(devices[0].node, 2u, 2u) == NINLIL_ERR_CONFLICT);
    size = ninlil_join_encode(
        &ninlil_node_authority_peer(devices[0].node, 1u)->record, bytes,
        sizeof(bytes));
    at = (devices[0].head + devices[0].count) % QUEUE;
    CHECK(ninlil_node_control_send(devices[0].node, 2u, NODE_JOIN_ACTIVE, bytes,
                                   size) == NINLIL_OK);
    staged = devices[0].queue[at];
    CHECK(ninlil_node_revoke(devices[0].node, 2u, 1u) == NINLIL_OK);
    CHECK(ninlil_node_frame_current(devices[0].node, staged.bytes,
                                    staged.length, now - devices[0].boot_at) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(!devices[0].node->peers[1].revocation_applied);
    wait_ms(6000u);
    CHECK(devices[0].node->peers[1].revocation_applied);
    CHECK(!devices[1].node->joined &&
          devices[1].node->endpoint.record.state == NINLIL_JOIN_REVOKED);
    restart(1u);
    restart(0u);
    wait_ms(90000u);
    CHECK(devices[0].node->peers[1].revocation_applied);
    CHECK(!devices[1].node->joined &&
          devices[1].node->endpoint.record.state == NINLIL_JOIN_REVOKED);
    delivery(5u, 0u);
    puts("Autonomous alternate route, drain/resume and durable remote "
         "revocation PASS");
}
static void rotate(void)
{
    for (unsigned int i = 0u; i < NODES; i++) {
        ninlil_node_close(devices[i].node);
        devices[i].node = NULL;
        devices[i].head = devices[i].count = 0u;
        devices[i].boot_at = now;
    }
    for (unsigned int i = 0u; i < 2u; i++) {
        CHECK(ninlil_identity_rotate(&devices[i].identity,
                                     devices[i].identity.generation) ==
              NINLIL_OK);
        memcpy(members[i].public_key, devices[i].identity.public_key, 65u);
    }
    CHECK(ninlil_node_open(&devices[0].node, &devices[0].config, 0u) ==
          NINLIL_ERR_UNAUTHORIZED);
    for (unsigned int i = 0u; i < 2u; i++)
        members[i].grant.membership_epoch = members[i].grant.binding_epoch = 2u;
    for (unsigned int i = 0u; i < NODES; i++)
        CHECK(ninlil_node_open(&devices[i].node, &devices[i].config, 0u) ==
              NINLIL_OK);
    weak_direct = 1;
    lose_every = 13u;
    wait_ms(120000u);
    for (unsigned int i = 0u; i < NODES; i++)
        CHECK(devices[i].node->joined);
    delivery(6u, 0u);
    puts("Explicit authority/relay key rotation and higher-epoch reenrollment "
         "under RF loss PASS");
}
int main(int argc, char **argv)
{
    (void)argv;
    if (getenv("NINLIL_TEST_SEED")) {
        char *end;
        unsigned long seed = strtoul(getenv("NINLIL_TEST_SEED"), &end, 10);
        CHECK(!*end && seed > 0u && seed <= UINT32_MAX);
        timing_seed = (uint32_t)seed;
    }
    printf("Node scheduling seed: %u\n", timing_seed);
    direct_bootstrap = argc > 1;
    root_relay = argc > 2;
    lose_join_ack = argc > 3;
    slow_radio = argc > 4 || getenv("NINLIL_TEST_SLOW_RADIO") != NULL;
    setup();
    if (getenv("NINLIL_TEST_ENROLLMENT")) {
        enrollment_run();
        return 0;
    }
    if (root_relay) {
        CHECK(ninlil_node_drain(devices[0].node) == NINLIL_OK);
        restart(0u);
        wait_ms(70000u);
        CHECK(ninlil_node_ready_remove(devices[0].node));
        CHECK(ninlil_node_relay_drain(devices[0].node, 0) == NINLIL_OK);
        CHECK(!ninlil_node_ready_remove(devices[0].node));
    }
    wait_ms(120000u);
    for (unsigned int i = 0u; i < NODES; i++)
        CHECK(devices[i].node->joined);
    if (argc > 5)
        delivery(7u,
                 0u); /* Bounded radio remains congested through route setup. */
    lose_join_ack = 0;
    slow_radio = 0;
    delayed_probe_keeps_response_window();
    {
        device *d = &devices[2];
        uint8_t request[8] = {0};
        packet queued;
        unsigned int at = (d->head + d->count) % QUEUE;
        ninlil_node_put(request, 123u, 8u);
        CHECK(ninlil_lease_request(&d->node->clock, 123u, now - d->boot_at) ==
              NINLIL_OK);
        CHECK(ninlil_node_control_send(d->node, 1u, NODE_CLOCK_REQUEST, request,
                                       8u) == NINLIL_OK);
        queued = d->queue[at];
        CHECK(ninlil_lease_request(&d->node->clock, 124u, now - d->boot_at) ==
              NINLIL_OK);
        CHECK(ninlil_node_frame_current(d->node, queued.bytes, queued.length,
                                        now - d->boot_at) == NINLIL_ERR_STATE);
    }
    delivery(1u, 0u);
    delayed_apply_preserves_effective();
    if (argc == 1) {
        slow_radio = 1;
        test_node_bulk(devices[0].node, &devices[2].node, tick,
                       restart_bulk_receiver);
        slow_radio = 0;
    }
    delivery(2u, 1u);
    delivery(3u, 2u);
    lifecycle();
    rotate();
    delivery(8u, 3u);
    wait_ms(90000u);
    CHECK(ninlil_node_ready_remove(devices[1].node));
    /* Even an explicit repeat of provisioning cannot erase history by
     * blessing a missing Core or authority log as a new empty store. */
    for (unsigned int i = 0u; i < 2u; i++) {
        device *d = &devices[i];
        ninlil_node_close(d->node);
        d->node = NULL;
        CHECK(unlink(i ? d->core_path : d->control_path) == 0);
        CHECK(ninlil_node_provision_stores(&d->config) == NINLIL_ERR_CORRUPT);
    }
    for (unsigned int i = 0u; i < NODES; i++) {
        device *d = &devices[i];
        ninlil_node_close(d->node);
        ninlil_identity_close(&d->identity);
        ninlil_identity_file_close(&d->identity_file);
        test_remove_directory(d->identity_dir, "identity.bin",
                              ".identity.lock");
        test_remove_directory(d->core_dir, "core", NULL);
        test_remove_directory(d->control_dir, "control", NULL);
    }
    ninlil_counter_close(&eras);
    printf("Autonomous three-node actual EDHOC/Core/relay delivery PASS: %u "
           "frames\n",
           transmissions);
    return 0;
}
