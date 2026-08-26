#ifndef NINLIL_SECURITY_STATE_H
#define NINLIL_SECURITY_STATE_H

#include "ninlil.h"

#include <stddef.h>
#include <stdint.h>

#define NINLIL_SECURITY_SECTOR_SIZE 4096u
#define NINLIL_SECURITY_PARTITION_SIZE (2u * NINLIL_SECURITY_SECTOR_SIZE)
#define NINLIL_SECURITY_FINGERPRINT_BYTES 16u
#define NINLIL_SECURITY_COUNTER_MAX_EXCLUSIVE (UINT64_C(1) << 40)

#define NINLIL_DIRECTION_INITIATOR_TO_RESPONDER 0u
#define NINLIL_DIRECTION_RESPONDER_TO_INITIATOR 1u

/*
 * Synchronous NOR-flash boundary used by the portable security-state store.
 * The caller owns ctx and must keep it valid while a store is open. erase()
 * accepts sector-aligned ranges; write() must implement NOR 1-to-0 semantics.
 * One execution owner may use a partition at a time.
 */
typedef struct ninlil_security_io {
    int (*read)(void *ctx, size_t offset, uint8_t *buffer, size_t length);
    int (*write)(void *ctx,
                 size_t offset,
                 const uint8_t *buffer,
                 size_t length);
    int (*erase)(void *ctx, size_t offset, size_t length);
    void *ctx;
    size_t size;
} ninlil_security_io;

typedef enum ninlil_counter_open_mode {
    NINLIL_COUNTER_CREATE_NEW = 1,
    NINLIL_COUNTER_RESUME_EXISTING = 2
} ninlil_counter_open_mode;

/* session_fingerprint identifies key material but is not itself a secret. */
typedef struct ninlil_counter_config {
    uint8_t session_fingerprint[NINLIL_SECURITY_FINGERPRINT_BYTES];
    uint8_t direction;
    uint32_t reservation_size;
    uint64_t max_counter_exclusive;
} ninlil_counter_config;

typedef struct ninlil_counter_store {
    ninlil_security_io io;
    ninlil_counter_config config;
    uint64_t next_counter;
    uint64_t reserved_until;
    uint32_t generation;
    int8_t current_slot;
    uint8_t opened;
    uint8_t poisoned;
} ninlil_counter_store;

/*
 * Destructively erases and verifies the complete two-sector partition.
 * This is a provisioning/rekey operation, not a substitute for membership
 * revocation. A caller interrupted before success must retry explicitly.
 */
int ninlil_security_format(const ninlil_security_io *io);

/*
 * CREATE_NEW succeeds only on a fully erased partition and durably reserves
 * the first counter block before returning. RESUME_EXISTING requires an exact
 * config match and durably reserves a fresh block, abandoning every unused
 * counter from the previous boot. No counter is returned before reservation
 * commit and read-back verification complete.
 */
int ninlil_counter_open(ninlil_counter_store *store,
                        const ninlil_security_io *io,
                        ninlil_counter_open_mode mode,
                        const ninlil_counter_config *config);

/* Returns one unique counter. Errors leave *counter unchanged. */
int ninlil_counter_next(ninlil_counter_store *store, uint64_t *counter);
void ninlil_counter_close(ninlil_counter_store *store);

typedef enum ninlil_membership_state {
    NINLIL_MEMBERSHIP_ACTIVE = 1,
    NINLIL_MEMBERSHIP_REVOKED = 2
} ninlil_membership_state;

/*
 * authority_fingerprint binds the record to the authenticated authority that
 * issued it. Capability meaning is owned by the protocol layer; this storage
 * layer preserves the validated bitset without inventing policy.
 */
typedef struct ninlil_membership_record {
    uint8_t authority_fingerprint[NINLIL_SECURITY_FINGERPRINT_BYTES];
    uint16_t node_id;
    uint64_t membership_epoch;
    uint64_t binding_epoch;
    uint32_t capabilities;
    ninlil_membership_state state;
} ninlil_membership_record;

typedef struct ninlil_membership_store {
    ninlil_security_io io;
    ninlil_membership_record record;
    uint32_t generation;
    int8_t current_slot;
    uint8_t has_record;
    uint8_t opened;
    uint8_t poisoned;
} ninlil_membership_store;

/* Empty partitions open successfully; get() then returns NOT_FOUND. */
int ninlil_membership_open(ninlil_membership_store *store,
                           const ninlil_security_io *io);
int ninlil_membership_get(const ninlil_membership_store *store,
                          ninlil_membership_record *record);

/*
 * Activation is idempotent for an identical active record. Any replacement
 * must keep the same authority, strictly increase membership_epoch, and never
 * decrease binding_epoch. A revoked record therefore cannot be replayed.
 */
int ninlil_membership_activate(ninlil_membership_store *store,
                               const ninlil_membership_record *record);
int ninlil_membership_revoke(ninlil_membership_store *store);
void ninlil_membership_close(ninlil_membership_store *store);

#endif
