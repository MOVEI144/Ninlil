#include "ninlil_maintenance.h"
#include "ninlil_node_internal.h"
#include "ninlil_setup.h"
static int empty_control(void *ctx, ninlil_control_log *next)
{
    (void)ctx;
    (void)next;
    return NINLIL_OK; /* Collector retains and verifies the identity binding. */
}
int ninlil_node_address_idle(ninlil_node *n, uint16_t address)
{
    int rc = ninlil_control_log_verify(n->log);
    if (rc != NINLIL_OK)
        return rc;
    for (unsigned int i = 0u; i < n->relay.capacity; i++)
        if (n->custody[i].used)
            for (unsigned int j = 0u; j < n->custody[i].record.path.count; j++)
                if (n->custody[i].record.path.nodes[j] == address)
                    return NINLIL_ERR_BUSY;
    return ninlil_peer_idle(n->core, address);
}
int ninlil_node_deployment_idle(ninlil_node *n)
{
    if (!n || !n->config.offline)
        return NINLIL_ERR_STATE;
    for (unsigned int i = 0u; i < n->relay.capacity; i++)
        if (n->custody[i].used)
            return NINLIL_ERR_BUSY;
    int rc = ninlil_control_log_verify(n->log);
    return rc == NINLIL_OK ? ninlil_peer_idle(n->core, 0u) : rc;
}
int ninlil_node_retire_deployment(ninlil_node *n)
{
    int rc = ninlil_node_deployment_idle(n);
    if (rc == NINLIL_OK)
        rc = ninlil_retire_completed(n->core);
    if (rc == NINLIL_OK)
        rc = ninlil_control_log_collect(n->log, empty_control, NULL, 1);
    return rc;
}
