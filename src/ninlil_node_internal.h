#ifndef NINLIL_NODE_INTERNAL_H
#define NINLIL_NODE_INTERNAL_H

#include "ninlil_control_log.h"
#include "ninlil_identity.h"
#include "ninlil_node.h"

#define NODE_RETRY_MS 1000u
#define NODE_CONTROL_MS 500u
#define NODE_FORWARD_MAX 8u
#define NODE_EDGES_MAX 64u
#define NODE_RELAY_MAX 16u
#define NODE_NO_PEER UINT16_MAX

typedef enum node_control_kind {
    NODE_JOIN_REQUEST = 1,
    NODE_JOIN_ACCEPT = 2,
    NODE_JOIN_ACK = 3,
    NODE_JOIN_ACTIVE = 4,
    NODE_MEMBER = 5,
    NODE_CLOCK_REQUEST = 6,
    NODE_CLOCK_REPLY = 7,
    NODE_PROBE = 8,
    NODE_PROBE_REPLY = 9,
    NODE_OBSERVATION = 10,
    NODE_FLOW_REQUEST = 11,
    NODE_PREPARE = 12,
    NODE_PREPARED = 13,
    NODE_APPLY = 14,
    NODE_APPLIED = 15,
    NODE_EFFECTIVE = 16,
    NODE_RECOVER_REQUEST = 17,
    NODE_RECOVER_CONFIRMED = 18,
    NODE_DRAIN = 19,
    NODE_MEMBER_ACK = 20,
    NODE_REMOVE_READY = 21,
    NODE_REVOKE_NOTICE = 22,
    NODE_REVOKE_ACK = 23,
    NODE_RELEASE = 24,
    NODE_RELEASED = 25,
    NODE_EFFECTIVE_ACK = 26,
    NODE_JOIN_READY = 27,
    NODE_OBSERVATION_ACK = 28,
    NODE_DRAIN_ACK = 29,
    NODE_REMOVE_READY_ACK = 30,
    NODE_CORE_RECEIPT = 31
} node_control_kind;

typedef struct node_peer {
    ninlil_secure_session sessions[2];
    ninlil_counter_store counters[2];
    uint64_t retry_at;
    uint64_t control_at;
    uint64_t control_emit_at;
    uint64_t probe_at;
    uint64_t probe_token;
    uint64_t probe_sent_at;
    uint64_t observed_at;
    uint64_t report_at;
    uint64_t drain_epoch;
    uint64_t remove_ack_epoch;
    uint32_t probe_airtime_us;
    uint16_t attempts;
    uint16_t delivered;
    uint16_t member_acks;
    uint8_t membership_cursor;
    uint8_t probe_window;
    uint8_t probe_report;
    uint8_t probe_sent;
    uint8_t member_active;
    uint8_t member_ready;
    uint8_t revoked;
    uint8_t revocation_applied;
    uint8_t draining;
    uint8_t drain_ready;
} node_peer;

typedef struct node_forward {
    uint8_t digest[16];
    uint64_t until_ms;
} node_forward;

struct ninlil_node {
    struct ninlil_discovery *discovery;
    ninlil_node_config config;
    ninlil_node_member members[NINLIL_NODE_MEMBERS_MAX];
    node_peer peers[NINLIL_NODE_MEMBERS_MAX];
    ninlil_join_authority authority;
    ninlil_join_peer join_peers[NINLIL_NODE_MEMBERS_MAX];
    ninlil_join_endpoint endpoint;
    ninlil_coordinator coordinator;
    ninlil_network_node graph_nodes[NINLIL_NODE_MEMBERS_MAX];
    ninlil_network_edge edges[NODE_EDGES_MAX];
    ninlil_network_plan local_plans[NINLIL_NETWORK_FLOWS_MAX];
    uint8_t local_ready[NINLIL_NETWORK_FLOWS_MAX];
    uint64_t retired[NINLIL_NETWORK_FLOWS_MAX];
    uint8_t plan_bindings[NINLIL_NETWORK_FLOWS_MAX][NINLIL_NETWORK_PATH_MAX]
                         [16];
    ninlil_network_plan prepared;
    ninlil_relay relay;
    ninlil_relay_slot custody[NODE_RELAY_MAX];
    ninlil_routed routed;
    ninlil_runtime *core;
    ninlil_control_log *log;
    ninlil_lease_clock clock;
    ninlil_edhoc handshake;
    ninlil_identity_peer credential;
    node_forward forwarded[NODE_FORWARD_MAX];
    uint8_t forward_pending[NINLIL_SECURE_FRAME_MAX];
    uint16_t forward_length;
    uint64_t forward_at, forward_until;
    uint16_t control_seen[32], control_ok[32];
    int control_last[32];
    ninlil_link data_link;
    ninlil_node_status status;
    uint64_t now_ms;
    uint64_t exchange_token;
    uint64_t handshake_retry_at;
    uint64_t control_at;
    uint64_t local_plan_epoch;
    uint64_t membership_generation;
    uint64_t core_at;
    uint64_t route_at;
    uint64_t recovery_at;
    uint64_t drain_ack_epoch;
    uint8_t drain_ack_state;
    uint64_t proof_epoch;
    uint8_t proofs[NINLIL_NETWORK_PATH_MAX][48];
    uint8_t proof_mask;
    uint8_t released_mask;
    uint64_t effective_epoch[NINLIL_NETWORK_FLOWS_MAX];
    uint8_t effective_bindings[NINLIL_NETWORK_FLOWS_MAX]
                              [NINLIL_NETWORK_PATH_MAX][16];
    uint8_t effective_notified[NINLIL_NETWORK_FLOWS_MAX];
    uint16_t request_peer[NINLIL_NETWORK_FLOWS_MAX][NINLIL_NETWORK_PATH_MAX];
    uint64_t request_seen[NINLIL_NETWORK_FLOWS_MAX][NINLIL_NETWORK_PATH_MAX];
    uint16_t local_index;
    uint16_t root_index;
    uint16_t dynamic_members;
    uint16_t handshake_peer;
    uint16_t next_peer;
    uint16_t broadcast_cursor;
    uint16_t wanted[NINLIL_NETWORK_FLOWS_MAX][2];
    uint64_t wanted_token[NINLIL_NETWORK_FLOWS_MAX];
    uint8_t wanted_force[NINLIL_NETWORK_FLOWS_MAX];
    uint64_t request_sequence;
    uint8_t wanted_cursor;
    uint8_t handshake_installed;
    uint8_t forward_cursor;
    uint8_t member_cursor;
    uint8_t plan_cursor;
    uint8_t notify_cursor, notify_turn;
    uint8_t joined;
    uint8_t removal_ready;
    uint8_t planning;
};

uint64_t ninlil_node_get(const uint8_t *data, size_t length);
void ninlil_node_put(uint8_t *data, uint64_t value, size_t length);
int ninlil_node_index(const ninlil_node *node, uint16_t address);
int ninlil_node_policy(void *ctx, uint16_t peer, ninlil_peer_policy *policy);
int ninlil_node_bootstrap_send(ninlil_node *node, uint16_t peer, uint8_t kind,
                               uint64_t token, const uint8_t *data,
                               size_t length);
int ninlil_node_forward(ninlil_node *node, const uint8_t *frame, size_t length);
int ninlil_node_forward_step(ninlil_node *node);
int ninlil_node_sync_step(ninlil_node *node);
int ninlil_node_forward_current(ninlil_node *node, const uint8_t *frame,
                                size_t length);
int ninlil_node_auth_step(ninlil_node *node);
int ninlil_node_bootstrap(ninlil_node *node, const uint8_t *frame,
                          size_t length);
int ninlil_node_auth_current(ninlil_node *node, const uint8_t *frame,
                             size_t length);
void ninlil_node_disconnect(ninlil_node *node, uint16_t index);
int ninlil_node_control_send(ninlil_node *node, uint16_t peer,
                             node_control_kind kind, const uint8_t *data,
                             size_t length);
int ninlil_node_control_receive(ninlil_node *node, uint16_t peer,
                                const uint8_t *data, size_t length,
                                int neighbor);
int ninlil_node_control_dispatch(ninlil_node *node, uint16_t peer,
                                 const uint8_t *data, size_t length,
                                 int neighbor);
void ninlil_node_delivery_link(ninlil_node *node, ninlil_link *link);
int ninlil_node_core_receipt(ninlil_node *node, uint16_t peer,
                             const uint8_t *data, size_t length);
int ninlil_node_control_step(ninlil_node *node);
int ninlil_node_membership_step(ninlil_node *node);
int ninlil_node_routes_step(ninlil_node *node);
int ninlil_node_plan_receive(ninlil_node *node, uint16_t peer,
                             node_control_kind kind, const uint8_t *data,
                             size_t length);
int ninlil_node_route(void *ctx, uint16_t source, uint16_t target,
                      uint64_t now_ms, ninlil_network_plan *plan);
int ninlil_node_plan_restore(void *ctx, const ninlil_network_plan *plan);
int ninlil_node_join_commit(void *ctx, const ninlil_join_record *record);
int ninlil_node_plan_commit(void *ctx, const ninlil_network_plan *plan);
int ninlil_node_lease(ninlil_node *node, uint64_t *lease_ms);
int ninlil_node_result(ninlil_node *node, int result);
void ninlil_node_membership_changed(ninlil_node *node);
ninlil_join_peer *ninlil_node_authority_peer(ninlil_node *node, uint16_t index);
int ninlil_node_record_matches(ninlil_node *node, uint16_t index,
                               const ninlil_join_record *record);
int ninlil_node_same_plan(const ninlil_network_plan *a,
                          const ninlil_network_plan *b);
int ninlil_node_plan_in_progress(ninlil_node *node, uint16_t source,
                                 uint16_t target, uint64_t now);
int ninlil_node_next_notification(ninlil_node *node, uint64_t now);
int ninlil_node_prepare_window(const ninlil_network_plan *plan, uint64_t now);
void ninlil_node_plan_rejected(ninlil_node *node,
                               const ninlil_network_plan *plan);
int ninlil_node_plan_frame_current(ninlil_node *node, uint16_t peer,
                                   const uint8_t *plain, size_t size);
int ninlil_node_plan_position(const ninlil_network_plan *plan,
                              uint16_t address);
int ninlil_node_local_plan(ninlil_node *node, uint16_t source, uint16_t target,
                           int create);
int ninlil_node_apply(ninlil_node *node, const ninlil_network_plan *plan,
                      uint8_t proof[48]);
int ninlil_node_prepare(ninlil_node *node, const ninlil_network_plan *plan);
int ninlil_node_effective(ninlil_node *node, const ninlil_network_plan *plan,
                          const uint8_t *bindings, size_t length);
int ninlil_node_release(ninlil_node *node, uint64_t epoch,
                        uint64_t replacement);
void ninlil_node_want(ninlil_node *node, uint16_t source, uint16_t target);
void ninlil_node_need_reconcile(ninlil_node *node, uint16_t source,
                                uint16_t target);
int ninlil_node_link_receive(ninlil_node *node, uint16_t peer,
                             node_control_kind kind, const uint8_t *data,
                             size_t length);
int ninlil_node_links_step(ninlil_node *node);
void ninlil_node_probe_transmitted(ninlil_node *node, const uint8_t *frame,
                                   size_t length, uint32_t airtime_us);
int ninlil_node_probe_current(ninlil_node *node, const uint8_t *frame,
                              size_t length);
int ninlil_node_recovery_step(ninlil_node *node);
int ninlil_node_recovery_receive(ninlil_node *node, uint16_t peer,
                                 node_control_kind kind, const uint8_t *data,
                                 size_t length);
int ninlil_node_lifecycle_step(ninlil_node *node);
int ninlil_node_lifecycle_receive(ninlil_node *node, uint16_t peer,
                                  node_control_kind kind, const uint8_t *data,
                                  size_t length);
int ninlil_node_expire_plans(ninlil_node *node, uint64_t lease);
int ninlil_node_epoch_restore(void *ctx, uint64_t epoch);
int ninlil_node_collection_step(ninlil_node *node);
int ninlil_node_member_check(ninlil_node *node,
                             const ninlil_node_member *member, uint16_t count);
int ninlil_node_member_restore(void *ctx, const uint8_t *data, uint16_t length);
int ninlil_node_member_snapshot(ninlil_node *node, ninlil_control_log *out);

int ninlil_node_discovery_open(ninlil_node *node);
void ninlil_node_discovery_close(ninlil_node *node);
int ninlil_node_discovery_step(ninlil_node *node);
int ninlil_node_discovery_receive(ninlil_node *node, const uint8_t *frame,
                                  size_t length);
int ninlil_node_discovery_current(ninlil_node *node, const uint8_t *frame,
                                  size_t length);
#endif
