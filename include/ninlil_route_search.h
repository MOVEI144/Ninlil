#ifndef NINLIL_ROUTE_SEARCH_H
#define NINLIL_ROUTE_SEARCH_H
#include "ninlil.h"
#define NINLIL_SEARCH_NODES 512u
#define NINLIL_SEARCH_EDGES 2048u
#define NINLIL_SEARCH_FRONTIER 64u
#define NINLIL_SEARCH_CANDIDATES 16u
#define NINLIL_SEARCH_PATH 5u
#define NINLIL_SEARCH_KNOWN_ALL 15u

typedef struct ninlil_search_node {
    uint16_t address;
    uint64_t membership_epoch, session_epoch;
    ninlil_role role;
    uint32_t capabilities, failure_domain;
} ninlil_search_node;
typedef struct ninlil_search_edge {
    uint16_t from, to; /* Dense snapshot indexes, not radio addresses. */
    uint16_t attempts, delivered;
    uint32_t profile, exchange_us, queue_us, wake_us, commit_us;
    uint64_t observed_ms, from_epoch, to_epoch;
    uint8_t known; /* bit0 exchange, bit1 queue, bit2 wake, bit3 commit */
} ninlil_search_edge;
typedef struct ninlil_search_path {
    uint16_t nodes[NINLIL_SEARCH_PATH]; /* Dense indexes. */
    uint32_t profiles[NINLIL_SEARCH_PATH - 1u];
    uint64_t cost_us;
    uint8_t count;
} ninlil_search_path;
/* Must validate authorization AND shared radio/capacity/wake constraints.
 * Called on complete candidates, never given permission to mutate a plan.
 * Returning OK admits a proposal for later staging, not a delivery. */
typedef int (*ninlil_search_validate)(void *ctx, const ninlil_search_path *path);
typedef struct ninlil_search_config {
    const ninlil_search_node *nodes;
    const ninlil_search_edge *edges;
    const uint64_t *current_generation;
    uint64_t generation, now_ms;
    uint32_t max_age_ms, work_limit;
    uint16_t node_count, edge_count, source, target, excluded;
    ninlil_search_validate validate;
    void *validate_ctx;
} ninlil_search_config;
typedef struct ninlil_search_result {
    ninlil_search_path paths[3];
    uint32_t work;
    uint8_t count, search_limited, declared_disjoint; /* Only declared intermediate failure domains, NOT physical independence. */
} ninlil_search_result;
typedef struct ninlil_route_search {
    ninlil_search_config config;
    ninlil_search_path frontier[NINLIL_SEARCH_FRONTIER];
    ninlil_search_path candidates[NINLIL_SEARCH_CANDIDATES], expanding;
    uint32_t work;
    uint16_t edge_cursor;
    uint8_t frontier_count, candidate_count, active, done, limited;
    int error;
} ninlil_route_search;
/* Config arrays and generation pointer are borrowed, immutable until finish.
 * One caller-owned bounded workspace, no recursion or allocation. Begin does
 * bounded structural validation; step inspects at most work<=64 edges/pops.
 * UINT16_MAX means no excluded node. Budgets are 1..4096 work units.
 * Any snapshot change invalidates the proposal. No global-optimum claim. */
int ninlil_route_search_begin(ninlil_route_search *search,
                              const ninlil_search_config *config);
int ninlil_route_search_step(ninlil_route_search *search, unsigned int work);
int ninlil_route_search_result(const ninlil_route_search *search,
                               ninlil_search_result *result);
#endif
