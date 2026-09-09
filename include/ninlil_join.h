#ifndef NINLIL_JOIN_H
#define NINLIL_JOIN_H

#include "ninlil.h"

#define NINLIL_JOIN_PEERS_MAX 512u
#define NINLIL_JOIN_PROVISIONAL_MAX 64u
#define NINLIL_JOIN_SERVICES_MAX 8u
#define NINLIL_JOIN_TIMEOUT_MS 30000u
#define NINLIL_JOIN_RECORD_MAX 156u

typedef enum ninlil_join_state {
    NINLIL_JOIN_PENDING = 1,
    NINLIL_JOIN_ACTIVE = 2,
    NINLIL_JOIN_REVOKED = 3
} ninlil_join_state;

typedef struct ninlil_join_grant {
    uint8_t identity[32];
    uint8_t authority[16];
    uint16_t node;
    uint64_t membership_epoch;
    uint64_t binding_epoch;
    uint32_t capabilities;
    ninlil_role role;
    uint8_t service_count;
    ninlil_service_grant services[NINLIL_JOIN_SERVICES_MAX];
} ninlil_join_grant;

/* Versioned durable record, transported only inside an authenticated channel.
 * Session bytes are transaction identity, not keys. A restored ACTIVE record
 * never restores a ready session. The storage adapter must encode fields, not
 * persist compiler struct padding. */
typedef struct ninlil_join_record {
    ninlil_join_grant grant;
    uint8_t transaction[16];
    ninlil_join_state state;
} ninlil_join_record;

typedef struct ninlil_join_peer {
    ninlil_join_record record;
    uint8_t identity[32];
    uint8_t session[16];
    uint64_t began_ms;
    uint8_t used;
    uint8_t persisted;
    uint8_t phase;
    uint8_t session_ready;
} ninlil_join_peer;

typedef int (*ninlil_join_commit_fn)(void *ctx,
                                     const ninlil_join_record *record);
typedef int (*ninlil_join_approve_fn)(void *ctx, const uint8_t identity[32],
                                      ninlil_join_grant *grant);

typedef struct ninlil_join_authority {
    ninlil_join_peer *peers;
    uint16_t capacity;
    uint8_t authority[16];
    ninlil_join_commit_fn commit;
    void *commit_ctx;
    ninlil_join_approve_fn approve;
    void *approve_ctx;
    uint8_t poisoned;
} ninlil_join_authority;

int ninlil_join_open(ninlil_join_authority *a, ninlil_join_peer *peers,
                     uint16_t capacity, const uint8_t authority[16],
                     ninlil_join_commit_fn commit, void *commit_ctx,
                     ninlil_join_approve_fn approve, void *approve_ctx);
int ninlil_join_restore(ninlil_join_authority *a,
                        const ninlil_join_record *record);
int ninlil_join_begin(ninlil_join_authority *a, const uint8_t identity[32],
                      uint64_t now_ms);
/* Transport owner calls only after fresh mutual EDHOC authentication, with
 * exactly the verified identity and exporter fingerprint. */
int ninlil_join_authenticated(ninlil_join_authority *a,
                              const uint8_t identity[32],
                              const uint8_t session[16], uint64_t now_ms);
int ninlil_join_prepare(ninlil_join_authority *a, const uint8_t identity[32],
                        uint64_t now_ms, ninlil_join_record *accept);
/* Endpoint must commit accept before returning it as JOIN_COMMIT. This callback
 * is independent from the Gateway authority log and fails closed on ambiguity.
 */
int ninlil_join_endpoint_commit(const ninlil_join_record *accept,
                                const ninlil_join_record *previous,
                                const uint8_t identity[32],
                                const uint8_t authority[16],
                                const uint8_t session[16],
                                ninlil_join_commit_fn commit, void *ctx,
                                ninlil_join_record *ack);
int ninlil_join_confirm(ninlil_join_authority *a, const ninlil_join_record *ack,
                        const uint8_t session[16], uint64_t now_ms);
int ninlil_join_resume(ninlil_join_authority *a,
                       const ninlil_join_record *saved,
                       const uint8_t session[16], uint64_t now_ms);
int ninlil_join_revoke(ninlil_join_authority *a, const uint8_t identity[32]);
void ninlil_join_disconnect(ninlil_join_authority *a,
                            const uint8_t identity[32]);
void ninlil_join_expire(ninlil_join_authority *a, uint64_t now_ms);
int ninlil_join_policy(void *ctx, uint16_t peer, ninlil_peer_policy *policy);
int ninlil_join_grant_valid(const ninlil_join_grant *grant);
size_t ninlil_join_encode(const ninlil_join_record *record, uint8_t *output,
                          size_t capacity);
int ninlil_join_decode(const uint8_t *input, size_t length,
                       ninlil_join_record *record);

typedef struct ninlil_join_endpoint {
    ninlil_join_record record;
    uint8_t identity[32];
    uint8_t authority[16];
    ninlil_join_commit_fn commit;
    void *commit_ctx;
    uint8_t persisted;
    uint8_t poisoned;
} ninlil_join_endpoint;
int ninlil_join_endpoint_open(ninlil_join_endpoint *e,
                              const uint8_t identity[32],
                              const uint8_t authority[16],
                              ninlil_join_commit_fn commit, void *ctx);
int ninlil_join_endpoint_restore(ninlil_join_endpoint *e,
                                 const ninlil_join_record *record);
int ninlil_join_endpoint_accept(ninlil_join_endpoint *e,
                                const ninlil_join_record *accept,
                                const uint8_t session[16],
                                ninlil_join_record *ack);

#endif
