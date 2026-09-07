#include "hil_delivery.h"

#include <string.h>

#define HIL_SERVICE UINT16_C(0x0100)

static void put_be32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | (uint32_t)input[3];
}

static int valid_campaign(const ninlil_hil_campaign *campaign)
{
    return campaign && campaign->campaign != 0u && campaign->node != 0u &&
           campaign->node != UINT16_MAX && campaign->peer != 0u &&
           campaign->peer != UINT16_MAX && campaign->node != campaign->peer &&
           campaign->count >= 1u && campaign->count <= 100u;
}

int ninlil_hil_request(const ninlil_hil_campaign *campaign, uint32_t sequence,
                       ninlil_submission *request,
                       uint8_t payload[NINLIL_HIL_PAYLOAD_SIZE])
{
    if (!valid_campaign(campaign) || !request || !payload || sequence == 0u ||
        sequence > campaign->count)
        return NINLIL_ERR_INVALID;
    put_be32(payload, campaign->campaign);
    payload[4] = (uint8_t)(campaign->node >> 8);
    payload[5] = (uint8_t)campaign->node;
    payload[6] = (uint8_t)(campaign->peer >> 8);
    payload[7] = (uint8_t)campaign->peer;
    put_be32(payload + 8, sequence);
    ninlil_submission_defaults(request);
    memcpy(request->idempotency_key.bytes, "HIL2", 4u);
    memcpy(request->idempotency_key.bytes + 4, payload,
           NINLIL_HIL_PAYLOAD_SIZE);
    request->target = campaign->peer;
    request->service = HIL_SERVICE;
    request->ownership = NINLIL_OWNERSHIP_DURABLE;
    request->required_evidence = NINLIL_EVIDENCE_REMOTE_STORED;
    request->payload = payload;
    request->payload_len = NINLIL_HIL_PAYLOAD_SIZE;
    return NINLIL_OK;
}

int ninlil_hil_inbound(const ninlil_hil_campaign *campaign,
                       const ninlil_inbound *inbound, uint32_t *sequence)
{
    uint32_t value;

    if (!valid_campaign(campaign) || !inbound || !sequence ||
        inbound->source != campaign->peer || inbound->service != HIL_SERVICE ||
        inbound->ownership != NINLIL_OWNERSHIP_DURABLE ||
        inbound->required_evidence != NINLIL_EVIDENCE_REMOTE_STORED ||
        inbound->traffic_class != NINLIL_TRAFFIC_NORMAL ||
        inbound->absolute_deadline_ms != 0u ||
        inbound->payload_len != NINLIL_HIL_PAYLOAD_SIZE ||
        get_be32(inbound->payload) != campaign->campaign ||
        inbound->payload[4] != (uint8_t)(campaign->peer >> 8) ||
        inbound->payload[5] != (uint8_t)campaign->peer ||
        inbound->payload[6] != (uint8_t)(campaign->node >> 8) ||
        inbound->payload[7] != (uint8_t)campaign->node)
        return NINLIL_ERR_INVALID;
    value = get_be32(inbound->payload + 8);
    if (value == 0u || value > campaign->count)
        return NINLIL_ERR_INVALID;
    *sequence = value;
    return NINLIL_OK;
}
