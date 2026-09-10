#ifndef NINLIL_FANOUT_STORE_H
#define NINLIL_FANOUT_STORE_H
#include "ninlil_fanout.h"
#define NINLIL_FANOUT_STORE_MAX_BYTES (UINT64_C(1024) * 1024u)

typedef struct ninlil_fanout_store ninlil_fanout_store;
typedef enum ninlil_fanout_store_mode {
    NINLIL_FANOUT_STORE_INITIALIZE = 1,
    NINLIL_FANOUT_STORE_RESUME = 2
} ninlil_fanout_store_mode;

/* Synchronous, caller-owned SHA-256 backend, not a custom hash or signature.
 * Output must remain unchanged on failure; ctx outlives the open store. */
typedef int (*ninlil_fanout_sha256)(void *ctx, const uint8_t *bytes,
                                    size_t length, uint8_t output[32]);
typedef struct ninlil_fanout_store_config {
    const char *location;
    uint64_t maximum_bytes;
    uint16_t target_capacity;
    uint8_t source_identity[32];
    ninlil_id operation;
    ninlil_fanout_sha256 sha256;
    void *hash_ctx;
    int (*eligible)(void *ctx, const ninlil_fanout_contract *contract,
                    const ninlil_fanout_target *target);
    /* Must bind the immutable identity/epochs to EVERY Core retry and final TX.
     * A legacy address-only ninlil_submit wrapper is NOT a conforming adapter.
     * Repeated key/contract must recover the same ID even after reply loss.
     * Payload is re-read/verified and borrowed only during this call. */
    int (*admit_bound)(void *ctx, const ninlil_fanout_contract *contract,
                       const ninlil_fanout_target *target,
                       const uint8_t *payload, uint16_t length,
                       ninlil_id *message);
    int (*query_bound)(void *ctx, const ninlil_fanout_contract *contract,
                       const ninlil_fanout_target *target,
                       const ninlil_id *message, ninlil_info *info);
    void *delivery_ctx;
} ninlil_fanout_store_config;

/* One operation per separate journal, exclusive execution owner, no tasks/RF.
 * Allocates at open, bounded by target_capacity <=512; no per-packet
 * allocation. Uses the existing selected POSIX/raw-Flash journal. Close
 * releases memory, NEVER deletes the file, target obligations, payload or
 * outcome history. RESUME rejects a missing/incomplete START; it never silently
 * initializes it. Journal open may create an empty backing file or repair an
 * uncommitted tail. INITIALIZE permits explicit completion of an interrupted,
 * identical START. Config is copied; callback contexts and location's backing
 * storage outlive it. No external delivery callbacks run during open, replay,
 * start or inspection. */
size_t ninlil_fanout_store_memory(uint16_t capacity);
int ninlil_fanout_store_open(ninlil_fanout_store **out,
                             const ninlil_fanout_store_config *config,
                             ninlil_fanout_store_mode mode);
void ninlil_fanout_store_close(ninlil_fanout_store *store);
/* At most N+3 record appends. Header, targets and body are provisional until
 * the final seal commits/readbacks; OK then means owned, NOT delivered.
 * 0..256 payload bytes, exact SHA-256 equals contract.payload_digest.
 * A resumed partial START must match every retained byte; no replacement.
 * Disk/commit errors poison this handle; close/reopen, do not retry in place.
 */
int ninlil_fanout_store_start(ninlil_fanout_store *store,
                              const ninlil_fanout_contract *contract,
                              const ninlil_fanout_target *targets,
                              uint16_t count, const uint8_t *payload,
                              uint16_t length);
/* Runs actual fanout owner with verified persistent callbacks; <=32 targets.
 * Each admission is preceded by durable INTENT and referenced-record integrity
 * checks. Time is boot-local for scheduling; deadline policy belongs to Core.
 */
int ninlil_fanout_store_step(ninlil_fanout_store *store, uint64_t now_ms,
                             unsigned int work);
/* Revalidate retained records before returning success/cached metadata.
 * EMPTY means no committed START, not a successful/empty fanout.
 * Outputs remain unchanged on error. No custody is retired here. */
int ninlil_fanout_store_inspect(ninlil_fanout_store *store,
                                ninlil_fanout_status *status);
int ninlil_fanout_store_target(ninlil_fanout_store *store, uint16_t index,
                               ninlil_fanout_target *target,
                               ninlil_fanout_item *item);
int ninlil_fanout_store_payload(ninlil_fanout_store *store, uint8_t *output,
                                size_t capacity, uint16_t *written);
/* Verified immutable START metadata. No delivery callbacks. */
int ninlil_fanout_store_contract(ninlil_fanout_store *store,
                                 ninlil_fanout_contract *contract,
                                 uint16_t *count);
#endif
