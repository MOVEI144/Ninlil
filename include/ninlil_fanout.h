#ifndef NINLIL_FANOUT_H
#define NINLIL_FANOUT_H
#include "ninlil.h"
#define NINLIL_FANOUT_TARGETS 512u
#define NINLIL_FANOUT_WORK 32u
#define NINLIL_FANOUT_SCHEMA 1u

typedef struct ninlil_fanout_contract {
    ninlil_id operation;
    uint8_t authority[16], source[32], payload_digest[32];
    uint64_t authority_epoch, deadline_ms, payload_reference;
    uint16_t service;
    ninlil_traffic_class traffic;
    ninlil_evidence evidence;
} ninlil_fanout_contract;
typedef struct ninlil_fanout_target {
    uint8_t identity[32];
    uint64_t membership_epoch, binding_epoch;
    uint16_t address;
    ninlil_id idempotency_key;
} ninlil_fanout_target;
typedef enum ninlil_fanout_phase {
    NINLIL_FANOUT_PENDING = 0,
    NINLIL_FANOUT_INTENT = 1,
    NINLIL_FANOUT_ADMITTED = 2,
    NINLIL_FANOUT_TERMINAL = 3
} ninlil_fanout_phase;
typedef struct ninlil_fanout_item {
    ninlil_id message;
    uint64_t next_ms;
    ninlil_outcome outcome;
    ninlil_evidence latest;
    ninlil_fanout_phase phase;
    int wait_reason;
} ninlil_fanout_item;
typedef enum ninlil_fanout_record_kind {
    NINLIL_FANOUT_START = 1,
    NINLIL_FANOUT_TARGET_INTENT = 2,
    NINLIL_FANOUT_TARGET_ADMITTED = 3,
    NINLIL_FANOUT_TARGET_TERMINAL = 4
} ninlil_fanout_record_kind;
/* Typed storage boundary, NOT a struct-to-wire format. commit copies everything
 * it borrows synchronously. START must atomically retain the complete contract,
 * canonical target snapshot, and the payload referenced by that contract. */
typedef struct ninlil_fanout_record {
    uint16_t schema, count, index;
    uint64_t sequence;
    ninlil_fanout_record_kind kind;
    ninlil_fanout_contract contract;
    const ninlil_fanout_target *targets; /* START only */
    ninlil_id message;
    ninlil_outcome outcome;
    ninlil_evidence evidence;
} ninlil_fanout_record;
typedef struct ninlil_fanout_callbacks {
    int (*commit)(void *ctx, const ninlil_fanout_record *record);
    /* Checks current authorization, identity/epochs, wake/route/capacity.
     * BUSY/NOT_FOUND/UNAUTHORIZED/CAPACITY suspend service, never ownership. */
    int (*eligible)(void *ctx, const ninlil_fanout_contract *contract,
                    const ninlil_fanout_target *target);
    /* Must durably bind identity+epochs to the Core message, through every
     * autonomous retry and final transmit. A legacy address-only submit adapter
     * does NOT satisfy this interface. Exact repeated intents return the same
     * ID. This module deliberately does not silently wrap legacy ninlil_submit.
     */
    int (*admit_bound)(void *ctx, const ninlil_fanout_contract *contract,
                       const ninlil_fanout_target *target, ninlil_id *message);
    /* Query this exact bound message's authoritative Core evidence. */
    int (*query_bound)(void *ctx, const ninlil_fanout_contract *contract,
                       const ninlil_fanout_target *target,
                       const ninlil_id *message, ninlil_info *info);
    void *ctx;
} ninlil_fanout_callbacks;
typedef struct ninlil_fanout {
    ninlil_fanout_contract contract;
    ninlil_fanout_target *targets;
    ninlil_fanout_item *items;
    ninlil_fanout_callbacks callbacks;
    uint64_t record_sequence, now_ms;
    uint16_t capacity, count, cursor;
    uint8_t started, poisoned;
} ninlil_fanout;
typedef struct ninlil_fanout_status {
    uint16_t total, pending, intent, active, satisfied, terminal_other, unknown;
    uint8_t all_terminal, all_satisfied;
} ninlil_fanout_status;
/* Single caller-owned operation; application may own at most four instances.
 * Workspace arrays each contain capacity entries. No allocations or threads.
 * Targets are in canonical ascending identity order, with unique nonzero keys.
 * Registration can contain 512 targets; a 512-node domain has at most 511
 * peers. The adapter owns the persistent codec/backend and same-owner Core
 * binding.
 */
int ninlil_fanout_open(ninlil_fanout *owner, ninlil_fanout_target *targets,
                       ninlil_fanout_item *items, uint16_t capacity,
                       const ninlil_fanout_callbacks *callbacks);
int ninlil_fanout_start(ninlil_fanout *owner,
                        const ninlil_fanout_contract *contract,
                        const ninlil_fanout_target *targets, uint16_t count);
/* Replays typed, authoritative records; never invokes commit or Core callbacks.
 * Reopen/replay is mandatory after an ambiguous write. No implicit forgetting.
 */
int ninlil_fanout_restore(ninlil_fanout *owner,
                          const ninlil_fanout_record *record);
/* <=32 target service opportunities per call, not a claim of 32 simultaneous
 * RF transmissions. Timed waiting releases the opportunity but keeps the
 * intent, message ID and ownership. Rotation lets reachable targets bypass a
 * partition.
 */
int ninlil_fanout_step(ninlil_fanout *owner, uint64_t now_ms,
                       unsigned int work);
int ninlil_fanout_inspect(const ninlil_fanout *owner,
                          ninlil_fanout_status *status);
#endif
