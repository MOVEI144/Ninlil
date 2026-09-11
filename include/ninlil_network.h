#ifndef NINLIL_NETWORK_H
#define NINLIL_NETWORK_H

#include "ninlil.h"

#define NINLIL_NETWORK_NODES_MAX 512u
#define NINLIL_NETWORK_EDGES_MAX 2048u
#define NINLIL_NETWORK_HOPS_MAX 4u
#define NINLIL_NETWORK_PATH_MAX 5u
#define NINLIL_NETWORK_STALE_MS 30000u
#define NINLIL_NETWORK_HOLD_MS 2000u
#define NINLIL_NETWORK_FLOWS_MAX 32u
#define NINLIL_NETWORK_PLAN_MAX 98u
#define NINLIL_NETWORK_LEASE_MAX_MS 60000u

typedef struct ninlil_network_path {
    uint16_t nodes[NINLIL_NETWORK_PATH_MAX];
    uint8_t count;
    uint64_t cost_us;
    uint64_t membership_epochs[NINLIL_NETWORK_PATH_MAX];
} ninlil_network_path;

typedef struct ninlil_network_edge {
    uint16_t from;
    uint16_t to;
    uint16_t attempts;
    uint16_t delivered;
    uint32_t airtime_us;
    uint32_t queue_us;
    uint64_t observed_ms;
    uint64_t membership_epoch;
    uint8_t used;
} ninlil_network_edge;

typedef struct ninlil_network_node {
    uint16_t id;
    ninlil_network_path path;
    ninlil_network_path previous;
} ninlil_network_node;

typedef enum ninlil_plan_phase {
    NINLIL_PLAN_STAGED = 1,
    NINLIL_PLAN_COMMITTED = 2,
    NINLIL_PLAN_EFFECTIVE = 3,
    NINLIL_PLAN_ABORTED = 4,
    NINLIL_PLAN_RETIRED = 5
} ninlil_plan_phase;

typedef struct ninlil_network_plan {
    ninlil_network_path path;
    uint64_t epoch;
    uint64_t valid_until_ms;
    uint32_t profile;
    uint32_t rto_ms;
    uint8_t prepared;
    uint8_t applied;
    ninlil_plan_phase phase;
    /* Zero: NP1. Nonzero: NP2 preparation deadline, distinct from the
     * committed lease. Both preparation and active use remain <= 60 seconds. */
    uint64_t prepare_until_ms;
} ninlil_network_plan;

typedef int (*ninlil_plan_commit_fn)(void *ctx,
                                     const ninlil_network_plan *plan);

typedef struct ninlil_route_optimizer ninlil_route_optimizer;

typedef struct ninlil_network_flow {
    ninlil_network_plan active;
    uint8_t reconciled;
    uint64_t
        changed_ms; /* Boot-local per-flow hold; not persisted or authority. */
} ninlil_network_flow;

typedef struct ninlil_coordinator {
    ninlil_network_node *nodes;
    ninlil_network_edge *edges;
    uint16_t node_capacity;
    uint16_t edge_capacity;
    uint16_t node_count;
    ninlil_policy_lookup policy;
    void *policy_ctx;
    ninlil_plan_commit_fn commit;
    void *commit_ctx;
    ninlil_network_plan active;
    ninlil_network_plan pending;
    uint8_t prepared_live;
    uint8_t applied_live;
    uint64_t last_epoch;
    uint64_t last_change_ms;
    uint32_t permitted_profile;
    uint8_t enabled;
    uint8_t separate_prepare_lease; /* Opt in to NP2 before staging new work. */
    uint8_t poisoned;
    ninlil_network_flow flows[NINLIL_NETWORK_FLOWS_MAX];
    ninlil_network_plan last_record;
    ninlil_route_optimizer
        *optimizer; /* Optional borrowed, fixed-PHY planner. */
} ninlil_coordinator;

int ninlil_coordinator_open(ninlil_coordinator *c, ninlil_network_node *nodes,
                            uint16_t node_capacity, ninlil_network_edge *edges,
                            uint16_t edge_capacity, uint32_t permitted_profile,
                            ninlil_policy_lookup policy, void *policy_ctx,
                            ninlil_plan_commit_fn commit, void *commit_ctx);
/* Reports arrive only after authentication; reporter must own the from field.
 * Samples never grant membership, Relay capability or a new legal PHY profile.
 */
int ninlil_coordinator_observe(ninlil_coordinator *c, uint16_t reporter,
                               const ninlil_network_edge *edge,
                               uint64_t now_ms);
int ninlil_coordinator_select(ninlil_coordinator *c, uint16_t source,
                              uint16_t target, uint16_t excluded,
                              uint64_t now_ms, ninlil_network_path *path);
/* Periodic optimizer entry. OK means a durable proposal is staged; EMPTY
 * means no change. It never substitutes a proposal for participant application.
 */
int ninlil_coordinator_tick(ninlil_coordinator *c, uint16_t source,
                            uint16_t target, uint16_t excluded, uint64_t now_ms,
                            ninlil_time_quality quality);
int ninlil_coordinator_stage(ninlil_coordinator *c,
                             const ninlil_network_path *path, uint64_t now_ms,
                             uint64_t valid_until_ms,
                             ninlil_time_quality time_quality);
int ninlil_coordinator_prepared(ninlil_coordinator *c, uint16_t peer,
                                uint64_t epoch);
int ninlil_coordinator_activate(ninlil_coordinator *c, uint64_t now_ms,
                                ninlil_time_quality time_quality,
                                int old_released);
int ninlil_coordinator_applied(ninlil_coordinator *c, uint16_t peer,
                               uint64_t epoch);
int ninlil_coordinator_abort(ninlil_coordinator *c);
/* Abort is only for STAGED work. Once committed, withdrawal requires the
 *
 * exact epoch and either authenticated release of every participant or
 *
 * proven lease expiry. It never retires Core or Relay custody. */
int ninlil_coordinator_withdraw(ninlil_coordinator *c, uint64_t epoch,
                                uint64_t now_ms, ninlil_time_quality quality,
                                int all_released);
/* Explicit retirement after every participant released the old epoch, or its
 * restart-safe lease expired. Does not retire any Core or Relay ownership. */
int ninlil_coordinator_retire(ninlil_coordinator *c, uint16_t source,
                              uint16_t target, uint64_t expected_epoch,
                              uint64_t now_ms, ninlil_time_quality quality,
                              int all_released);
int ninlil_coordinator_restore(ninlil_coordinator *c,
                               const ninlil_network_plan *plan);
/* Disable optimizer without deleting the active plan or owned messages. */
void ninlil_coordinator_enable(ninlil_coordinator *c, int enabled);
int ninlil_coordinator_reconcile(ninlil_coordinator *c, uint16_t peer,
                                 uint64_t epoch);
/* Authentication owner calls before removing/replacing a live peer session.
 *
 * Invalidates volatile application evidence and observations, preserving
 *
 * every committed plan/lease fence. A fresh session alone cannot reapply it. */
void ninlil_coordinator_disconnect(ninlil_coordinator *c, uint16_t peer);
int ninlil_coordinator_route_check(void *ctx, const ninlil_network_path *path,
                                   uint64_t epoch, uint64_t now_ms);
int ninlil_network_path_valid(const ninlil_network_path *path);
int ninlil_network_plan_valid(const ninlil_network_plan *plan);
size_t ninlil_network_plan_encode(const ninlil_network_plan *plan,
                                  uint8_t *output, size_t capacity);
int ninlil_network_plan_decode(const uint8_t *input, size_t length,
                               ninlil_network_plan *plan);
typedef struct ninlil_remove_status {
    uint16_t dependent_flows;
    uint16_t reroutable_flows;
    uint16_t blocked_flows;
    uint8_t ready;
} ninlil_remove_status;
int ninlil_coordinator_remove_status(ninlil_coordinator *c, uint16_t peer,
                                     uint64_t now_ms, int custody_drained,
                                     ninlil_remove_status *status);
/* Bounded EWMA sample filter; retransmitted samples never alter the estimate.
 */
uint32_t ninlil_network_rto(uint32_t current_ms, uint32_t sample_ms,
                            int retransmitted, uint32_t minimum_ms);

int ninlil_coordinator_route(void *ctx, uint16_t source, uint16_t target,
                             uint64_t now_ms, ninlil_network_plan *plan);
#endif
