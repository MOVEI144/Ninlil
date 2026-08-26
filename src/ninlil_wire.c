#include "ninlil_wire.h"

#include <string.h>

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

size_t ninlil_wire_data_size(uint16_t payload_length)
{
    return (size_t)NINLIL_WIRE_DATA_HEADER + (size_t)payload_length;
}

int ninlil_wire_packet_type(const uint8_t *packet, size_t length, uint8_t *type)
{
    if (!packet || !type || length < 4u || packet[0] != 'N' ||
        packet[1] != 'L' || packet[2] != 1u)
        return NINLIL_ERR_INVALID;
    *type = packet[3];
    return NINLIL_OK;
}

size_t ninlil_wire_encode_data(uint8_t *packet, uint16_t source,
                               uint16_t target, uint16_t service,
                               const ninlil_id *message_id,
                               const uint8_t *payload, uint16_t payload_length)
{
    packet[0] = 'N';
    packet[1] = 'L';
    packet[2] = 1u;
    packet[3] = NINLIL_WIRE_DATA;
    put_be16(packet + 4, source);
    put_be16(packet + 6, target);
    put_be16(packet + 8, service);
    memcpy(packet + 10, message_id->bytes, NINLIL_ID_BYTES);
    put_be16(packet + 26, payload_length);
    if (payload_length > 0u)
        memcpy(packet + NINLIL_WIRE_DATA_HEADER, payload, payload_length);
    return ninlil_wire_data_size(payload_length);
}

int ninlil_wire_decode_data(const uint8_t *packet, size_t length,
                            ninlil_wire_data_view *view)
{
    uint8_t type;
    uint16_t payload_length;

    if (!view || ninlil_wire_packet_type(packet, length, &type) != NINLIL_OK ||
        type != NINLIL_WIRE_DATA || length < NINLIL_WIRE_DATA_HEADER)
        return NINLIL_ERR_INVALID;
    payload_length = get_be16(packet + 26);
    if (payload_length > NINLIL_MAX_PAYLOAD ||
        length != ninlil_wire_data_size(payload_length))
        return NINLIL_ERR_INVALID;
    view->source = get_be16(packet + 4);
    view->target = get_be16(packet + 6);
    view->service = get_be16(packet + 8);
    memcpy(view->message_id.bytes, packet + 10, NINLIL_ID_BYTES);
    view->payload = packet + NINLIL_WIRE_DATA_HEADER;
    view->payload_length = payload_length;
    return NINLIL_OK;
}

size_t ninlil_wire_encode_receipt(uint8_t *packet, uint16_t source,
                                  uint16_t target, const ninlil_id *message_id,
                                  ninlil_progress progress)
{
    packet[0] = 'N';
    packet[1] = 'L';
    packet[2] = 1u;
    packet[3] = NINLIL_WIRE_RECEIPT;
    put_be16(packet + 4, source);
    put_be16(packet + 6, target);
    memcpy(packet + 8, message_id->bytes, NINLIL_ID_BYTES);
    packet[24] = (uint8_t)progress;
    return NINLIL_WIRE_RECEIPT_SIZE;
}

int ninlil_wire_decode_receipt(const uint8_t *packet, size_t length,
                               ninlil_wire_receipt_view *view)
{
    uint8_t type;

    if (!view || ninlil_wire_packet_type(packet, length, &type) != NINLIL_OK ||
        type != NINLIL_WIRE_RECEIPT || length != NINLIL_WIRE_RECEIPT_SIZE)
        return NINLIL_ERR_INVALID;
    view->source = get_be16(packet + 4);
    view->target = get_be16(packet + 6);
    memcpy(view->message_id.bytes, packet + 8, NINLIL_ID_BYTES);
    view->progress = (ninlil_progress)packet[24];
    return NINLIL_OK;
}
