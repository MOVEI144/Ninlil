#ifndef NINLIL_NODE_H
#define NINLIL_NODE_H

#include "ninlil_join.h"
#include "ninlil_probe_monitor.h"
#include "ninlil_lease_clock.h"
#include "ninlil_routed.h"
typedef struct ninlil_identity ninlil_identity;

#define NINLIL_NODE_CONFIG_API_VERSION 2u
#define NINLIL_NODE_MEMBERS_MAX 16u
#define NINLIL_NODE_BOOTSTRAP_HEADER 16u
#define NINLIL_NODE_BOOTSTRAP_PAYLOAD 224u

/* Explicitly provisioned trust, including self. Bootstrap/control traffic uses
 * bounded four-hop flooding through configured powered Relay members, with
 * duplicate suppression. It remains available when a data route is missing.
 * Public keys are pinned to stable device identities, never learned from RF. */
typedef struct ninlil_node_member {
    ninlil_join_grant grant;
    uint8_t public_key[65];
} ninlil_node_member;

typedef struct ninlil_node_config {
    uint16_t local;
    uint16_t root;
    const ninlil_node_member *members;
    uint16_t member_count;
    ninlil_identity *identity;
    const char *journal_location;
    const char *control_location;
    uint32_t control_max_bytes;
    uint32_t permitted_profile;
    ninlil_role_profile resources;
    ninlil_clock utc;
    ninlil_random random;
    ninlil_counter_store *root_eras;
    /* Two dedicated 8 KiB slots per member index. Contexts outlive this owner.
     * Fresh authenticated material permits reformat of that ephemeral counter
     * slot only. Never map these slots onto identity, era or custody storage.
     */
    int (*counter_io)(void *ctx, uint16_t slot, ninlil_security_io *io);
    void *counter_ctx;
    ninlil_route_emit_fn emit;
    void *emit_ctx;
    uint8_t dynamic_enrollment; /* Opt-in to signed-credential discovery. */
    const uint8_t
        *authority_key; /* Optional 65-byte CA key, borrowed until close. */
    uint8_t offline;    /* Maintenance only: opens stores, never permits RF. */
    /* Optional borrowed telemetry owner. Prefer enable_probe_monitor before
     * the first step. Rebuild consumers after this config ABI extension. */
    ninlil_probe_monitor *probe_monitor;
} ninlil_node_config;

typedef struct ninlil_node ninlil_node;

typedef struct ninlil_node_status {
    uint16_t authenticated_peers;
    uint16_t active_members;
    uint16_t relay_owned;
    uint16_t handshake_peer;
    uint32_t handshakes;
    uint32_t rejected_frames;
    int fault;
    uint8_t joined;
    uint8_t lease_clock_ready;
    uint8_t handshake_stage;
    uint8_t handshake_initiator;
    uint64_t pending_route_epoch, prepared_route_epoch;
    uint8_t pending_phase, prepared_mask, proof_mask;
    uint8_t authority_routes, local_routes, effective_routes;
    uint16_t wanted_routes;
} ninlil_node_status;

typedef struct ninlil_node_peer_status {
    uint8_t active, revoked, ready, authority_state, authority_phase;
    uint16_t probe_attempts, probe_delivered;
    uint8_t e2e_fingerprint[16], hop_fingerprint[16];
} ninlil_node_peer_status;

typedef struct ninlil_node_flow_status {
    ninlil_network_plan local, authority;
    uint64_t lease_ms;
    uint8_t local_ready, reconciled, notified;
} ninlil_node_flow_status;

/* One explicit execution owner; owner tables allocate at open. PSA/EDHOC may
 * allocate during handshakes. Configuration, trust,
 * identity and IO contexts remain valid until close. Open replays Core/control
 * storage but never restores old session keys or invents participant evidence.
 * Drive step and receive even while application transmission is blocked. */
int ninlil_node_open(ninlil_node **node, const ninlil_node_config *config,
                     uint64_t monotonic_ms);
/* Explicit initial setup, before enabling RF. Creates matching identity
 * bindings only in empty stores; resumes matching interrupted setup. It never
 * erases an existing store or establishes a session. Normal open requires both
 * bindings, so a missing/cross-device journal cannot become a fresh history. */
int ninlil_node_provision_stores(const ninlil_node_config *config);
void ninlil_node_close(ninlil_node *node);
int ninlil_node_step(ninlil_node *node, uint64_t monotonic_ms);
int ninlil_node_receive(ninlil_node *node, const uint8_t *frame, size_t length,
                        uint64_t monotonic_ms);
int ninlil_node_frame_current(ninlil_node *node, const uint8_t *frame,
                              size_t length, uint64_t monotonic_ms);
/* Read-only equality of queued content; does not authorize TX or retire
 * custody. Own encrypted frames require successful inspection in the current
 * session. */
int ninlil_node_frame_equal(ninlil_node *node, const uint8_t *a,
                            const uint8_t *b, size_t length);
/* Report the actual driver result, not scheduler admission. Only TX_DONE
 * counts as a transmitted neighbor probe; a later authenticated reply is
 * separate link evidence. No application delivery outcome changes here. */
void ninlil_node_transmitted(ninlil_node *node, const uint8_t *frame,
                             size_t length, int driver_result,
                             uint32_t airtime_us, uint64_t monotonic_ms);
/* Initialize/borrow monitor before any probes; no RF, storage or authority
 * effects. The monitor must outlive the node and not be shared with another. */
int ninlil_node_enable_probe_monitor(ninlil_node *node,
                                      ninlil_probe_monitor *monitor);
/* Supplement the matching successful transmitted() call, same owner/time.
 * Reads actual applied TX power, not a staged radio_power() request.
 * Unknown queue residence is UINT64_MAX and cannot become a zero-cost sample.
 * Telemetry errors do not retract TX or change a delivery's evidence. */
int ninlil_node_probe_measured(ninlil_node *node, const uint8_t *frame,
                               size_t length, uint32_t airtime_us,
                               uint64_t queue_us, int8_t applied_power_dbm,
                               uint64_t monotonic_ms);
ninlil_runtime *ninlil_node_core(ninlil_node *node);
int ninlil_node_inspect(const ninlil_node *node, ninlil_node_status *status);
/* Public context fingerprints only; authority fields are zero on participants.
 */
int ninlil_node_peer_inspect(ninlil_node *node, uint16_t peer,
                             ninlil_node_peer_status *status);
/* Last-step snapshot; epoch zero means absent. Authority shows a matching
 * pending plan before an active plan. This neither requests a route nor grants
 * permission to transmit. lease_ms is zero while synchronization is
 * unavailable. */
int ninlil_node_flow_inspect(ninlil_node *node, uint16_t source,
                             uint16_t target, ninlil_node_flow_status *status);
/* Authority-only durable revocation. Remote application remains pending until
 * peers acknowledge it or their old leases expire. */
int ninlil_node_revoke(ninlil_node *node, uint16_t peer,
                       uint64_t expected_membership_epoch);
int ninlil_node_drain(ninlil_node *node);
/* Resume with draining=0. Ready means this Relay role has no dependent route
 * or opaque custody; it makes no claim about application/physical work. */
int ninlil_node_relay_drain(ninlil_node *node, int draining);
int ninlil_node_ready_remove(ninlil_node *node);
/* Collect Core/control state between owner calls. References and current
 * sessions remain valid; persistent plans still need fresh proofs after reboot.
 */
int ninlil_node_collect(ninlil_node *node);
/* Completed authenticated eight-probe window, boot-local monotonic time.
 * EMPTY during a current probe/rekey; observations never imply delivery. */
int ninlil_node_link_quality(ninlil_node *node, uint16_t peer, uint64_t now_ms,
                             uint64_t *observed_ms, uint16_t *delivered);

#endif
