#ifndef NINLIL_DIAG_H
#define NINLIL_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define NINLIL_DIAG_MAX_PAYLOAD 32u
#define NINLIL_DIAG_HEADER 13u
#define NINLIL_DIAG_MAX (NINLIL_DIAG_HEADER + NINLIL_DIAG_MAX_PAYLOAD)
#define NINLIL_DIAG_PING 1u
#define NINLIL_DIAG_PONG 2u

typedef struct ninlil_diag_frame {
    uint8_t type;
    uint32_t sequence;
    uint16_t source;
    uint16_t target;
    uint8_t payload_length;
    uint8_t payload[NINLIL_DIAG_MAX_PAYLOAD];
} ninlil_diag_frame;

size_t ninlil_diag_encode(uint8_t *packet, const ninlil_diag_frame *frame);
int ninlil_diag_decode(const uint8_t *packet, size_t length,
                       ninlil_diag_frame *frame);

#endif
