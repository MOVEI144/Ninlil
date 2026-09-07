#ifndef NINLIL_RELAY_H
#define NINLIL_RELAY_H

#include "ninlil_network.h"

#define NINLIL_RELAY_PACKETS_MAX 64u
#define NINLIL_RELAY_CIPHERTEXT_MAX 144u
#define NINLIL_RELAY_HEADER 48u
#define NINLIL_RELAY_FRAME_MAX 192u

typedef struct ninlil_relay_record {
    ninlil_network_path path;
    uint64_t route_epoch;
    uint8_t packet_id[16];
    uint8_t ciphertext[NINLIL_RELAY_CIPHERTEXT_MAX];
    uint16_t length;
    uint8_t done;
    uint8_t control;
    ninlil_traffic_class traffic;
} ninlil_relay_record;

typedef struct ninlil_relay_slot {
    ninlil_relay_record record;
    uint64_t next_attempt_ms;
    uint32_t attempts;
    uint8_t used;
} ninlil_relay_slot;

typedef int (*ninlil_relay_commit_fn)(void *ctx,
                                      const ninlil_relay_record *record);
typedef int (*ninlil_relay_verify_fn)(void *ctx,
                                      const ninlil_relay_record *record);
typedef int (*ninlil_relay_route_check)(void *ctx,
                                        const ninlil_network_path *path,
                                        uint64_t epoch, uint64_t now_ms);

typedef struct ninlil_relay {
    ninlil_relay_slot *slots;
    uint16_t capacity;
    uint16_t local;
    uint16_t cursor;
    uint32_t retry_ms;
    ninlil_policy_lookup policy;
    void *policy_ctx;
    ninlil_relay_commit_fn commit;
    ninlil_relay_verify_fn verify;
    void *commit_ctx;
    ninlil_relay_route_check route_check;
    void *route_ctx;
    uint8_t draining;
    uint8_t poisoned;
    uint64_t drain_epoch;
} ninlil_relay;

/* Source/final nodes keep Core ownership separately. This powered intermediate
 * queue stores only end-to-end ciphertext; it never decrypts application data.
 * The inbound transport must verify the hop AEAD before passing authenticated
 * sender here. A next-hop custody ACK can retire this hop copy, never the
 * source Core message. Retry has no discard limit. Storage errors poison this
 * view. */
int ninlil_relay_open(ninlil_relay *r, ninlil_relay_slot *slots,
                      uint16_t capacity, uint16_t local, ninlil_role role,
                      uint32_t capabilities, uint32_t retry_ms,
                      ninlil_policy_lookup policy, void *policy_ctx,
                      ninlil_relay_commit_fn commit,
                      ninlil_relay_verify_fn verify, void *commit_ctx,
                      ninlil_relay_route_check route_check, void *route_ctx);
int ninlil_relay_restore(ninlil_relay *r, const ninlil_relay_record *record);
int ninlil_relay_receive(ninlil_relay *r, uint16_t authenticated_sender,
                         const ninlil_relay_record *record, uint64_t now_ms);
int ninlil_relay_next(ninlil_relay *r, uint64_t now_ms, uint16_t *next_hop,
                      ninlil_relay_record *record);
int ninlil_relay_ack(ninlil_relay *r, uint16_t authenticated_sender,
                     const uint8_t packet_id[16], uint64_t route_epoch);
/* Drain rejects new custody but keeps retries and ACK processing alive.
 * READY requires no owned packets. Cancellation never restores revoked rights.
 */
int ninlil_relay_drain(ninlil_relay *r, int draining);
int ninlil_relay_repair(ninlil_relay *r, const uint8_t packet_id[16],
                        const ninlil_network_path *path, uint64_t epoch,
                        uint64_t now_ms);
/* Explicit return of an obsolete E2E envelope to its original logical owner.
 * The authenticated source must have recovered its authoritative Core store,
 * closed the old context and confirmed that all its logical messages remain
 * owned or have durable terminal evidence. This is not delivery evidence.
 * A fresh-context confirmation is required; timeouts never call this API.
 * Retires at most one hop copy per call; EMPTY means no matching copy remains.
 */
int ninlil_relay_return_to_source(ninlil_relay *r,
                                  uint16_t authenticated_source,
                                  uint16_t final_target,
                                  const uint8_t old_context[16],
                                  const uint8_t fresh_context[16]);
int ninlil_relay_ready_remove(const ninlil_relay *r);
size_t ninlil_relay_encode(const ninlil_relay_record *record, uint8_t *output,
                           size_t capacity);
int ninlil_relay_decode(const uint8_t *input, size_t length,
                        ninlil_relay_record *record);

#endif
