#ifndef NINLIL_EDHOC_H
#define NINLIL_EDHOC_H

#include "ninlil_secure.h"
#include <edhoc.h>

#define NINLIL_EDHOC_MESSAGE_MAX 1024u
#define NINLIL_EDHOC_DEADLINE_MS 30000u
#define NINLIL_EDHOC_RETRY_MAX 8u

typedef struct ninlil_edhoc_config {
    struct edhoc_credentials credentials;
    void *credential_ctx;
    /* Called after verification lookup; returns its stable authenticated
     * identity. Lookup must reject expired/untrusted credentials itself. */
    int (*peer_identity)(void *ctx, uint8_t identity[32]);
    uint8_t expected_peer[32];
    uint8_t initiator;
    int8_t connection_id;
} ninlil_edhoc_config;

typedef struct ninlil_edhoc {
    struct edhoc_context context;
    ninlil_edhoc_config config;
    ninlil_session_material material;
    ninlil_session_material hop_material;
    uint8_t peer_identity[32];
    uint8_t last_input[NINLIL_EDHOC_MESSAGE_MAX];
    uint8_t last_output[NINLIL_EDHOC_MESSAGE_MAX];
    size_t input_length;
    size_t output_length;
    uint64_t started_ms;
    uint8_t step;
    uint8_t retries;
    uint8_t authenticated;
    uint8_t opened;
    int upstream_error;
} ninlil_edhoc;

/* Suite 2, method 0 only, including message 4 confirmation. The wrapper uses a
 * shared bounded allocator protected by a try-lock; concurrent calls return
 * BUSY, never block. Credential callbacks must not call libedhoc directly.
 * Retries return the exact cached message, never rerun ephemeral generation.
 * All received messages have a 1024-byte bound and a 30-second boot-local age.
 * Successful authentication does not activate membership. */
int ninlil_edhoc_open(ninlil_edhoc *h, const ninlil_edhoc_config *config,
                      uint64_t now_ms);
int ninlil_edhoc_exchange(ninlil_edhoc *h, const uint8_t *input, size_t length,
                          uint64_t now_ms, uint8_t *output, size_t capacity,
                          size_t *written);
int ninlil_edhoc_material(const ninlil_edhoc *h,
                          ninlil_session_material *material,
                          uint8_t peer_identity[32]);
int ninlil_edhoc_hop_material(const ninlil_edhoc *h,
                              ninlil_session_material *material);
void ninlil_edhoc_close(ninlil_edhoc *h);

#endif
