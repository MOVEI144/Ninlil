#include "ninlil_node_internal.h"
#include <stdlib.h>
#include <string.h>

int ninlil_node_index(const ninlil_node *n, uint16_t address)
{
    uint16_t i;
    for (i = 0u; i < n->config.member_count; i++)
        if (n->members[i].grant.node == address)
            return (int)i;
    return -1;
}

int ninlil_node_policy(void *ctx, uint16_t address, ninlil_peer_policy *out)
{
    ninlil_node *n = ctx;
    int index = ninlil_node_index(n, address);
    const ninlil_join_grant *g;
    node_peer *peer;
    if (index < 0 || n->peers[index].revoked)
        return NINLIL_ERR_UNAUTHORIZED;
    if (n->status.fault)
        return n->status.fault;
    peer = &n->peers[index];
    g = &n->members[index].grant;
    memset(out, 0, sizeof(*out));
    out->role = g->role;
    out->capabilities = g->capabilities;
    if (n->planning && peer->draining)
        out->capabilities &= ~((uint32_t)NINLIL_CAP_RELAY_CUSTODY);
    out->grants = g->services;
    out->grant_count = g->service_count;
    out->membership_epoch = g->membership_epoch;
    if (n->joined && peer->member_active &&
        (address == n->config.local || peer->sessions[0].ready))
        out->session_membership_epoch = g->membership_epoch;
    return NINLIL_OK;
}

int ninlil_node_join_commit(void *ctx, const ninlil_join_record *record)
{
    ninlil_node *n = ctx;
    int rc = ninlil_control_log_join(n->log, record);
    if (rc != NINLIL_OK)
        n->status.fault = rc;
    return rc;
}
int ninlil_node_plan_commit(void *ctx, const ninlil_network_plan *plan)
{
    ninlil_node *n = ctx;
    if (n->config.authority_key && n->config.local == n->config.root &&
        (plan->epoch >> 32) != n->members[n->root_index].grant.membership_epoch)
        return n->status.fault = NINLIL_ERR_CAPACITY;
    int rc = ninlil_control_log_plan(n->log, plan);
    if (rc != NINLIL_OK)
        n->status.fault = rc;
    return rc;
}
static int relay_commit(void *ctx, const ninlil_relay_record *record)
{
    ninlil_node *n = ctx;
    int rc = ninlil_control_log_relay(n->log, record);
    if (rc != NINLIL_OK)
        n->status.fault = rc;
    return rc;
}
static int relay_verify(void *ctx, const ninlil_relay_record *record)
{
    return ninlil_control_log_verify_relay(((ninlil_node *)ctx)->log, record);
}
static int relay_check(void *ctx, const ninlil_network_path *path,
                       uint64_t epoch, uint64_t now)
{
    ninlil_network_plan plan;
    int rc = ninlil_node_route(ctx, path->nodes[0],
                               path->nodes[path->count - 1u], now, &plan);
    return rc != NINLIL_OK ? rc
           : plan.epoch == epoch && plan.path.count == path->count &&
                   memcmp(plan.path.nodes, path->nodes,
                          (size_t)path->count * sizeof(uint16_t)) == 0
               ? NINLIL_OK
               : NINLIL_ERR_STATE;
}
static int replay_relay(void *ctx, const ninlil_relay_record *record)
{
    ninlil_node *n = ctx;
    return n->routed.config.relay ? ninlil_relay_restore(&n->relay, record)
                                  : NINLIL_ERR_CORRUPT;
}
static int replay_join(void *ctx, const ninlil_join_record *record)
{
    ninlil_node *n = ctx;
    int index = ninlil_node_index(n, record->grant.node), rc;
    if (index < 0 || memcmp(n->members[index].grant.identity,
                            record->grant.identity, 32u) != 0)
        return NINLIL_ERR_CORRUPT;
    if (n->config.local == n->config.root)
        rc = ninlil_join_restore(&n->authority, record);
    else
        rc = ninlil_join_endpoint_restore(&n->endpoint, record);
    if (rc == NINLIL_OK)
        n->peers[index].revoked =
            record->state == NINLIL_JOIN_REVOKED &&
                    record->grant.membership_epoch >=
                        n->members[index].grant.membership_epoch
                ? 1u
                : 0u;
    return rc;
}
static int approve(void *ctx, const uint8_t identity[32],
                   ninlil_join_grant *grant)
{
    ninlil_node *n = ctx;
    unsigned int i;
    for (i = 0u; i < n->config.member_count; i++)
        if (memcmp(identity, n->members[i].grant.identity, 32u) == 0) {
            *grant = n->members[i].grant;
            return NINLIL_OK;
        }
    return NINLIL_ERR_UNAUTHORIZED;
}
static ninlil_secure_session *session(void *ctx, uint16_t address, int hop)
{
    ninlil_node *n = ctx;
    int index = ninlil_node_index(n, address);
    return index >= 0 && (hop == 0 || hop == 1) ? &n->peers[index].sessions[hop]
                                                : NULL;
}

static int configuration(ninlil_node *n, const ninlil_node_config *c)
{
    unsigned int i;
    int local, root;
    if (!c || c->offline > 1u || !c->members || c->member_count < 1u ||
        c->member_count > NINLIL_NODE_MEMBERS_MAX || !c->identity ||
        !c->identity->signing_key || !c->counter_io || !c->emit ||
        !c->random.fill || !c->permitted_profile || !c->journal_location ||
        !c->control_location || c->control_max_bytes < 4096u ||
        c->control_max_bytes > NINLIL_CONTROL_LOG_MAX ||
        ninlil_role_profile_validate(&c->resources) != NINLIL_OK ||
        (!c->dynamic_enrollment &&
         c->member_count - 1u > c->resources.active_peers) ||
        (c->local == c->root && !c->root_eras))
        return NINLIL_ERR_INVALID;
    n->config = *c;
    memcpy(n->members, c->members,
           (size_t)c->member_count * sizeof(*c->members));
    n->config.members = n->members;
    local = ninlil_node_index(n, c->local);
    root = ninlil_node_index(n, c->root);
    if (local < 0 || root < 0 ||
        n->members[root].grant.role != NINLIL_ROLE_SITE_GATEWAY ||
        n->members[local].grant.role != c->resources.role ||
        memcmp(n->members[local].grant.identity, c->identity->identity, 32u) !=
            0 ||
        memcmp(n->members[local].public_key, c->identity->public_key, 65u) != 0)
        return NINLIL_ERR_INVALID;
    n->local_index = (uint16_t)local;
    n->root_index = (uint16_t)root;
    for (i = 0u; i < c->member_count; i++) {
        int rc = ninlil_node_member_check(n, &n->members[i], (uint16_t)i);
        if (rc != NINLIL_OK)
            return rc;
    }
    return NINLIL_OK;
}

static int root_identity(ninlil_node *n)
{
    ninlil_join_record record = {0};
    ninlil_join_peer *old = ninlil_node_authority_peer(n, n->local_index);
    int rc;
    record.grant = n->members[n->local_index].grant;
    memcpy(record.transaction, n->config.identity->fingerprint, 16u);
    record.state = NINLIL_JOIN_PENDING;
    if (old && old->persisted) {
        if (ninlil_node_record_matches(n, n->local_index, &old->record) &&
            memcmp(old->record.transaction, record.transaction, 16u) == 0) {
            if (old->record.state == NINLIL_JOIN_ACTIVE) {
                n->joined = n->peers[n->local_index].member_active = 1u;
                return NINLIL_OK;
            }
            if (old->record.state != NINLIL_JOIN_PENDING)
                return NINLIL_ERR_UNAUTHORIZED;
        } else if (record.grant.membership_epoch <=
                       old->record.grant.membership_epoch ||
                   record.grant.binding_epoch < old->record.grant.binding_epoch)
            return NINLIL_ERR_UNAUTHORIZED;
        else {
            rc = ninlil_node_join_commit(n, &record);
            if (rc == NINLIL_OK)
                rc = ninlil_join_restore(&n->authority, &record);
            if (rc != NINLIL_OK)
                return rc;
        }
    }
    record.state = NINLIL_JOIN_ACTIVE;
    rc = ninlil_node_join_commit(n, &record);
    if (rc == NINLIL_OK)
        rc = ninlil_join_restore(&n->authority, &record);
    if (rc == NINLIL_OK)
        n->joined = n->peers[n->local_index].member_active = 1u;
    return rc;
}

static int open_layers(ninlil_node *n, int initialize)
{
    const ninlil_join_grant *local = &n->members[n->local_index].grant;
    ninlil_routed_config routed = {0};
    ninlil_control_replay replay = {
        replay_join, ninlil_node_plan_restore,  replay_relay,
        n,           ninlil_node_epoch_restore, ninlil_node_member_restore};
    ninlil_config core = {0};
    int rc = ninlil_join_open(&n->authority, n->join_peers,
                              NINLIL_NODE_MEMBERS_MAX, local->authority,
                              ninlil_node_join_commit, n, approve, n);
    if (rc == NINLIL_OK)
        rc = ninlil_join_endpoint_open(&n->endpoint, local->identity,
                                       local->authority,
                                       ninlil_node_join_commit, n);
    if (rc == NINLIL_OK)
        rc = ninlil_coordinator_open(
            &n->coordinator, n->graph_nodes, NINLIL_NODE_MEMBERS_MAX, n->edges,
            NODE_EDGES_MAX, n->config.permitted_profile, ninlil_node_policy, n,
            ninlil_node_plan_commit, n);
    n->coordinator.separate_prepare_lease = n->config.dynamic_enrollment;
    if (rc == NINLIL_OK && (local->capabilities & NINLIL_CAP_RELAY_CUSTODY)) {
        rc = ninlil_relay_open(
            &n->relay, n->custody, NODE_RELAY_MAX, n->config.local, local->role,
            local->capabilities, 1000u, ninlil_node_policy, n, relay_commit,
            relay_verify, n, relay_check, n);
        routed.relay = &n->relay;
    }
    if (rc != NINLIL_OK)
        return rc;
    routed.local = n->config.local;
    routed.policy = ninlil_node_policy;
    routed.policy_ctx = n;
    routed.session = session;
    routed.session_ctx = n;
    routed.route = ninlil_node_route;
    routed.route_ctx = n;
    routed.emit = n->config.emit;
    routed.emit_ctx = n->config.emit_ctx;
    routed.digest = ninlil_psa_packet_digest;
    rc = ninlil_routed_open(&n->routed, &routed, &core.link);
    if (rc == NINLIL_OK)
        rc = ninlil_control_log_open(&n->log, n->config.control_location,
                                     n->config.control_max_bytes, replay);
    if (rc != NINLIL_OK)
        return rc;
    rc = ninlil_control_log_bind(n->log, n->config.identity->identity,
                                 initialize);
    if (rc != NINLIL_OK)
        return rc;
    core.node_id = n->config.local;
    core.journal_location = n->config.journal_location;
    core.max_work_per_step = 4u;
    core.retry_interval_steps = 100u;
    core.profile = n->config.resources;
    core.random = n->config.random;
    core.clock = n->config.utc;
    core.policy_lookup = ninlil_node_policy;
    core.policy_ctx = n;
    ninlil_node_delivery_link(n, &core.link);
    rc = ninlil_open(&n->core, &core);
    if (rc == NINLIL_OK)
        rc = ninlil_bind_storage(n->core, n->config.identity->identity,
                                 initialize);
    if (rc == NINLIL_OK)
        ninlil_routed_attach(&n->routed, n->core);
    return rc;
}

int ninlil_node_open(ninlil_node **out, const ninlil_node_config *c,
                     uint64_t now)
{
    ninlil_node *n;
    int rc;
    if (!out)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    if (!c || now > UINT64_MAX - NINLIL_EDHOC_DEADLINE_MS)
        return NINLIL_ERR_INVALID;
    n = calloc(1u, sizeof(*n));
    if (!n)
        return NINLIL_ERR_CAPACITY;
    n->now_ms = now;
    n->handshake_peer = NODE_NO_PEER;
    n->membership_generation = c->local == c->root ? 1u : 0u;
    rc = configuration(n, c);
    if (rc == NINLIL_OK)
        rc = ninlil_node_discovery_open(n);
    if (rc == NINLIL_OK && !c->identity->initialized)
        rc = NINLIL_ERR_CORRUPT;
    if (rc == NINLIL_OK)
        rc = open_layers(n, 0);
    if (rc == NINLIL_OK && c->local == c->root && !c->offline) {
        rc = n->status.fault ? n->status.fault : root_identity(n);
        if (rc == NINLIL_OK)
            rc = ninlil_node_root_clock(n, now);
    } else if (rc == NINLIL_OK)
        ninlil_lease_peer_open(&n->clock, now);
    if (rc != NINLIL_OK) {
        ninlil_node_close(n);
        return rc;
    }
    *out = n;
    return NINLIL_OK;
}

int ninlil_node_provision_stores(const ninlil_node_config *config)
{
    ninlil_node *n = calloc(1u, sizeof(*n));
    int rc;
    if (!n)
        return NINLIL_ERR_CAPACITY;
    rc = configuration(n, config);
    if (rc == NINLIL_OK)
        rc = open_layers(n, config->identity->initialized ? 0 : 1);
    if (rc == NINLIL_OK)
        rc = ninlil_identity_mark_initialized(config->identity);
    ninlil_node_close(n);
    return rc;
}

void ninlil_node_close(ninlil_node *n)
{
    unsigned int i, hop;
    if (!n)
        return;
    ninlil_node_discovery_close(n);
    ninlil_edhoc_close(&n->handshake);
    for (i = 0u; i < NINLIL_NODE_MEMBERS_MAX; i++)
        for (hop = 0u; hop < 2u; hop++) {
            ninlil_secure_close(&n->peers[i].sessions[hop]);
            ninlil_counter_close(&n->peers[i].counters[hop]);
        }
    ninlil_close(n->core);
    ninlil_control_log_close(n->log);
    ninlil_secret_clear(n, sizeof(*n));
    free(n);
}

int ninlil_node_result(ninlil_node *n, int rc)
{
    if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_CORRUPT ||
        rc == NINLIL_ERR_FAULT)
        n->status.fault = rc;
    return n->status.fault ? n->status.fault : NINLIL_OK;
}
int ninlil_node_lease(ninlil_node *n, uint64_t *lease)
{
    int rc = ninlil_lease_now(&n->clock, n->now_ms, lease);
    n->status.lease_clock_ready = rc == NINLIL_OK ? 1u : 0u;
    return rc;
}
ninlil_runtime *ninlil_node_core(ninlil_node *n)
{
    return n ? n->core : NULL;
}
int ninlil_node_inspect(const ninlil_node *n, ninlil_node_status *status)
{
    unsigned int i;
    if (!n || !status)
        return NINLIL_ERR_INVALID;
    *status = n->status;
    status->pending_route_epoch = n->coordinator.pending.epoch;
    status->prepared_route_epoch = n->prepared.epoch;
    status->pending_phase = (uint8_t)n->coordinator.pending.phase;
    status->prepared_mask = n->coordinator.prepared_live;
    status->proof_mask = n->proof_mask;
    status->authority_routes = status->local_routes = status->effective_routes =
        0u;
    status->wanted_routes = 0u;
    for (i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        if (n->coordinator.flows[i].active.epoch)
            status->authority_routes++;
        if (n->local_plans[i].epoch)
            status->local_routes++;
        if (n->local_ready[i] == 3u)
            status->effective_routes++;
        if (n->wanted[i][0])
            status->wanted_routes++;
    }
    status->joined = n->joined;
    status->handshake_peer =
        n->handshake.opened && n->handshake_peer < n->config.member_count
            ? n->members[n->handshake_peer].grant.node
            : 0u;
    status->handshake_stage = n->handshake.step;
    status->handshake_initiator = n->handshake.config.initiator;
    status->active_members = status->authenticated_peers = status->relay_owned =
        0u;
    for (i = 0u; i < n->config.member_count; i++) {
        if (n->peers[i].sessions[0].ready)
            status->authenticated_peers++;
        if (n->peers[i].member_active)
            status->active_members++;
    }
    for (i = 0u; i < NODE_RELAY_MAX; i++)
        if (n->custody[i].used)
            status->relay_owned++;
    return NINLIL_OK;
}

int ninlil_node_peer_inspect(ninlil_node *n, uint16_t peer,
                             ninlil_node_peer_status *status)
{
    int index;
    node_peer *p;
    ninlil_join_peer *j;
    if (!n || !status || (index = ninlil_node_index(n, peer)) < 0)
        return NINLIL_ERR_INVALID;
    p = &n->peers[index];
    memset(status, 0, sizeof(*status));
    status->active = p->member_active;
    status->revoked = p->revoked;
    status->ready = p->sessions[0].ready;
    status->probe_attempts = p->attempts;
    status->probe_delivered = p->delivered;
    memcpy(status->e2e_fingerprint, p->sessions[0].material.fingerprint, 16u);
    memcpy(status->hop_fingerprint, p->sessions[1].material.fingerprint, 16u);
    j = ninlil_node_authority_peer(n, (uint16_t)index);
    if (j) {
        status->authority_state = (uint8_t)j->record.state;
        status->authority_phase = j->phase;
    }
    return NINLIL_OK;
}

int ninlil_node_flow_inspect(ninlil_node *n, uint16_t source, uint16_t target,
                             ninlil_node_flow_status *status)
{
    ninlil_lease_clock clock;
    int slot;
    if (!n || !status || source == target || ninlil_node_index(n, source) < 0 ||
        ninlil_node_index(n, target) < 0)
        return NINLIL_ERR_INVALID;
    memset(status, 0, sizeof(*status));
    clock = n->clock;
    (void)ninlil_lease_now(&clock, n->now_ms, &status->lease_ms);
    slot = ninlil_node_local_plan(n, source, target, 0);
    if (slot >= 0) {
        status->local = n->local_plans[slot];
        status->local_ready = n->local_ready[slot];
    }
    if (n->config.local == n->config.root) {
        for (unsigned int i = 0u; i <= NINLIL_NETWORK_FLOWS_MAX; i++) {
            const ninlil_network_plan *p =
                i == NINLIL_NETWORK_FLOWS_MAX ? &n->coordinator.pending
                                              : &n->coordinator.flows[i].active;
            if (!p->epoch || p->path.nodes[0] != source ||
                p->path.nodes[p->path.count - 1u] != target)
                continue;
            status->authority = *p;
            status->reconciled = i == NINLIL_NETWORK_FLOWS_MAX
                                     ? 0u
                                     : n->coordinator.flows[i].reconciled;
            status->notified =
                i == NINLIL_NETWORK_FLOWS_MAX ? 0u : n->effective_notified[i];
        }
    }
    return NINLIL_OK;
}
