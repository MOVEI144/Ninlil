#ifndef NINLIL_FANOUT_STORE_INTERNAL_H
#define NINLIL_FANOUT_STORE_INTERNAL_H
#include "ninlil_fanout_internal.h"
#include "ninlil_fanout_store.h"
#include "ninlil_journal.h"
#define NFS_HEADER 1u
#define NFS_TARGET 2u
#define NFS_PAYLOAD 3u
#define NFS_SEAL 4u
#define NFS_DELTA 5u
#define NFS_HEADER_SIZE 132u
#define NFS_TARGET_SIZE 88u
#define NFS_PAYLOAD_HEADER 22u
#define NFS_SEAL_SIZE 24u
#define NFS_DELTA_SIZE 49u
#define NFS_MAX_RECORD 278u

typedef struct nfs_ref {
    ninlil_journal_ref target, state;
    uint64_t sequence;
} nfs_ref;
struct ninlil_fanout_store {
    ninlil_fanout_store_config config;
    ninlil_fanout owner;
    ninlil_fanout_target *targets;
    ninlil_fanout_item *items;
    nfs_ref *refs;
    ninlil_journal *journal;
    ninlil_fanout_contract contract;
    ninlil_journal_ref header, payload, seal;
    uint16_t count, loaded, payload_length;
    uint8_t has_header, has_payload, sealed, busy;
    int fault;
    const uint8_t *starting_payload;
};
static inline int ninlil_fanout_store_fail(ninlil_fanout_store *s, int rc)
{
    s->fault = rc;
    s->owner.poisoned = 1u;
    return rc;
}
void ninlil_fanout_store_header_encode(const ninlil_fanout_contract *c,
                                       uint16_t count, uint16_t length,
                                       uint8_t out[132]);
int ninlil_fanout_store_header_decode(const uint8_t *data, size_t size,
                                      ninlil_fanout_contract *c,
                                      uint16_t *count, uint16_t *length);
void ninlil_fanout_store_target_encode(const ninlil_id *op, uint16_t index,
                                       const ninlil_fanout_target *target,
                                       uint8_t out[88]);
int ninlil_fanout_store_target_decode(const uint8_t *data, size_t size,
                                      const ninlil_id *op, uint16_t index,
                                      ninlil_fanout_target *target);
void ninlil_fanout_store_delta_encode(const ninlil_fanout_record *record,
                                      uint8_t out[49]);
int ninlil_fanout_store_delta_decode(const uint8_t *data, size_t size,
                                     const ninlil_fanout_contract *c,
                                     ninlil_fanout_record *record);
void ninlil_fanout_store_seal_encode(const ninlil_id *op, uint16_t count,
                                     uint16_t length, uint8_t out[24]);
void ninlil_fanout_store_payload_encode(const ninlil_id *op,
                                        const uint8_t *payload, uint16_t length,
                                        uint8_t out[NFS_MAX_RECORD]);
int ninlil_fanout_store_payload_decode(const uint8_t *data, size_t size,
                                       const ninlil_id *op, uint16_t length);
int ninlil_fanout_store_verify(ninlil_fanout_store *s, int all);
int ninlil_fanout_store_verify_target(ninlil_fanout_store *s, uint16_t index);
int ninlil_fanout_store_load_payload(ninlil_fanout_store *s,
                                     uint8_t output[256]);
int ninlil_fanout_store_replay(void *ctx, uint8_t type, const uint8_t *data,
                               uint16_t length, const ninlil_journal_ref *ref);
int ninlil_fanout_store_commit(void *ctx, const ninlil_fanout_record *record);
#endif
