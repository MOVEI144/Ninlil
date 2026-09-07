#include "ninlil_network.h"
#include "ninlil_relay.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check %d: %s\n", __LINE__, #x);                   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct fixture {
    ninlil_network_plan plans[64];
    ninlil_relay_record records[64];
    size_t plan_count, record_count;
    uint16_t revoked;
    int fail;
} fixture;

static int policy(void *ctx, uint16_t peer, ninlil_peer_policy *p)
{
    fixture *f = ctx;
    if (peer == 0u || peer > 4u || peer == f->revoked)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(p, 0, sizeof(*p));
    p->role = NINLIL_ROLE_POWERED_ENDPOINT;
    p->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    if (peer == 2u || peer == 3u)
        p->capabilities |= NINLIL_CAP_RELAY_CUSTODY;
    p->membership_epoch = 1u;
    p->session_membership_epoch = 1u;
    return NINLIL_OK;
}
static int plan_commit(void *ctx, const ninlil_network_plan *p)
{
    fixture *f = ctx;
    if (f->plan_count >= 64u)
        return NINLIL_ERR_CAPACITY;
    f->plans[f->plan_count++] = *p;
    return f->fail ? NINLIL_ERR_IO : NINLIL_OK;
}
static int relay_commit(void *ctx, const ninlil_relay_record *p)
{
    fixture *f = ctx;
    if (f->record_count >= 64u)
        return NINLIL_ERR_CAPACITY;
    f->records[f->record_count++] = *p;
    return f->fail ? NINLIL_ERR_IO : NINLIL_OK;
}
static int relay_verify(void *ctx, const ninlil_relay_record *record)
{
    fixture *f = ctx;
    uint8_t a[NINLIL_RELAY_FRAME_MAX], b[NINLIL_RELAY_FRAME_MAX];
    size_t i, size = ninlil_relay_encode(record, a, sizeof(a));
    if (!size)
        return NINLIL_ERR_CORRUPT;
    for (i = f->record_count; i > 0u; i--) {
        const ninlil_relay_record *old = &f->records[i - 1u];
        if (old->control || memcmp(old->packet_id, record->packet_id, 16u) != 0)
            continue;
        if (ninlil_relay_encode(old, b, sizeof(b)) != size ||
            memcmp(a, b, size) != 0)
            return NINLIL_ERR_CORRUPT;
        return NINLIL_OK;
    }
    return NINLIL_ERR_NOT_FOUND;
}

static int observe(ninlil_coordinator *c, uint16_t from, uint16_t to,
                   uint16_t delivered, uint32_t airtime)
{
    ninlil_network_edge e;
    memset(&e, 0, sizeof(e));
    e.from = from;
    e.to = to;
    e.attempts = 10u;
    e.delivered = delivered;
    e.airtime_us = airtime;
    e.observed_ms = 100u;
    e.membership_epoch = 1u;
    return ninlil_coordinator_observe(c, from, &e, 100u);
}
static int install(ninlil_coordinator *c, ninlil_network_path *path,
                   uint64_t now)
{
    unsigned int i;
    uint64_t epoch;
    CHECK(ninlil_coordinator_stage(c, path, now, now + 50000u,
                                   NINLIL_TIME_RESTART_SAFE) == NINLIL_OK);
    epoch = c->pending.epoch;
    for (i = 0u; i < path->count; i++)
        CHECK(ninlil_coordinator_prepared(c, path->nodes[i], epoch) ==
              NINLIL_OK);
    CHECK(ninlil_coordinator_activate(c, now + 1u, NINLIL_TIME_RESTART_SAFE,
                                      1) == NINLIL_OK);
    for (i = 0u; i < path->count; i++)
        CHECK(ninlil_coordinator_applied(c, path->nodes[i], epoch) ==
              NINLIL_OK);
    CHECK(ninlil_coordinator_route_check(c, path, epoch, now + 2u) ==
          NINLIL_OK);
    return 0;
}

int main(void)
{
    ninlil_coordinator c;
    ninlil_network_node nodes[4];
    ninlil_network_edge edges[8];
    ninlil_network_path chosen, repaired, direct;
    ninlil_relay r;
    ninlil_relay_slot slots[2];
    ninlil_relay_record packet, got, decoded;
    ninlil_remove_status removal;
    fixture f;
    uint8_t wire[NINLIL_RELAY_FRAME_MAX];
    uint16_t next;
    size_t i, n;
    uint64_t epoch;
    memset(&f, 0, sizeof(f));
    CHECK(ninlil_coordinator_open(&c, nodes, 4u, edges, 8u, 1u, policy, &f,
                                  plan_commit, &f) == NINLIL_OK);
    CHECK(observe(&c, 1u, 4u, 1u, 400000u) == NINLIL_OK);
    CHECK(observe(&c, 1u, 2u, 10u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 2u, 3u, 10u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 3u, 4u, 10u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 2u, 4u, 1u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 0u, 4u, 10u, 100000u) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_coordinator_select(&c, 1u, 4u, 0u, 101u, &chosen) ==
          NINLIL_OK);
    CHECK(chosen.count == 4u && chosen.cost_us == 300000u);
    CHECK(ninlil_coordinator_stage(&c, &chosen, 102u, 50000u,
                                   NINLIL_TIME_RUNTIME_ONLY) ==
          NINLIL_ERR_STATE);
    CHECK(ninlil_coordinator_stage(&c, &chosen, 102u, 50000u,
                                   NINLIL_TIME_RESTART_SAFE) == NINLIL_OK);
    epoch = c.pending.epoch;
    for (i = 0u; i < 3u; i++)
        CHECK(ninlil_coordinator_prepared(&c, chosen.nodes[i], epoch) ==
              NINLIL_OK);
    CHECK(ninlil_coordinator_activate(&c, 103u, NINLIL_TIME_RESTART_SAFE, 1) ==
          NINLIL_ERR_STATE);
    CHECK(ninlil_coordinator_prepared(&c, 4u, epoch) == NINLIL_OK);
    CHECK(ninlil_coordinator_activate(&c, 103u, NINLIL_TIME_RESTART_SAFE, 1) ==
          NINLIL_OK);
    for (i = 0u; i < 3u; i++)
        CHECK(ninlil_coordinator_applied(&c, chosen.nodes[i], epoch) ==
              NINLIL_OK);
    CHECK(ninlil_coordinator_route_check(&c, &chosen, epoch, 104u) !=
          NINLIL_OK);
    CHECK(ninlil_coordinator_applied(&c, 4u, epoch) == NINLIL_OK);
    CHECK(ninlil_coordinator_route_check(&c, &chosen, epoch, 104u) ==
          NINLIL_OK);
    CHECK(ninlil_coordinator_open(&c, nodes, 4u, edges, 8u, 1u, policy, &f,
                                  plan_commit, &f) == NINLIL_OK);
    for (i = 0u; i < f.plan_count; i++)
        CHECK(ninlil_coordinator_restore(&c, &f.plans[i]) == NINLIL_OK);
    CHECK(ninlil_coordinator_route_check(&c, &chosen, epoch, 105u) ==
          NINLIL_ERR_STATE);
    for (i = 0u; i < 4u; i++)
        CHECK(ninlil_coordinator_reconcile(&c, chosen.nodes[i], epoch) ==
              NINLIL_OK);
    CHECK(ninlil_coordinator_route_check(&c, &chosen, epoch, 106u) ==
          NINLIL_OK);
    CHECK(observe(&c, 1u, 4u, 1u, 400000u) == NINLIL_OK);
    CHECK(observe(&c, 1u, 2u, 10u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 2u, 3u, 10u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 3u, 4u, 10u, 100000u) == NINLIL_OK);
    CHECK(observe(&c, 2u, 4u, 1u, 100000u) == NINLIL_OK);
    CHECK(ninlil_relay_open(&r, slots, 2u, 2u, NINLIL_ROLE_BATTERY_LEAF,
                            NINLIL_CAP_RELAY_CUSTODY, 100u, policy, &f,
                            relay_commit, relay_verify, &f,
                            ninlil_coordinator_route_check,
                            &c) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_relay_open(&r, slots, 2u, 2u, NINLIL_ROLE_POWERED_ENDPOINT,
                            NINLIL_CAP_RELAY_CUSTODY, 100u, policy, &f,
                            relay_commit, relay_verify, &f,
                            ninlil_coordinator_route_check, &c) == NINLIL_OK);
    memset(&packet, 0, sizeof(packet));
    packet.path = chosen;
    packet.route_epoch = epoch;
    packet.packet_id[0] = 1u;
    packet.length = 100u;
    memset(packet.ciphertext, 0xA5, packet.length);
    n = ninlil_relay_encode(&packet, wire, sizeof(wire));
    CHECK(n == 148u);
    CHECK(ninlil_relay_decode(wire, n, &decoded) == NINLIL_OK);
    CHECK(ninlil_relay_receive(&r, 4u, &decoded, 110u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_relay_receive(&r, 1u, &decoded, 110u) == NINLIL_OK &&
          f.record_count == 1u);
    CHECK(ninlil_relay_receive(&r, 1u, &decoded, 111u) == NINLIL_OK &&
          f.record_count == 1u);
    CHECK(ninlil_relay_ack(&r, 3u, packet.packet_id, epoch) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_relay_next(&r, 112u, &next, &got) == NINLIL_OK && next == 3u);
    CHECK(ninlil_relay_drain(&r, 1) == NINLIL_OK);
    CHECK(!ninlil_relay_ready_remove(&r));
    CHECK(ninlil_relay_open(&r, slots, 2u, 2u, NINLIL_ROLE_POWERED_ENDPOINT,
                            NINLIL_CAP_RELAY_CUSTODY, 100u, policy, &f,
                            relay_commit, relay_verify, &f,
                            ninlil_coordinator_route_check, &c) == NINLIL_OK);
    for (i = 0u; i < f.record_count; i++)
        CHECK(ninlil_relay_restore(&r, &f.records[i]) == NINLIL_OK);
    CHECK(r.draining && !ninlil_relay_ready_remove(&r));
    f.revoked = 3u;
    CHECK(ninlil_relay_next(&r, 120u, &next, &got) == NINLIL_ERR_EMPTY &&
          slots[0].used);
    CHECK(ninlil_coordinator_select(&c, 1u, 4u, 3u, 120u, &repaired) ==
              NINLIL_OK &&
          repaired.count == 3u);
    CHECK(install(&c, &repaired, 121u) == 0);
    CHECK(ninlil_relay_repair(&r, packet.packet_id, &repaired, c.active.epoch,
                              124u) == NINLIL_OK);
    CHECK(ninlil_relay_ack(&r, 3u, packet.packet_id, epoch) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_relay_next(&r, 125u, &next, &got) == NINLIL_OK && next == 4u);
    CHECK(memcmp(got.ciphertext, packet.ciphertext, packet.length) == 0);
    CHECK(ninlil_relay_ack(&r, 4u, packet.packet_id, c.active.epoch) ==
          NINLIL_OK);
    CHECK(ninlil_relay_ready_remove(&r));
    CHECK(ninlil_coordinator_remove_status(&c, 2u, 126u, 1, &removal) ==
          NINLIL_OK);
    CHECK(!removal.ready && removal.dependent_flows == 1u &&
          removal.reroutable_flows == 1u);
    CHECK(ninlil_coordinator_select(&c, 1u, 4u, 2u, 126u, &direct) ==
              NINLIL_OK &&
          direct.count == 2u);
    CHECK(install(&c, &direct, 127u) == 0);
    CHECK(ninlil_coordinator_remove_status(&c, 2u, 130u, 1, &removal) ==
              NINLIL_OK &&
          removal.ready);
    ninlil_coordinator_enable(&c, 0);
    CHECK(ninlil_coordinator_select(&c, 1u, 4u, 0u, 130u, &chosen) ==
          NINLIL_ERR_STATE);
    CHECK(ninlil_coordinator_route_check(&c, &direct, c.active.epoch, 130u) ==
          NINLIL_OK);
    CHECK(ninlil_network_rto(1000u, 100u, 1, 100u) == 1000u);
    CHECK(ninlil_coordinator_retire(&c, 1u, 4u, c.active.epoch - 1u, 130u,
                                    NINLIL_TIME_RESTART_SAFE,
                                    1) == NINLIL_ERR_STATE);
    CHECK(ninlil_coordinator_retire(&c, 1u, 4u, c.active.epoch, 130u,
                                    NINLIL_TIME_RESTART_SAFE,
                                    0) == NINLIL_ERR_BUSY);
    CHECK(ninlil_coordinator_retire(&c, 1u, 4u, c.active.epoch, 130u,
                                    NINLIL_TIME_RESTART_SAFE, 1) == NINLIL_OK);
    CHECK(ninlil_coordinator_open(&c, nodes, 4u, edges, 8u, 1u, policy, &f,
                                  plan_commit, &f) == NINLIL_OK);
    for (i = 0u; i < f.plan_count; i++)
        CHECK(ninlil_coordinator_restore(&c, &f.plans[i]) == NINLIL_OK);
    CHECK(ninlil_coordinator_route_check(&c, &direct, epoch, 130u) !=
          NINLIL_OK);

    memset(&f, 0, sizeof(f));
    CHECK(ninlil_coordinator_open(&c, nodes, 4u, edges, 8u, 1u, policy, &f,
                                  plan_commit, &f) == NINLIL_OK);
    CHECK(observe(&c, 1u, 4u, 10u, 1000u) == NINLIL_OK);
    CHECK(observe(&c, 1u, 2u, 10u, 10000u) == NINLIL_OK);
    CHECK(observe(&c, 2u, 4u, 10u, 10000u) == NINLIL_OK);
    CHECK(ninlil_coordinator_tick(&c, 1u, 4u, 0u, 100u,
                                  NINLIL_TIME_RESTART_SAFE) == NINLIL_OK);
    CHECK(c.pending.path.count == 2u && c.pending.phase == NINLIL_PLAN_STAGED &&
          c.active.epoch == 0u);
    CHECK(ninlil_coordinator_tick(&c, 1u, 4u, 0u, 101u,
                                  NINLIL_TIME_RESTART_SAFE) == NINLIL_ERR_BUSY);
    CHECK(ninlil_coordinator_abort(&c) == NINLIL_OK);
    CHECK(observe(&c, 1u, 4u, 0u, 1000u) == NINLIL_OK);
    CHECK(ninlil_coordinator_tick(&c, 1u, 4u, 0u, 102u,
                                  NINLIL_TIME_RESTART_SAFE) == NINLIL_OK);
    CHECK(c.pending.path.count == 3u && c.pending.path.nodes[1] == 2u &&
          c.active.epoch == 0u);

    puts("route optimization/partial ACK/reconcile/relay "
         "crash/repair/drain/bridge check PASS");
    return 0;
}
