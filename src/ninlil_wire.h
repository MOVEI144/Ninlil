#ifndef NINLIL_WIRE_H
#define NINLIL_WIRE_H

#include "ninlil.h"

#define NINLIL_WIRE_DATA 1u
#define NINLIL_WIRE_RECEIPT 2u
#define NINLIL_WIRE_DATA_HEADER 28u
#define NINLIL_WIRE_RECEIPT_SIZE 25u
#define NINLIL_WIRE_DATA_MAX (NINLIL_WIRE_DATA_HEADER + NINLIL_MAX_PAYLOAD)
#define NINLIL_WIRE_PACKET_MAX NINLIL_WIRE_DATA_MAX

typedef struct ninlil_wire_data_view {
    uint16_t source;
    uint16_t target;
    uint16_t service;
    ninlil_id message_id;
    const uint8_t *payload;
    uint16_t payload_length;
} ninlil_wire_data_view;

typedef struct ninlil_wire_receipt_view {
    uint16_t source;
    uint16_t target;
    ninlil_id message_id;
    ninlil_progress progress;
} ninlil_wire_receipt_view;

size_t ninlil_wire_data_size(uint16_t payload_length);
int ninlil_wire_packet_type(const uint8_t *packet, size_t length,
                            uint8_t *type);
size_t ninlil_wire_encode_data(uint8_t *packet, uint16_t source,
                               uint16_t target, uint16_t service,
                               const ninlil_id *message_id,
                               const uint8_t *payload, uint16_t payload_length);
int ninlil_wire_decode_data(const uint8_t *packet, size_t length,
                            ninlil_wire_data_view *view);
size_t ninlil_wire_encode_receipt(uint8_t *packet, uint16_t source,
                                  uint16_t target, const ninlil_id *message_id,
                                  ninlil_progress progress);
int ninlil_wire_decode_receipt(const uint8_t *packet, size_t length,
                               ninlil_wire_receipt_view *view);

#endif
