#include "ninlil_diag.h"
#include "ninlil.h"

#include <string.h>

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

size_t ninlil_diag_encode(uint8_t *packet, const ninlil_diag_frame *frame)
{
    if (!packet || !frame || frame->payload_length > NINLIL_DIAG_MAX_PAYLOAD ||
        (frame->type != NINLIL_DIAG_PING && frame->type != NINLIL_DIAG_PONG))
        return 0u;
    packet[0] = 'N';
    packet[1] = 'R';
    packet[2] = 1u;
    packet[3] = frame->type;
    put_be32(packet + 4, frame->sequence);
    put_be16(packet + 8, frame->source);
    put_be16(packet + 10, frame->target);
    packet[12] = frame->payload_length;
    if (frame->payload_length > 0u)
        memcpy(packet + NINLIL_DIAG_HEADER,
               frame->payload,
               frame->payload_length);
    return NINLIL_DIAG_HEADER + frame->payload_length;
}

int ninlil_diag_decode(const uint8_t *packet,
                       size_t length,
                       ninlil_diag_frame *frame)
{
    uint8_t payload_length;
    if (!packet || !frame || length < NINLIL_DIAG_HEADER ||
        packet[0] != 'N' || packet[1] != 'R' || packet[2] != 1u ||
        (packet[3] != NINLIL_DIAG_PING && packet[3] != NINLIL_DIAG_PONG))
        return NINLIL_ERR_INVALID;
    payload_length = packet[12];
    if (payload_length > NINLIL_DIAG_MAX_PAYLOAD ||
        length != NINLIL_DIAG_HEADER + payload_length)
        return NINLIL_ERR_INVALID;
    memset(frame, 0, sizeof(*frame));
    frame->type = packet[3];
    frame->sequence = get_be32(packet + 4);
    frame->source = get_be16(packet + 8);
    frame->target = get_be16(packet + 10);
    frame->payload_length = payload_length;
    if (payload_length > 0u)
        memcpy(frame->payload, packet + NINLIL_DIAG_HEADER, payload_length);
    return NINLIL_OK;
}
