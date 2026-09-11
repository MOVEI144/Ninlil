#include "node_example.h"
#include <string.h>

/* Example application transaction ledger, separate from transport custody.
 * It demonstrates durable APPLICATION_ACCEPTED, never physical execution. */
static ninlil_journal *ledger;
static ninlil_journal_ref entries[NODE_EXAMPLE_APP_MAX];
static ninlil_id ids[NODE_EXAMPLE_APP_MAX];
static uint16_t count;
static uint8_t bound;
static int replay(void *ctx, uint8_t type, const uint8_t *data, uint16_t size,
                  const ninlil_journal_ref *ref)
{
    (void)ctx;
    if (type == 1u && !bound && !count && size == 32u &&
        memcmp(data, node_identity.identity, 32u) == 0) {
        bound = 1u;
        return NINLIL_OK;
    }
    if (!bound || type != 2u || size != 38u || count == NODE_EXAMPLE_APP_MAX)
        return NINLIL_ERR_CORRUPT;
    if ((!data[16] && !data[17]) || (data[16] == 255u && data[17] == 255u) ||
        data[18] == 0u || data[20] != 0u || data[21] > 64u)
        return NINLIL_ERR_CORRUPT;
    for (unsigned int i = 0u; i < count; i++)
        if (memcmp(ids[i].bytes, data, 16u) == 0)
            return NINLIL_ERR_CORRUPT;
    memcpy(ids[count].bytes, data, 16u);
    entries[count++] = *ref;
    return NINLIL_OK;
}
void node_application_close(void)
{
    ninlil_journal_close(ledger);
    ledger = NULL;
    count = 0u;
    bound = 0u;
}
int node_application_open(const char *location, int provision)
{
    int rc;
    node_application_close();
    rc = ninlil_journal_open(&ledger, location, 0x20000u, replay, NULL);
    if (rc == NINLIL_OK && !bound) {
        if (!provision)
            rc = NINLIL_ERR_CORRUPT;
        else {
            rc = ninlil_journal_append(ledger, 1u, node_identity.identity, 32u,
                                       NULL);
            if (rc == NINLIL_OK)
                bound = 1u;
        }
    }
    if (rc != NINLIL_OK)
        node_application_close();
    return rc;
}
uint16_t node_application_count(void)
{
    return count;
}

int node_application_step(ninlil_runtime *core)
{
    ninlil_inbound message;
    ninlil_journal_ref ref;
    uint8_t record[38], stored[38];
    unsigned int i;
    int rc;
    if (!ledger || !bound)
        return NINLIL_ERR_STATE;
    rc = ninlil_receive_class(core, 256u, NINLIL_TRAFFIC_NORMAL, &message);
    if (rc != NINLIL_OK)
        return rc == NINLIL_ERR_EMPTY ? NINLIL_OK : rc;
    memcpy(record, message.message_id.bytes, 16u);
    record[16] = (uint8_t)(message.source >> 8);
    record[17] = (uint8_t)message.source;
    record[18] = (uint8_t)(message.service >> 8);
    record[19] = (uint8_t)message.service;
    record[20] = (uint8_t)(message.payload_len >> 8);
    record[21] = (uint8_t)message.payload_len;
    rc = ninlil_psa_packet_digest(message.payload, message.payload_len,
                                  record + 22);
    if (rc != NINLIL_OK)
        return rc;
    for (i = 0u; i < count; i++)
        if (memcmp(ids[i].bytes, record, 16u) == 0)
            break;
    if (i == count) {
        if (count == NODE_EXAMPLE_APP_MAX)
            return NINLIL_ERR_CAPACITY;
        rc = ninlil_journal_append(ledger, 2u, record, sizeof(record), &ref);
        if (rc == NINLIL_OK)
            rc = replay(NULL, 2u, record, sizeof(record), &ref);
        if (rc != NINLIL_OK)
            return rc;
    }
    rc = ninlil_journal_read(ledger, &entries[i], 0u, stored, sizeof(stored));
    if (rc != NINLIL_OK || memcmp(record, stored, sizeof(stored)) != 0)
        return rc != NINLIL_OK ? rc : NINLIL_ERR_CORRUPT;
    return ninlil_application_accept(core, &message.message_id);
}
int node_application_submit(ninlil_runtime *core, uint16_t target,
                            uint32_t sequence, ninlil_id *id)
{
    ninlil_submission request;
    uint8_t payload[64] = {0};
    if (!sequence)
        return NINLIL_ERR_INVALID;
    for (unsigned int i = 0u; i < 4u; i++)
        payload[i] = (uint8_t)(sequence >> (24u - i * 8u));
    for (unsigned int i = 4u; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(sequence + i);
    ninlil_submission_defaults(&request);
    memcpy(request.idempotency_key.bytes, "node-example", 12u);
    memcpy(request.idempotency_key.bytes + 12, payload, 4u);
    request.target = target;
    request.service = 256u;
    request.payload = payload;
    request.payload_len = sizeof(payload);
    request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    return ninlil_submit(core, &request, id);
}
