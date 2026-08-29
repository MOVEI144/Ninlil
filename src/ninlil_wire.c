#include "ninlil_wire.h"

#include <string.h>

#define DATA_DEADLINE_PRESENT 0x01u

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static void put_be64(uint8_t *data, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; index++) {
        data[7u - index] = (uint8_t)value;
        value >>= 8;
    }
}

static uint64_t get_be64(const uint8_t *data)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; index++)
        value = (value << 8) | data[index];
    return value;
}

static int id_valid(const uint8_t *bytes)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= bytes[index];
    return combined != 0u;
}

static int required_evidence_valid(uint8_t evidence)
{
    return evidence == NINLIL_EVIDENCE_REMOTE_STORED ||
           evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
}

static int receipt_valid(uint8_t status, uint8_t evidence)
{
    if (status == NINLIL_RECEIPT_EVIDENCE) {
        return evidence == NINLIL_EVIDENCE_GATEWAY_CUSTODY ||
               evidence == NINLIL_EVIDENCE_REMOTE_STORED ||
               evidence == NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    }
    return (status == NINLIL_RECEIPT_PERMANENT_REJECTION ||
            status == NINLIL_RECEIPT_EXPIRED) &&
           evidence == NINLIL_EVIDENCE_NONE;
}

size_t ninlil_wire_data_size(uint16_t payload_length)
{
    return (size_t)NINLIL_WIRE_DATA_HEADER + (size_t)payload_length;
}

int ninlil_wire_packet_type(const uint8_t *packet, size_t length, uint8_t *type)
{
    if (!packet || !type || length < 4u || packet[0] != 'N' ||
        packet[1] != 'L' || packet[2] != NINLIL_WIRE_VERSION ||
        (packet[3] != NINLIL_WIRE_DATA && packet[3] != NINLIL_WIRE_RECEIPT))
        return NINLIL_ERR_INVALID;
    *type = packet[3];
    return NINLIL_OK;
}

size_t ninlil_wire_encode_data(uint8_t *packet, uint16_t source,
                               const ninlil_submission *submission,
                               const ninlil_id *message_id,
                               const uint8_t *payload)
{
    packet[0] = 'N';
    packet[1] = 'L';
    packet[2] = NINLIL_WIRE_VERSION;
    packet[3] = NINLIL_WIRE_DATA;
    put_be16(packet + 4, source);
    put_be16(packet + 6, submission->target);
    put_be16(packet + 8, submission->service);
    memcpy(packet + 10, message_id->bytes, NINLIL_ID_BYTES);
    packet[26] = (uint8_t)submission->ownership;
    packet[27] = (uint8_t)submission->required_evidence;
    packet[28] = (uint8_t)submission->traffic_class;
    packet[29] = (uint8_t)(submission->absolute_deadline_ms == 0u
                               ? 0u
                               : DATA_DEADLINE_PRESENT);
    put_be64(packet + 30, submission->absolute_deadline_ms);
    put_be16(packet + 38, submission->payload_len);
    if (submission->payload_len > 0u)
        memcpy(packet + NINLIL_WIRE_DATA_HEADER, payload,
               submission->payload_len);
    return ninlil_wire_data_size(submission->payload_len);
}

int ninlil_wire_decode_data(const uint8_t *packet, size_t length,
                            ninlil_wire_data_view *view)
{
    ninlil_wire_data_view candidate;
    uint8_t type;
    uint8_t flags;

    if (!view || ninlil_wire_packet_type(packet, length, &type) != NINLIL_OK ||
        type != NINLIL_WIRE_DATA || length < NINLIL_WIRE_DATA_HEADER)
        return NINLIL_ERR_INVALID;
    memset(&candidate, 0, sizeof(candidate));
    candidate.source = get_be16(packet + 4);
    candidate.target = get_be16(packet + 6);
    candidate.service = get_be16(packet + 8);
    candidate.payload_length = get_be16(packet + 38);
    flags = packet[29];
    if (candidate.source == 0u || candidate.source == UINT16_MAX ||
        candidate.target == 0u || candidate.target == UINT16_MAX ||
        candidate.service < NINLIL_APPLICATION_SERVICE_MIN ||
        !id_valid(packet + 10) ||
        candidate.payload_length > NINLIL_MAX_PAYLOAD ||
        length != ninlil_wire_data_size(candidate.payload_length) ||
        packet[26] != NINLIL_OWNERSHIP_DURABLE ||
        !required_evidence_valid(packet[27]) ||
        packet[28] > NINLIL_TRAFFIC_BULK ||
        (flags & (uint8_t)~DATA_DEADLINE_PRESENT) != 0u)
        return NINLIL_ERR_INVALID;
    candidate.absolute_deadline_ms = get_be64(packet + 30);
    if (((flags & DATA_DEADLINE_PRESENT) == 0u) !=
        (candidate.absolute_deadline_ms == 0u))
        return NINLIL_ERR_INVALID;
    memcpy(candidate.message_id.bytes, packet + 10, NINLIL_ID_BYTES);
    candidate.ownership = (ninlil_ownership)packet[26];
    candidate.required_evidence = (ninlil_evidence)packet[27];
    candidate.traffic_class = (ninlil_traffic_class)packet[28];
    candidate.payload = packet + NINLIL_WIRE_DATA_HEADER;
    *view = candidate;
    return NINLIL_OK;
}

size_t ninlil_wire_encode_receipt(uint8_t *packet, uint16_t source,
                                  uint16_t target, const ninlil_id *message_id,
                                  uint8_t status, ninlil_evidence evidence)
{
    packet[0] = 'N';
    packet[1] = 'L';
    packet[2] = NINLIL_WIRE_VERSION;
    packet[3] = NINLIL_WIRE_RECEIPT;
    put_be16(packet + 4, source);
    put_be16(packet + 6, target);
    memcpy(packet + 8, message_id->bytes, NINLIL_ID_BYTES);
    packet[24] = status;
    packet[25] = (uint8_t)evidence;
    return NINLIL_WIRE_RECEIPT_SIZE;
}

int ninlil_wire_decode_receipt(const uint8_t *packet, size_t length,
                               ninlil_wire_receipt_view *view)
{
    ninlil_wire_receipt_view candidate;
    uint8_t type;

    if (!view || ninlil_wire_packet_type(packet, length, &type) != NINLIL_OK ||
        type != NINLIL_WIRE_RECEIPT || length != NINLIL_WIRE_RECEIPT_SIZE ||
        !receipt_valid(packet[24], packet[25]))
        return NINLIL_ERR_INVALID;
    memset(&candidate, 0, sizeof(candidate));
    candidate.source = get_be16(packet + 4);
    candidate.target = get_be16(packet + 6);
    if (candidate.source == 0u || candidate.source == UINT16_MAX ||
        candidate.target == 0u || candidate.target == UINT16_MAX ||
        !id_valid(packet + 8))
        return NINLIL_ERR_INVALID;
    memcpy(candidate.message_id.bytes, packet + 8, NINLIL_ID_BYTES);
    candidate.status = packet[24];
    candidate.evidence = (ninlil_evidence)packet[25];
    *view = candidate;
    return NINLIL_OK;
}
