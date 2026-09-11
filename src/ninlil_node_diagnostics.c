#include "ninlil_enrollment.h"
#include "ninlil_node_internal.h"

int ninlil_node_control_inspect(const ninlil_node *n, uint8_t kind,
                                ninlil_control_counter *out)
{
    if (!n || !out || !kind || kind > NODE_CORE_RECEIPT)
        return NINLIL_ERR_INVALID;
    *out = (ninlil_control_counter){n->control_seen[kind], n->control_ok[kind],
                                    n->control_last[kind]};
    return NINLIL_OK;
}

int ninlil_node_control_receive(ninlil_node *n, uint16_t peer,
                                const uint8_t *data, size_t length,
                                int neighbor)
{
    int rc = ninlil_node_control_dispatch(n, peer, data, length, neighbor);
    if (data && length && data[0] <= NODE_CORE_RECEIPT) {
        unsigned int kind = data[0];
        if (n->control_seen[kind] != UINT16_MAX)
            n->control_seen[kind]++;
        if (rc == NINLIL_OK && n->control_ok[kind] != UINT16_MAX)
            n->control_ok[kind]++;
        n->control_last[kind] = rc;
    }
    return rc;
}
