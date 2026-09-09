#include "ninlil_bulk.h"
#include "node_example.h"
#include <string.h>
static ninlil_bulk *bulk;
static int last_result;
static uint32_t get(const uint8_t *p, unsigned int size)
{
    uint32_t n = 0u;
    for (unsigned int i = 0u; i < size; i++)
        n = (n << 8) | p[i];
    return n;
}
static void put(uint8_t *p, uint32_t n, unsigned int size)
{
    for (unsigned int i = 0u; i < size; i++)
        p[size - i - 1u] = (uint8_t)(n >> (8u * i));
}
int node_bulk_command(char command, const uint8_t *data, size_t size,
                      uint8_t *out, size_t *written)
{
    if (command == 'O' && size == 3u && !bulk && data[2] <= 1u &&
        get(data, 2u) != CONFIG_NINLIL_NODE_ID)
        return ninlil_bulk_open(&bulk, "node_bulk", (uint16_t)get(data, 2u),
                                256u, data[2]);
    if (!bulk)
        return NINLIL_ERR_STATE;
    if (command == 'M' && size == 52u) {
        ninlil_bulk_manifest m;
        memcpy(m.id.bytes, data, 16u);
        m.length = get(data + 16, 4u);
        memcpy(m.sha256, data + 20, 32u);
        return ninlil_bulk_begin(bulk, &m);
    }
    if (command == 'W' && size > 4u && size <= 44u)
        return ninlil_bulk_write(bulk, get(data, 4u), data + 4,
                                 (uint16_t)(size - 4u));
    if (command == 'V' && !size)
        return ninlil_bulk_seal(bulk);
    if (command == 'J' && !size) {
        ninlil_bulk_status s;
        int rc = ninlil_bulk_query(bulk, &s);
        if (rc != NINLIL_OK)
            return rc;
        memcpy(out, s.manifest.id.bytes, 16u);
        put(out + 16, s.manifest.length, 4u);
        memcpy(out + 20, s.manifest.sha256, 32u);
        put(out + 52, s.stored_bytes, 4u);
        put(out + 56, s.acknowledged_frames, 2u);
        out[58] = s.sending;
        out[59] = s.ready;
        out[60] = s.remote_stored;
        put(out + 61, (uint32_t)last_result, 4u);
        *written = 65u;
        return NINLIL_OK;
    }
    if (command == 'Y' && size == 6u && get(data + 4, 2u) <= 120u) {
        int rc = ninlil_bulk_read(bulk, get(data, 4u), out,
                                  (uint16_t)get(data + 4, 2u));
        if (rc == NINLIL_OK)
            *written = get(data + 4, 2u);
        return rc;
    }
    if (command == 'C' && !size)
        return ninlil_bulk_collect(bulk);
    return NINLIL_ERR_INVALID;
}
int node_bulk_step(ninlil_runtime *core)
{
    int rc = bulk ? ninlil_bulk_step(bulk, core) : NINLIL_OK;
    last_result = rc;
    /* A cold/rekeying or revoked peer blocks this transfer, not the node's
     * authentication/control worker. J exposes the block; no completion or
     * ownership retirement is inferred. Corruption/I/O remain fatal. */
    return rc == NINLIL_ERR_BUSY || rc == NINLIL_ERR_CAPACITY ||
                   rc == NINLIL_ERR_STATE || rc == NINLIL_ERR_UNAUTHORIZED
               ? NINLIL_OK
               : rc;
}
void node_bulk_close(void)
{
    ninlil_bulk_close(bulk);
    bulk = NULL;
    last_result = NINLIL_OK;
}
