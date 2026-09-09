#ifndef NINLIL_ROUTED_H
#define NINLIL_ROUTED_H

#include "ninlil_relay.h"
#include "ninlil_secure.h"

#define NINLIL_ROUTED_ACK_CACHE 64u
#define NINLIL_ROUTED_TX_CACHE 64u
#define NINLIL_ROUTED_RETRY_WINDOW_MS 30000u

typedef ninlil_secure_session *(*ninlil_session_lookup_fn)(void *ctx,
                                                           uint16_t peer,
                                                           int hop);
typedef int (*ninlil_route_lookup_fn)(void *ctx, uint16_t source,
                                      uint16_t target, uint64_t now_ms,
                                      ninlil_network_plan *plan);
typedef int (*ninlil_route_emit_fn)(void *ctx, uint16_t next,
                                    ninlil_traffic_class traffic,
                                    const uint8_t *frame, size_t length);
typedef int (*ninlil_packet_digest_fn)(const uint8_t *data, size_t length,
                                       uint8_t id[16]);

typedef struct ninlil_routed_config {
    uint16_t local;
    ninlil_policy_lookup policy;
    void *policy_ctx;
    ninlil_session_lookup_fn session;
    void *session_ctx;
    ninlil_route_lookup_fn route;
    void *route_ctx;
    ninlil_route_emit_fn emit;
    void *emit_ctx;
    ninlil_packet_digest_fn digest;
    ninlil_relay *relay;
} ninlil_routed_config;

typedef struct ninlil_routed {
    ninlil_routed_config config;
    ninlil_runtime *core;
    uint64_t now_ms;
    uint8_t ack_cache[NINLIL_ROUTED_ACK_CACHE][16];
    uint16_t ack_cursor;
    /* Volatile ciphertext memoization, not another custody owner. A repeated
     * Core packet keeps its opaque Relay identity for a bounded retry window.
     * Expiry, eviction or a fresh context renews the envelope so a packet
     * cannot remain behind the replay window forever. Core ownership is
     * unchanged.
     */
    struct {
        uint64_t created_ms;
        uint8_t packet_key[16];
        uint8_t ciphertext[NINLIL_RELAY_CIPHERTEXT_MAX];
        uint16_t length;
    } tx_cache[NINLIL_ROUTED_TX_CACHE];
    uint16_t tx_cursor;
    uint32_t rejected;
} ninlil_routed;

/* One owner calls receive/poll and Core step sequentially. E2E sessions must
 * use exporter label 32768; hop sessions use 32769 with separate counters.
 * Emit OK is scheduler admission only. The Core owns source retry; each
 * intermediate owns its committed opaque hop copy. Final ACK follows ingest
 * commit, never receipt of an unauthenticated/enqueued packet. */
int ninlil_routed_open(ninlil_routed *r, const ninlil_routed_config *config,
                       ninlil_link *core_link);
void ninlil_routed_attach(ninlil_routed *r, ninlil_runtime *core);
int ninlil_routed_receive(ninlil_routed *r, const uint8_t *frame, size_t length,
                          uint64_t now_ms);
/* Revalidate a staged hop frame immediately before physical transmission. */
int ninlil_routed_frame_current(ninlil_routed *r, const uint8_t *frame,
                                size_t length);
int ninlil_routed_poll(ninlil_routed *r, uint64_t now_ms);
/* Apply the effective plan to boot-local Core retry scheduling. Caller must
 * drive Core at the declared fixed step duration. No evidence changes. */
int ninlil_routed_apply_rto(ninlil_routed *r, uint16_t target,
                            uint32_t step_ms);
int ninlil_psa_packet_digest(const uint8_t *data, size_t length,
                             uint8_t id[16]);

#endif
