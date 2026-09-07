#include "bench_relay.h"
#include "ninlil_routed.h"
#include "secure_bench.h"
#include <string.h>

static ninlil_relay relay;
static ninlil_relay_slot slots[8];
static ninlil_control_log **journal;

/* Explicit USB-provisioned static test grant, not production Join policy.
 * Fresh independently authenticated hop keys are required after every boot. */
static int policy(void *ctx, uint16_t peer, ninlil_peer_policy *out)
{
    ninlil_secure_session *s = ninlil_bench_session(peer, 1u);
    (void)ctx;
    if ((peer != 1u && peer != 3u) || !s || !s->ready)
        return NINLIL_ERR_UNAUTHORIZED;
    memset(out, 0, sizeof(*out));
    out->membership_epoch = out->session_membership_epoch = 1u;
    return NINLIL_OK;
}

static int route(void *ctx, const ninlil_network_path *p, uint64_t epoch,
                 uint64_t now)
{
    (void)ctx;
    (void)now;
    if (epoch != 1u || p->count != 3u || p->nodes[1] != 2u ||
        !((p->nodes[0] == 1u && p->nodes[2] == 3u) ||
          (p->nodes[0] == 3u && p->nodes[2] == 1u)))
        return NINLIL_ERR_UNAUTHORIZED;
    return NINLIL_OK;
}

static int commit(void *ctx, const ninlil_relay_record *record)
{
    (void)ctx;
    return ninlil_control_log_relay(*journal, record);
}

static int verify(void *ctx, const ninlil_relay_record *record)
{
    (void)ctx;
    return ninlil_control_log_verify_relay(*journal, record);
}

int ninlil_bench_relay_open(ninlil_control_log **log)
{
    journal = log;
    return ninlil_relay_open(&relay, slots, 8u, CONFIG_NINLIL_NODE_ID,
                             NINLIL_ROLE_POWERED_ENDPOINT,
                             NINLIL_CAP_RELAY_CUSTODY, 100u, policy, NULL,
                             commit, verify, NULL, route, NULL);
}

int ninlil_bench_relay_restore(void *ctx, const ninlil_relay_record *record)
{
    (void)ctx;
    return ninlil_relay_restore(&relay, record);
}

static int wrap(uint16_t peer, const ninlil_relay_record *record,
                uint8_t *output, size_t capacity, size_t *written)
{
    uint8_t bytes[NINLIL_RELAY_FRAME_MAX];
    size_t length = ninlil_relay_encode(record, bytes, sizeof(bytes));
    ninlil_secure_session *s = ninlil_bench_session(peer, 1u);
    if (!length || !s || !s->ready)
        return NINLIL_ERR_UNAUTHORIZED;
    return ninlil_secure_seal(s, bytes, length, output, capacity, written);
}

static int ingress(char command, const uint8_t *input, size_t length,
                   uint64_t now, uint8_t *output, size_t capacity,
                   size_t *written)
{
    ninlil_relay_record record;
    ninlil_secure_session *s;
    uint8_t bytes[NINLIL_RELAY_FRAME_MAX], id[16];
    uint16_t sender;
    size_t decoded;
    int rc;
    if (length < NINLIL_SECURE_OVERHEAD || length > 240u)
        return NINLIL_ERR_INVALID;
    sender = (uint16_t)(((uint16_t)input[4] << 8) | input[5]);
    s = ninlil_bench_session(sender, 1u);
    if (!s)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = ninlil_secure_unseal(s, input, length, bytes, sizeof(bytes), &decoded);
    if (rc == NINLIL_OK)
        rc = ninlil_relay_decode(bytes, decoded, &record);
    if (rc != NINLIL_OK)
        return rc;
    if (command == 'k') {
        if (record.done != 1u || record.control)
            return NINLIL_ERR_INVALID;
        return ninlil_relay_ack(&relay, sender, record.packet_id,
                                record.route_epoch);
    }
    rc = ninlil_psa_packet_digest(record.ciphertext, record.length, id);
    if (rc != NINLIL_OK || memcmp(id, record.packet_id, 16u) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = ninlil_relay_receive(&relay, sender, &record, now);
    if (rc != NINLIL_OK)
        return rc;
    record.done = 1u;
    /* This actual hop ACK is emitted only after Flash commit and verification.
     */
    return wrap(sender, &record, output, capacity, written);
}

int ninlil_bench_relay_command(char command, const uint8_t *input,
                               size_t length, uint64_t now, uint8_t *output,
                               size_t capacity, size_t *written)
{
    ninlil_relay_record record;
    uint16_t next;
    size_t i;
    int rc;
    if (CONFIG_NINLIL_NODE_ID != 2 || !journal || !*journal)
        return NINLIL_ERR_UNAUTHORIZED;
    if (command == 'j' || command == 'k')
        return ingress(command, input, length, now, output, capacity, written);
    if (command == 'd' && length == 1u && input[0] <= 1u)
        return ninlil_relay_drain(&relay, input[0]);
    if (length != 0u)
        return NINLIL_ERR_INVALID;
    if (command == 'r')
        return ninlil_relay_ready_remove(&relay) ? NINLIL_OK : NINLIL_ERR_BUSY;
    if (command == 'n') {
        rc = ninlil_relay_next(&relay, now, &next, &record);
        return rc == NINLIL_OK ? wrap(next, &record, output, capacity, written)
                               : rc;
    }
    if (command == 'q') {
        for (i = 0u; i < 8u; i++) {
            if (!slots[i].used)
                continue;
            rc = verify(NULL, &slots[i].record);
            if (rc != NINLIL_OK)
                return rc;
            *written = ninlil_relay_encode(&slots[i].record, output, capacity);
            return *written ? NINLIL_OK : NINLIL_ERR_CAPACITY;
        }
        return NINLIL_ERR_EMPTY;
    }
    return NINLIL_ERR_INVALID;
}
