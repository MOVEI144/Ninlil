#ifndef LAB_CRYPTO_H
#define LAB_CRYPTO_H
#include "ninlil_edhoc.h"
#define LAB_NODES 4u
/* Ephemeral lab trust store. Never deploy these host fixtures as a provider. */
typedef struct lab_identity {
    uint8_t private_key[32];
    uint8_t public_key[65];
    uint8_t identity[32];
    uint16_t node;
} lab_identity;
int lab_identity_create(lab_identity *id, uint16_t node);
int lab_handshake(lab_identity *a, lab_identity *b,
                  ninlil_session_material *e2e, ninlil_session_material *hop);
#endif
