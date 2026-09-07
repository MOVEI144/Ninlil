#ifndef NINLIL_CONTROL_FRAGMENT_H
#define NINLIL_CONTROL_FRAGMENT_H

#include "ninlil.h"

#define NINLIL_CONTROL_MESSAGE_MAX 1024u
#define NINLIL_CONTROL_FRAGMENT_HEADER 16u
#define NINLIL_CONTROL_FRAGMENT_BODY 224u
#define NINLIL_CONTROL_FRAGMENT_MAX 5u

typedef struct ninlil_control_reassembly {
    uint8_t bytes[NINLIL_CONTROL_MESSAGE_MAX];
    uint64_t exchange;
    uint64_t began_ms;
    uint16_t length;
    uint8_t kind;
    uint8_t count;
    uint8_t received;
    uint8_t poisoned;
} ninlil_control_reassembly;

/* One caller-owned reassembly per admitted provisional peer. No allocation,
 * no pre-authentication Flash writes. Exchange ID is a random nonzero transport
 * token, not authentication. Kind 1..4 denotes EDHOC message number. */
size_t ninlil_control_fragment(uint64_t exchange, uint8_t kind,
                               const uint8_t *message, size_t length,
                               uint8_t index, uint8_t *frame, size_t capacity);
int ninlil_control_reassemble(ninlil_control_reassembly *state,
                              const uint8_t *frame, size_t length,
                              uint64_t now_ms, uint8_t *message,
                              size_t capacity, size_t *written);
void ninlil_control_reassembly_clear(ninlil_control_reassembly *state);

#endif
