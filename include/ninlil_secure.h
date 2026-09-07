#ifndef NINLIL_SECURE_H
#define NINLIL_SECURE_H

#include "ninlil_security_state.h"

#define NINLIL_SECURE_HEADER 32u
#define NINLIL_SECURE_TAG 8u
#define NINLIL_SECURE_OVERHEAD (NINLIL_SECURE_HEADER + NINLIL_SECURE_TAG)
#define NINLIL_SECURE_PLAINTEXT_MAX 200u
#define NINLIL_SECURE_FRAME_MAX 240u

typedef struct ninlil_session_material {
    uint8_t fingerprint[16];
    uint8_t keys[2][16];
    uint8_t ivs[2][13];
} ninlil_session_material;

/* Implementations must not log keys or unauthenticated plaintext. */
typedef struct ninlil_aead {
    int (*crypt)(void *ctx, int encrypt, const uint8_t key[16],
                 const uint8_t nonce[13], const uint8_t *aad, size_t aad_len,
                 const uint8_t *input, size_t input_len, uint8_t *output,
                 size_t capacity, size_t *length);
    void *ctx;
} ninlil_aead;

typedef struct ninlil_secure_session {
    ninlil_session_material material;
    ninlil_counter_store *counter;
    ninlil_aead aead;
    uint64_t local_membership_epoch;
    uint64_t peer_membership_epoch;
    uint64_t rx_high;
    uint64_t rx_bitmap;
    uint16_t local;
    uint16_t peer;
    uint8_t direction;
    uint8_t ready;
} ninlil_secure_session;

/* Only fresh mutually authenticated material may enter this boundary. Never
 * persist this struct or reopen a receive window with old material after boot.
 * Counter storage must already be bound to this fingerprint and TX direction.
 * Membership authorization is separate; revoke closes the live session. */
int ninlil_secure_open(ninlil_secure_session *session,
                       const ninlil_session_material *material,
                       ninlil_counter_store *counter, ninlil_aead aead,
                       uint16_t local, uint16_t peer, uint8_t direction);
/* Bind once after Join commits. Epoch changes require fresh EDHOC/open. */
int ninlil_secure_bind_membership(ninlil_secure_session *session,
                                  uint64_t local_epoch, uint64_t peer_epoch);
void ninlil_secure_close(ninlil_secure_session *session);
void ninlil_secret_clear(void *data, size_t length);
int ninlil_secure_seal(ninlil_secure_session *session, const uint8_t *plain,
                       size_t length, uint8_t *frame, size_t capacity,
                       size_t *written);
int ninlil_secure_unseal(ninlil_secure_session *session, const uint8_t *frame,
                         size_t length, uint8_t *plain, size_t capacity,
                         size_t *written);
/* Control channel 1 is authenticated under the same ordered session counter
 * but separated from data channel 0 by AEAD-associated header byte 31.
 * The control owner gates Join/plan operations by verified identity and state.
 */
int ninlil_secure_seal_control(ninlil_secure_session *session,
                               const uint8_t *plain, size_t length,
                               uint8_t *frame, size_t capacity,
                               size_t *written);
int ninlil_secure_unseal_control(ninlil_secure_session *session,
                                 const uint8_t *frame, size_t length,
                                 uint8_t *plain, size_t capacity,
                                 size_t *written);
/* PSA Crypto AES-128-CCM, 8-byte tag. Caller initializes the PSA subsystem. */
ninlil_aead ninlil_psa_aead(void);

#endif
