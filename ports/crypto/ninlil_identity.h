#ifndef NINLIL_IDENTITY_H
#define NINLIL_IDENTITY_H

#include "ninlil_edhoc.h"
#include <psa/crypto.h>

#define NINLIL_IDENTITY_RECORD_SIZE 108u

/* This boundary contains a private signing key. One exclusive owner, private
 * storage, durable atomic replacement. Read returns EMPTY only for a genuinely
 * unprovisioned store; corruption/IO must never become EMPTY. No logs/backups
 * of this buffer. Physical secret protection belongs to the storage port. */
typedef struct ninlil_identity_io {
    int (*read)(void *ctx, uint8_t record[NINLIL_IDENTITY_RECORD_SIZE]);
    int (*commit)(void *ctx, const uint8_t record[NINLIL_IDENTITY_RECORD_SIZE]);
    void *ctx;
} ninlil_identity_io;

typedef struct ninlil_identity {
    ninlil_identity_io io;
    psa_key_id_t signing_key;
    uint64_t generation;
    uint8_t public_key[65];
    uint8_t fingerprint[32];
    uint8_t identity[32]; /* Stable across key rotation. */
    uint8_t initialized;  /* Irreversible Core/control provisioning marker. */
} ninlil_identity;

typedef struct ninlil_identity_peer {
    const ninlil_identity *local;
    uint8_t public_key[65];
    uint8_t identity[32];
    uint16_t local_node;
    uint16_t peer_node;
    uint8_t verified;
} ninlil_identity_peer;

/* Initialize PSA Crypto first. Open/provision outputs must be closed or unused;
 * close each successful open. EDHOC borrows the handle until handshake close.
 */
int ninlil_identity_open(ninlil_identity *identity, ninlil_identity_io io);
/* Explicit provisioning only; refuses an existing or corrupt store. */
int ninlil_identity_provision(ninlil_identity *identity, ninlil_identity_io io);
/* Close all EDHOC/live sessions before rotation. Requires the current
 * generation and exact matching stored identity. Reprovision public-key trust
 * and advance membership/binding epochs separately. Ambiguous commit closes the
 * old key. */
int ninlil_identity_rotate(ninlil_identity *identity,
                           uint64_t expected_generation);
void ninlil_identity_close(ninlil_identity *identity);
/* Commit once after both bound stores exist, before any RF. Normal startup
 * and repeated provisioning must then refuse missing stores. */
int ninlil_identity_mark_initialized(ninlil_identity *identity);
/* The supplied peer key must come from explicit trusted provisioning, never an
 * unauthenticated radio announcement. One peer context per handshake. */
int ninlil_identity_credentials(ninlil_identity_peer *peer,
                                const ninlil_identity *local,
                                uint16_t local_node, uint16_t peer_node,
                                const uint8_t trusted_key[65],
                                const uint8_t trusted_identity[32],
                                int initiator, ninlil_edhoc_config *config);

#endif
