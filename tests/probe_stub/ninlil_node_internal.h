#ifndef NINLIL_PROBE_FIXTURE_INTERNAL_H
#define NINLIL_PROBE_FIXTURE_INTERNAL_H
/* ONLY for a typed boundary fixture. Not the SDK's ABI or cryptographic stack.
 * Production C is copied byte-for-byte into the build dir to select these
 * dependency doubles without rewriting the implementation under test. */
#include "ninlil.h"
#include "ninlil_probe_monitor.h"
#define NINLIL_NODE_MEMBERS_MAX 16u
#define NINLIL_SECURE_FRAME_MAX 240u
#define NINLIL_SECURE_PLAINTEXT_MAX 200u
#define NINLIL_SECURE_OVERHEAD 40u
#define NINLIL_NETWORK_STALE_MS 30000u
#define NINLIL_LEASE_SYNC_ERROR_BOUND_MS 1000u
#define NINLIL_EDHOC_DEADLINE_MS 60000u
#define NODE_RETRY_MS 1000u
#define NODE_NO_PEER UINT16_MAX

typedef enum node_control_kind {
    NODE_PROBE = 8, NODE_PROBE_REPLY = 9, NODE_OBSERVATION = 10,
    NODE_OBSERVATION_ACK = 28, NODE_CORE_RECEIPT = 31
} node_control_kind;
typedef struct ninlil_secure_session {
    struct { uint8_t fingerprint[16]; } material;
    uint8_t ready;
} ninlil_secure_session;
typedef struct node_peer {
    ninlil_secure_session sessions[2];
    uint64_t probe_token, probe_sent_at, observed_at, probe_at, report_at, control_at;
    uint16_t attempts, delivered;
    uint32_t probe_airtime_us;
    uint8_t revoked, member_active, probe_sent, probe_window, probe_report;
} node_peer;
typedef struct ninlil_network_edge {
    uint16_t from, to, attempts, delivered;
    uint32_t airtime_us, queue_us;
    uint64_t observed_ms, membership_epoch;
    uint8_t used;
} ninlil_network_edge;
typedef struct ninlil_coordinator { int fixture; } ninlil_coordinator;
typedef struct ninlil_node {
    struct {
        uint16_t local, root, member_count;
        uint32_t permitted_profile;
        uint8_t dynamic_enrollment, offline;
        int (*emit)(void *, uint16_t, ninlil_traffic_class, const uint8_t *, size_t);
        void *emit_ctx;
        ninlil_random random;
        ninlil_probe_monitor *probe_monitor;
    } config;
    struct { struct { uint16_t node; uint64_t membership_epoch; ninlil_role role; } grant; } members[16];
    node_peer peers[16];
    uint16_t local_index, root_index, handshake_peer;
    uint64_t now_ms;
    uint8_t joined, sleeping;
    struct { int fault; } status;
    struct { uint64_t last_local_ms; } clock;
    int handshake;
    ninlil_runtime *core;
    ninlil_link data_link;
    ninlil_coordinator coordinator;
} ninlil_node;
int ninlil_node_index(const ninlil_node *, uint16_t);
uint64_t ninlil_node_get(const uint8_t *, size_t);
void ninlil_node_put(uint8_t *, uint64_t, size_t);
int ninlil_node_policy(void *, uint16_t, ninlil_peer_policy *);
int ninlil_node_control_send(ninlil_node *, uint16_t, node_control_kind, const uint8_t *, size_t);
int ninlil_node_recovery_receive(ninlil_node *, uint16_t, node_control_kind, const uint8_t *, size_t);
int ninlil_node_recovery_step(ninlil_node *);
int ninlil_node_lease(ninlil_node *, uint64_t *);
int ninlil_coordinator_observe(ninlil_coordinator *, uint16_t, const ninlil_network_edge *, uint64_t);
int ninlil_secure_seal_neighbor(ninlil_secure_session *, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
int ninlil_secure_inspect_tx(const ninlil_secure_session *, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
void ninlil_secret_clear(void *, size_t);
int ninlil_node_probe_current(ninlil_node *, const uint8_t *, size_t);
void ninlil_node_probe_transmitted(ninlil_node *, const uint8_t *, size_t, uint32_t);
int ninlil_node_link_receive(ninlil_node *, uint16_t, node_control_kind, const uint8_t *, size_t);
int ninlil_node_links_step(ninlil_node *);
void ninlil_node_delivery_link(ninlil_node *, ninlil_link *);
int ninlil_node_core_receipt(ninlil_node *, uint16_t, const uint8_t *, size_t);
int ninlil_node_frame_equal(ninlil_node *, const uint8_t *, const uint8_t *, size_t);
int ninlil_node_link_quality(ninlil_node *, uint16_t, uint64_t, uint64_t *, uint16_t *);
int ninlil_node_enable_probe_monitor(ninlil_node *, ninlil_probe_monitor *);
int ninlil_node_probe_measured(ninlil_node *, const uint8_t *, size_t, uint32_t, uint64_t, int8_t, uint64_t);
int ninlil_node_step(ninlil_node *, uint64_t);
int ninlil_node_receive(ninlil_node *, const uint8_t *, size_t, uint64_t);
int ninlil_node_frame_current(ninlil_node *, const uint8_t *, size_t, uint64_t);
void ninlil_node_transmitted(ninlil_node *, const uint8_t *, size_t, int, uint32_t, uint64_t);
void ninlil_edhoc_close(void *);
void ninlil_lease_invalidate(void *);
#endif
