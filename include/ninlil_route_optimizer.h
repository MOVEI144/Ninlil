#ifndef NINLIL_ROUTE_OPTIMIZER_H
#define NINLIL_ROUTE_OPTIMIZER_H
#include "ninlil_network.h"
#include "ninlil_route_search.h"

#define NINLIL_OPTIMIZER_MAX_AGE_MS 5000u
/* Extra current-resource check. Must be bounded, read-only, and return a
 * NINLIL_ERR_* on refusal. This callback is NOT a grant of RF permission.
 * NULL selects explicitly best-effort ranking; no admission guarantee is made.
 */
typedef int (*ninlil_route_constraint)(void *ctx,
                                       const ninlil_network_path *path,
                                       uint64_t now_ms);
struct ninlil_route_optimizer {
    ninlil_route_search search;
    ninlil_search_node *nodes;
    ninlil_search_edge *edges;
    ninlil_search_result result;
    ninlil_coordinator *owner;
    ninlil_route_constraint constraint;
    void *constraint_ctx;
    uint64_t generation, began_ms, now_ms;
    uint16_t node_capacity, edge_capacity, source, target, excluded;
    uint32_t work_limit;
    uint8_t opened, active, ready;
    int result_code;
};
/* One caller-owned workspace and snapshot arrays. No allocation, threads or IO.
 * Arrays are borrowed exclusively until detached/owner closed. 2..512 nodes,
 * 1..2048 edges; size them for the actual owner's capacities (reference 16/64).
 * All candidates are provisional fixed-PHY best-effort paths: legacy reports
 * measure full-size probe airtime + queue, NOT complete evidence latency.
 * This mode preserves unknown wake/commit costs instead of inventing zeros.
 * OPEN does not attach or touch a Coordinator. */
int ninlil_route_optimizer_open(ninlil_route_optimizer *optimizer,
                                ninlil_search_node *nodes,
                                uint16_t node_capacity,
                                ninlil_search_edge *edges,
                                uint16_t edge_capacity, uint32_t work_limit,
                                ninlil_route_constraint constraint,
                                void *constraint_ctx);
int ninlil_route_optimizer_attach(ninlil_route_optimizer *optimizer,
                                  ninlil_coordinator *coordinator);
/* Keep any old committed route usable while searching. At most 64 search work
 * units/call, one outstanding request; other requests return BUSY without
 * resetting that request. node_step drives this automatically after attachment.
 * Standalone users must drive step themselves. No implicit early publication.
 */
int ninlil_route_optimizer_step(ninlil_route_optimizer *optimizer,
                                ninlil_coordinator *coordinator,
                                uint64_t now_ms, unsigned int work);
/* Used by coordinator_select; starts a snapshot when idle. Completed proposals
 * recheck membership epochs, relay rights, directed observations in BOTH
 * directions and the optional resource validator. Output unchanged on error. */
int ninlil_route_optimizer_select(ninlil_route_optimizer *optimizer,
                                  ninlil_coordinator *coordinator,
                                  uint16_t source, uint16_t target,
                                  uint16_t excluded, uint64_t now_ms,
                                  ninlil_network_path *out);
/* Also called immediately before durable stage, including manual stage calls.
 * No prepare/apply/lease gate is bypassed by this planning helper. */
int ninlil_route_optimizer_validate(ninlil_route_optimizer *optimizer,
                                    ninlil_coordinator *coordinator,
                                    const ninlil_network_path *path,
                                    uint64_t now_ms);
#endif
