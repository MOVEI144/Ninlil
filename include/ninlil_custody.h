#ifndef NINLIL_CUSTODY_H
#define NINLIL_CUSTODY_H

#include "ninlil.h"

#include <stddef.h>
#include <stdint.h>

#define NINLIL_HOST_SPOOL_MAX_MESSAGES 4096u
#define NINLIL_HOST_SPOOL_MAX_BYTES (UINT64_C(64) * 1024u * 1024u)
#define NINLIL_HOST_PENDING_PER_PEER_MAX 8u
#define NINLIL_HOST_RECONNECT_INITIAL_MS 250u
#define NINLIL_GATEWAY_RECONNECT_INITIAL_MS 500u
#define NINLIL_RECONNECT_MAX_MS 30000u

typedef enum ninlil_custody_reconnect_kind {
    NINLIL_CUSTODY_RECONNECT_HOST = 1,
    NINLIL_CUSTODY_RECONNECT_GATEWAY = 2
} ninlil_custody_reconnect_kind;

typedef enum ninlil_custody_record_type {
    NINLIL_CUSTODY_RECORD_ADMIT = 1,
    NINLIL_CUSTODY_RECORD_EVIDENCE = 2,
    NINLIL_CUSTODY_RECORD_ROUTE = 3,
    NINLIL_CUSTODY_RECORD_FORGET = 4
} ninlil_custody_record_type;

typedef struct ninlil_custody_entry {
    ninlil_id message_id;
    uint64_t payload_token;
    uint64_t route_epoch;
    uint32_t payload_bytes;
    uint16_t peer;
    ninlil_evidence evidence;
    uint8_t used;
    uint8_t terminal;
} ninlil_custody_entry;

typedef int (*ninlil_custody_commit)(void *ctx,
                                     ninlil_custody_record_type record_type,
                                     const ninlil_custody_entry *entry);

typedef struct ninlil_custody_spool {
    ninlil_custody_entry *entries;
    uint16_t capacity;
    uint16_t pending_per_peer;
    uint16_t live;
    uint64_t byte_limit;
    uint64_t live_bytes;
    ninlil_custody_commit commit;
    void *commit_ctx;
    uint8_t poisoned;
} ninlil_custody_spool;

/* entries are caller-owned metadata. Payload bodies remain in the durable
 * store identified by payload_token. commit() must return OK only after the
 * record is authoritative; any other result poisons this in-memory view. */
int ninlil_custody_reconnect_delay(ninlil_custody_reconnect_kind kind,
                                   uint8_t retry_count, uint32_t *delay_ms);
int ninlil_custody_open(ninlil_custody_spool *spool,
                        ninlil_custody_entry *entries, uint16_t capacity,
                        uint64_t byte_limit, uint16_t pending_per_peer,
                        ninlil_custody_commit commit, void *commit_ctx);
/* restore is used only while replaying the authoritative durable spool. */
int ninlil_custody_restore(ninlil_custody_spool *spool,
                           ninlil_custody_record_type record_type,
                           const ninlil_custody_entry *entry);
int ninlil_custody_admit(ninlil_custody_spool *spool, const ninlil_id *id,
                         uint16_t peer, uint32_t payload_bytes,
                         uint64_t payload_token, uint64_t route_epoch,
                         ninlil_evidence local_custody);
int ninlil_custody_note_evidence(ninlil_custody_spool *spool,
                                 const ninlil_id *id, ninlil_evidence evidence);
int ninlil_custody_update_route(ninlil_custody_spool *spool,
                                const ninlil_id *id, uint64_t route_epoch);
/* Reconnect replay returns every non-terminal entry; Gateway custody does not
 * remove Host payload ownership. */
int ninlil_custody_replay_next(const ninlil_custody_spool *spool,
                               uint16_t *cursor, ninlil_custody_entry *entry);
/* Product/integration code may forget only a terminal tombstone. */
int ninlil_custody_forget(ninlil_custody_spool *spool, const ninlil_id *id);

#endif
