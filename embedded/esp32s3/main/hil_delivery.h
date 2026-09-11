#ifndef NINLIL_HIL_DELIVERY_H
#define NINLIL_HIL_DELIVERY_H

#include "ninlil.h"

#define NINLIL_HIL_PAYLOAD_SIZE 12u

typedef struct ninlil_hil_campaign {
    uint32_t campaign;
    uint16_t node;
    uint16_t peer;
    uint32_t count;
} ninlil_hil_campaign;

/* Test harness only. Outputs stay unchanged on invalid input. */
int ninlil_hil_request(const ninlil_hil_campaign *campaign, uint32_t sequence,
                       ninlil_submission *request,
                       uint8_t payload[NINLIL_HIL_PAYLOAD_SIZE]);
int ninlil_hil_inbound(const ninlil_hil_campaign *campaign,
                       const ninlil_inbound *inbound, uint32_t *sequence);

#endif
