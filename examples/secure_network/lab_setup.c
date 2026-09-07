#include "lab.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int approve(void *ctx, const uint8_t id[32], ninlil_join_grant *g)
{
    lab *l = ctx;
    unsigned int i;
    for (i = 0u; i < LAB_NODES; i++)
        if (memcmp(id, l->nodes[i].identity.identity, 32u) == 0)
            break;
    if (i == LAB_NODES)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(g, 0, sizeof(*g));
    memcpy(g->identity, id, 32u);
    memset(g->authority, 7, 16u);
    g->node = (uint16_t)(i + 1u);
    g->membership_epoch = 1u;
    g->binding_epoch = 1u;
    g->role = NINLIL_ROLE_POWERED_ENDPOINT;
    g->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    if (i == 1u || i == 2u)
        g->capabilities |= NINLIL_CAP_RELAY_CUSTODY;
    g->service_count = 1u;
    g->services[0] =
        (ninlil_service_grant){256u, 64u, 16u, NINLIL_SERVICE_BOTH, 15u};
    return NINLIL_OK;
}

static ninlil_secure_session *session(void *ctx, uint16_t peer, int hop)
{
    lab_node *n = ctx;
    if (peer == 0u || peer > LAB_NODES || peer == n->id)
        return NULL;
    return &n->sessions[peer - 1u][hop ? 1u : 0u].session;
}

static int emit(void *ctx, uint16_t next, ninlil_traffic_class traffic,
                const uint8_t *frame, size_t length)
{
    lab_node *n = ctx;
    if (n->token == UINT64_MAX)
        return NINLIL_ERR_STATE;
    return ninlil_airtime_enqueue(&n->scheduler, ++n->token, next, traffic,
                                  1000u, frame, length);
}

static int open_session(lab_session *s, const ninlil_session_material *m,
                        uint16_t local, uint16_t peer, uint8_t direction)
{
    ninlil_security_io io = {read_flash, write_flash, erase_flash, &s->storage,
                             sizeof(s->storage.bytes)};
    ninlil_counter_config c;
    memset(&c, 0, sizeof(c));
    memset(s->storage.bytes, 255, sizeof(s->storage.bytes));
    s->storage.fail = 0;
    memcpy(c.session_fingerprint, m->fingerprint, 16u);
    c.direction = direction;
    c.reservation_size = 128u;
    c.max_counter_exclusive = 1000000u;
    REQUIRE(
        ninlil_counter_open(&s->counter, &io, NINLIL_COUNTER_CREATE_NEW, &c));
    REQUIRE(ninlil_secure_open(&s->session, m, &s->counter, ninlil_psa_aead(),
                               local, peer, direction));
    return 0;
}

static int join_wire(lab *l, lab_node *n, ninlil_join_record *record,
                     int response)
{
    uint8_t plain[NINLIL_JOIN_RECORD_MAX], frame[NINLIL_SECURE_FRAME_MAX];
    uint8_t received[NINLIL_JOIN_RECORD_MAX];
    size_t length = ninlil_join_encode(record, plain, sizeof(plain)), size,
           decoded;
    ninlil_secure_session *a, *b;
    if (n->id == 1u)
        return 0; /* Explicit local root bootstrap, not a remote Join. */
    a = &l->nodes[0].sessions[n->id - 1u][1].session;
    b = &n->sessions[0][1].session;
    ASSERT(length != 0u);
    REQUIRE(ninlil_secure_seal_control(response ? b : a, plain, length, frame,
                                       sizeof(frame), &size));
    REQUIRE(ninlil_secure_unseal_control(response ? a : b, frame, size,
                                         received, sizeof(received), &decoded));
    REQUIRE(ninlil_join_decode(received, decoded, record));
    return 0;
}

static int join_node(lab *l, lab_node *n, const uint8_t fingerprint[16])
{
    ninlil_join_record accept, ack;
    const uint8_t *identity = n->identity.identity;
    REQUIRE(ninlil_join_begin(&l->authority, identity, 1000u));
    REQUIRE(
        ninlil_join_authenticated(&l->authority, identity, fingerprint, 1001u));
    REQUIRE(ninlil_join_prepare(&l->authority, identity, 1002u, &accept));
    REQUIRE(join_wire(l, n, &accept, 0));
    REQUIRE(
        ninlil_join_endpoint_accept(&n->member, &accept, fingerprint, &ack));
    REQUIRE(join_wire(l, n, &ack, 1));
    REQUIRE(ninlil_join_confirm(&l->authority, &ack, fingerprint, 1003u));
    return 0;
}

static int sessions(lab *l)
{
    unsigned int a, b, kind;
    for (a = 0u; a < LAB_NODES; a++)
        for (b = a + 1u; b < LAB_NODES; b++) {
            ninlil_session_material material[2];
            REQUIRE(lab_handshake(&l->nodes[a].identity, &l->nodes[b].identity,
                                  &material[0], &material[1]));
            for (kind = 0u; kind < 2u; kind++) {
                REQUIRE(open_session(&l->nodes[a].sessions[b][kind],
                                     &material[kind], (uint16_t)(a + 1u),
                                     (uint16_t)(b + 1u), 0u));
                REQUIRE(open_session(&l->nodes[b].sessions[a][kind],
                                     &material[kind], (uint16_t)(b + 1u),
                                     (uint16_t)(a + 1u), 1u));
            }
            if (a == 0u) {
                if (b == 1u)
                    REQUIRE(
                        join_node(l, &l->nodes[a], material[0].fingerprint));
                REQUIRE(join_node(l, &l->nodes[b], material[0].fingerprint));
            }
            ninlil_secret_clear(material, sizeof(material));
        }
    return 0;
}

int lab_plan(lab *l, uint16_t from, uint16_t to, uint16_t excluded)
{
    ninlil_network_path path;
    unsigned int i;
    uint64_t epoch;
    if (l->adaptive && excluded == 0u &&
        ((from == 1u && to == 4u) || (from == 4u && to == 1u)))
        excluded = 3u;
    if (l->adaptive) {
        int rc = ninlil_coordinator_tick(&l->coordinator, from, to, excluded,
                                         l->now_ms, NINLIL_TIME_RESTART_SAFE);
        if (rc == NINLIL_ERR_EMPTY)
            return 0;
        REQUIRE(rc);
        path = l->coordinator.pending.path;
    } else {
        memset(&path, 0, sizeof(path));
        path.nodes[0] = from;
        path.nodes[1] = to;
        path.count = 2u;
        path.cost_us = 10000u;
        path.membership_epochs[0] = path.membership_epochs[1] = 1u;
        REQUIRE(ninlil_coordinator_stage(&l->coordinator, &path, l->now_ms,
                                         l->now_ms + 50000u,
                                         NINLIL_TIME_RESTART_SAFE));
    }
    epoch = l->coordinator.pending.epoch;
    for (i = 0u; i < path.count; i++)
        REQUIRE(
            ninlil_coordinator_prepared(&l->coordinator, path.nodes[i], epoch));
    /* Lab owner synchronously fences every old participant before switch. */
    REQUIRE(ninlil_coordinator_activate(&l->coordinator, l->now_ms,
                                        NINLIL_TIME_RESTART_SAFE,
                                        l->offline[1] ? 0 : 1));
    for (i = 0u; i < path.count; i++)
        REQUIRE(
            ninlil_coordinator_applied(&l->coordinator, path.nodes[i], epoch));
    return 0;
}

static int network(lab *l)
{
    unsigned int a, b;
    REQUIRE(ninlil_coordinator_open(
        &l->coordinator, l->graph_nodes, LAB_NODES, l->edges, 32u, 1u,
        ninlil_join_policy, &l->authority, ninlil_control_log_plan, l->log));
    for (a = 1u; a <= LAB_NODES; a++)
        for (b = 1u; b <= LAB_NODES; b++) {
            ninlil_network_edge e;
            if (a == b)
                continue;
            memset(&e, 0, sizeof(e));
            e.from = (uint16_t)a;
            e.to = (uint16_t)b;
            e.attempts = 10u;
            e.delivered =
                (a == 1u && b == 4u) || (a == 4u && b == 1u) ? 1u : 10u;
            e.airtime_us = 1000u;
            e.membership_epoch = 1u;
            e.observed_ms = l->now_ms;
            REQUIRE(ninlil_coordinator_observe(&l->coordinator, (uint16_t)a, &e,
                                               l->now_ms));
        }
    for (a = 1u; a <= LAB_NODES; a++)
        for (b = 1u; b <= LAB_NODES; b++)
            if (a != b)
                REQUIRE(lab_plan(l, (uint16_t)a, (uint16_t)b, 0u));
    return 0;
}

static int node_runtime(lab *l, lab_node *n)
{
    ninlil_routed_config c;
    ninlil_config core;
    memset(&c, 0, sizeof(c));
    memset(&core, 0, sizeof(core));
    c.local = n->id;
    c.policy = ninlil_join_policy;
    c.policy_ctx = &l->authority;
    c.session = session;
    c.session_ctx = n;
    c.route = ninlil_coordinator_route;
    c.route_ctx = &l->coordinator;
    c.emit = emit;
    c.emit_ctx = n;
    c.digest = ninlil_psa_packet_digest;
    if (n->id == 2u || n->id == 3u) {
        REQUIRE(ninlil_relay_open(
            &n->relay, n->slots, 16u, n->id, NINLIL_ROLE_POWERED_ENDPOINT,
            NINLIL_CAP_RELAY_CUSTODY, 100u, ninlil_join_policy, &l->authority,
            ninlil_control_log_relay, ninlil_control_log_verify_relay, n->log,
            ninlil_coordinator_route_check, &l->coordinator));
        c.relay = &n->relay;
    }
    REQUIRE(ninlil_routed_open(&n->routed, &c, &core.link));
    REQUIRE(ninlil_airtime_open(&n->scheduler, 0u, 1000000u, 0u));
    core.node_id = n->id;
    core.journal_location = n->core_path;
    core.retry_interval_steps = 10u;
    core.max_work_per_step = 4u;
    core.random.fill = test_rng_fill;
    core.random.ctx = &l->rng;
    core.policy_lookup = ninlil_join_policy;
    core.policy_ctx = &l->authority;
    REQUIRE(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                         &core.profile));
    REQUIRE(ninlil_open(&n->core, &core));
    ninlil_routed_attach(&n->routed, n->core);
    REQUIRE(ninlil_routed_poll(&n->routed, l->now_ms));
    return 0;
}

int lab_open(lab *l, int adaptive)
{
    unsigned int i;
    uint8_t authority[16];
    ninlil_control_replay replay = {0};
    memset(l, 0, sizeof(*l));
    l->adaptive = adaptive;
    l->rng = 42u;
    l->now_ms = 1100u;
    memset(authority, 7, sizeof(authority));
    ASSERT(test_make_directory(l->directory, sizeof(l->directory)) == 0);
    ASSERT(test_make_path(l->log_path, sizeof(l->log_path), l->directory,
                          "authority") == 0);
    REQUIRE(ninlil_control_log_open(&l->log, l->log_path,
                                    NINLIL_CONTROL_LOG_MAX, replay));
    for (i = 0u; i < LAB_NODES; i++) {
        lab_node *n = &l->nodes[i];
        char name[32];
        n->id = (uint16_t)(i + 1u);
        n->lab = l;
        REQUIRE(lab_identity_create(&n->identity, n->id));
        (void)snprintf(name, sizeof(name), "node%u.core", i);
        ASSERT(test_make_path(n->core_path, sizeof(n->core_path), l->directory,
                              name) == 0);
        (void)snprintf(name, sizeof(name), "node%u.control", i);
        ASSERT(test_make_path(n->log_path, sizeof(n->log_path), l->directory,
                              name) == 0);
        REQUIRE(ninlil_control_log_open(&n->log, n->log_path,
                                        NINLIL_CONTROL_LOG_MAX, replay));
        REQUIRE(ninlil_join_endpoint_open(&n->member, n->identity.identity,
                                          authority, ninlil_control_log_join,
                                          n->log));
    }
    REQUIRE(ninlil_join_open(&l->authority, l->members, LAB_NODES, authority,
                             ninlil_control_log_join, l->log, approve, l));
    REQUIRE(sessions(l));
    for (unsigned int a = 0u; a < LAB_NODES; a++)
        for (unsigned int b = 0u; b < LAB_NODES; b++)
            if (a != b)
                for (unsigned int kind = 0u; kind < 2u; kind++)
                    REQUIRE(ninlil_secure_bind_membership(
                        &l->nodes[a].sessions[b][kind].session, 1u, 1u));
    REQUIRE(network(l));
    for (i = 0u; i < LAB_NODES; i++)
        REQUIRE(node_runtime(l, &l->nodes[i]));
    return 0;
}

void lab_close(lab *l)
{
    unsigned int i;
    for (i = 0u; i < LAB_NODES; i++) {
        ninlil_close(l->nodes[i].core);
        ninlil_control_log_close(l->nodes[i].log);
        (void)unlink(l->nodes[i].core_path);
        (void)unlink(l->nodes[i].log_path);
    }
    ninlil_control_log_close(l->log);
    (void)unlink(l->log_path);
    (void)rmdir(l->directory);
    ninlil_secret_clear(l, sizeof(*l));
}

int lab_rekey(lab *l, uint16_t peer)
{
    ninlil_session_material material[2];
    lab_node *a = &l->nodes[0], *b;
    unsigned int kind;
    if (peer < 2u || peer > LAB_NODES)
        return NINLIL_ERR_INVALID;
    b = &l->nodes[peer - 1u];
    REQUIRE(
        lab_handshake(&a->identity, &b->identity, &material[0], &material[1]));
    for (kind = 0u; kind < 2u; kind++) {
        ninlil_secure_close(&a->sessions[peer - 1u][kind].session);
        ninlil_secure_close(&b->sessions[0][kind].session);
        REQUIRE(open_session(&a->sessions[peer - 1u][kind], &material[kind], 1u,
                             peer, 0u));
        REQUIRE(
            open_session(&b->sessions[0][kind], &material[kind], peer, 1u, 1u));
    }
    ninlil_join_disconnect(&l->authority, b->identity.identity);
    REQUIRE(join_node(l, b, material[0].fingerprint));
    for (kind = 0u; kind < 2u; kind++) {
        REQUIRE(ninlil_secure_bind_membership(
            &a->sessions[peer - 1u][kind].session, 1u, 1u));
        REQUIRE(ninlil_secure_bind_membership(&b->sessions[0][kind].session, 1u,
                                              1u));
    }
    ninlil_secret_clear(material, sizeof(material));
    return 0;
}

int lab_refresh(lab *l)
{
    unsigned int i;
    for (i = 0u; i < 32u; i++) {
        ninlil_network_edge e = l->edges[i];
        if (!e.used || l->offline[e.from - 1u] || l->offline[e.to - 1u])
            continue;
        e.observed_ms = l->now_ms;
        REQUIRE(
            ninlil_coordinator_observe(&l->coordinator, e.from, &e, l->now_ms));
    }
    return 0;
}
