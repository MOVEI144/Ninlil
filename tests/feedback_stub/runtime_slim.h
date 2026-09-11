/* Isolated declaration fixture only. Not repository headers, an SDK build,
 * crypto implementation or production ABI verification. */
#ifndef NINLIL_FEEDBACK_ISOLATED_TYPES
#define NINLIL_FEEDBACK_ISOLATED_TYPES
#include "ninlil.h"
#include "ninlil_network.h"
#include "ninlil_probe_monitor.h"
#include "ninlil_routed.h"
#include <stdbool.h>
#include <stdint.h>
#define NINLIL_NODE_H
#define NINLIL_NODE_INTERNAL_H
#define NINLIL_SLEEP_H
#define NINLIL_SECURE_OVERHEAD 40u
#define NINLIL_SECURE_PLAINTEXT_MAX 200u
#define NINLIL_SECURE_FRAME_MAX 240u
#define NINLIL_LEASE_SYNC_ERROR_BOUND_MS 10122u
#define NINLIL_EDHOC_DEADLINE_MS 30000u
#define NODE_RETRY_MS 1000u
#define NODE_NO_PEER UINT16_MAX
#define NINLIL_NODE_MEMBERS_MAX 16u

typedef struct ninlil_session_material {
    uint8_t fingerprint[16];
    uint8_t keys[2][16];
    uint8_t ivs[2][13];
} ninlil_session_material;
typedef struct ninlil_secure_session {
    ninlil_session_material material;
    uint8_t ready;
} ninlil_secure_session;
typedef struct ninlil_node_radio_observer {
    void (*reply)(void *, uint16_t, uint64_t, const uint8_t[16], uint64_t);
    int (*suspend)(void *, uint64_t);
    void *ctx;
} ninlil_node_radio_observer;
typedef struct ninlil_node_radio_tx {
    uint16_t peer;
    uint32_t profile;
    uint64_t probe_token;
    uint8_t session[16];
} ninlil_node_radio_tx;
typedef struct ninlil_join_grant {
    uint16_t node;
    uint64_t membership_epoch;
    ninlil_role role;
} ninlil_join_grant;
typedef struct ninlil_node_member {
    ninlil_join_grant grant;
} ninlil_node_member;
typedef struct ninlil_node_config {
    uint16_t local, root, member_count;
    uint32_t permitted_profile;
    uint8_t dynamic_enrollment, offline;
    int (*emit)(void *, uint16_t, ninlil_traffic_class, const uint8_t *,
                size_t);
    void *emit_ctx;
    ninlil_random random;
    ninlil_node_radio_observer radio_observer;
    ninlil_probe_monitor *probe_monitor;
} ninlil_node_config;
typedef struct node_peer {
    ninlil_secure_session sessions[2];
    uint64_t probe_token, probe_sent_at, observed_at, probe_at, report_at,
        control_at;
    uint32_t probe_airtime_us;
    uint16_t attempts, delivered;
    uint8_t member_active, revoked, probe_window, probe_report, probe_sent;
} node_peer;
typedef struct ninlil_edhoc {
    int unused;
} ninlil_edhoc;
typedef struct ninlil_lease_clock {
    uint64_t began_ms, last_local_ms;
} ninlil_lease_clock;
typedef enum node_control_kind {
    NODE_PROBE = 8,
    NODE_PROBE_REPLY = 9,
    NODE_OBSERVATION = 10,
    NODE_OBSERVATION_ACK = 28,
    NODE_CORE_RECEIPT = 31
} node_control_kind;
typedef struct ninlil_join_authority {
    int unused;
} ninlil_join_authority;
typedef struct ninlil_node {
    ninlil_node_config config;
    node_peer peers[16];
    ninlil_node_member members[16];
    uint16_t local_index, root_index, handshake_peer;
    uint8_t joined, sleeping, planning;
    uint64_t now_ms, core_at;
    struct {
        int fault;
        uint32_t rejected_frames;
    } status;
    ninlil_routed routed;
    ninlil_join_authority authority;
    uint8_t local_ready[32];
    ninlil_network_plan local_plans[32];
    ninlil_runtime *core;
    ninlil_link data_link;
    ninlil_coordinator coordinator;
    ninlil_edhoc handshake;
    ninlil_lease_clock clock;
} ninlil_node;
int ninlil_node_observe_radio(ninlil_node *,
                              const ninlil_node_radio_observer *);
int ninlil_node_radio_tx_context(ninlil_node *, const uint8_t *, size_t,
                                 ninlil_node_radio_tx *);
int ninlil_node_link_receive(ninlil_node *, uint16_t, node_control_kind,
                             const uint8_t *, size_t);
int ninlil_node_links_step(ninlil_node *);
int ninlil_node_probe_current(ninlil_node *, const uint8_t *, size_t);
void ninlil_node_probe_transmitted(ninlil_node *, const uint8_t *, size_t,
                                   uint32_t);
int ninlil_node_link_quality(ninlil_node *, uint16_t, uint64_t, uint64_t *,
                             uint16_t *);
int ninlil_node_frame_equal(ninlil_node *, const uint8_t *, const uint8_t *,
                            size_t);
void ninlil_node_delivery_link(ninlil_node *, ninlil_link *);
int ninlil_node_core_receipt(ninlil_node *, uint16_t, const uint8_t *, size_t);
int ninlil_node_suspend(ninlil_node *, uint64_t);
int ninlil_node_resume(ninlil_node *, uint64_t);
int ninlil_node_step(ninlil_node *, uint64_t);
int ninlil_node_receive(ninlil_node *, const uint8_t *, size_t, uint64_t);
int ninlil_node_frame_current(ninlil_node *, const uint8_t *, size_t, uint64_t);
void ninlil_node_transmitted(ninlil_node *, const uint8_t *, size_t, int,
                             uint32_t, uint64_t);
uint64_t ninlil_node_get(const uint8_t *, size_t);
void ninlil_node_put(uint8_t *, uint64_t, size_t);
int ninlil_node_index(const ninlil_node *, uint16_t);
int ninlil_node_policy(void *, uint16_t, ninlil_peer_policy *);
int ninlil_node_control_send(ninlil_node *, uint16_t, node_control_kind,
                             const uint8_t *, size_t);
int ninlil_node_lease(ninlil_node *, uint64_t *);
int ninlil_node_recovery_step(ninlil_node *);
int ninlil_node_recovery_receive(ninlil_node *, uint16_t, node_control_kind,
                                 const uint8_t *, size_t);
int ninlil_coordinator_observe(ninlil_coordinator *, uint16_t,
                               const ninlil_network_edge *, uint64_t);
int ninlil_secure_inspect_tx(const ninlil_secure_session *, const uint8_t *,
                             size_t, uint8_t *, size_t, size_t *);
int ninlil_secure_seal_neighbor(ninlil_secure_session *, const uint8_t *,
                                size_t, uint8_t *, size_t, size_t *);
void ninlil_secret_clear(void *, size_t);
void ninlil_edhoc_close(ninlil_edhoc *);
void ninlil_lease_invalidate(ninlil_lease_clock *);

int ninlil_node_collection_step(ninlil_node *);
int ninlil_node_discovery_step(ninlil_node *);
int ninlil_node_auth_step(ninlil_node *);
int ninlil_node_control_step(ninlil_node *);
int ninlil_node_routes_step(ninlil_node *);
int ninlil_node_root_clock(ninlil_node *, uint64_t);
int ninlil_node_result(ninlil_node *, int);
int ninlil_node_bootstrap(ninlil_node *, const uint8_t *, size_t);
int ninlil_node_auth_current(ninlil_node *, const uint8_t *, size_t);
int ninlil_node_control_receive(ninlil_node *, uint16_t, const uint8_t *,
                                size_t, int);
void ninlil_join_expire(ninlil_join_authority *, uint64_t);
int ninlil_set_retry_interval(ninlil_runtime *, uint32_t);
int ninlil_secure_unseal_neighbor(ninlil_secure_session *, const uint8_t *,
                                  size_t, uint8_t *, size_t, size_t *);
int ninlil_node_enable_probe_monitor(ninlil_node *, ninlil_probe_monitor *);
int ninlil_node_probe_measured(ninlil_node *, const uint8_t *, size_t, uint32_t,
                               uint64_t, int8_t, uint64_t);
int ninlil_node_enable_route_optimizer(ninlil_node *, ninlil_route_optimizer *);
#endif
