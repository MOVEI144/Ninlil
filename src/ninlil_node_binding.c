#include "ninlil_binding.h"
#include "ninlil_node_internal.h"
#include <string.h>

int ninlil_node_peer_binding(ninlil_node *n, uint16_t address,
                             ninlil_delivery_binding *out)
{
    ninlil_delivery_binding b = {0};
    ninlil_peer_policy policy;
    const ninlil_join_grant *grant, *root;
    int index, rc;
    if (!n || !out || address == n->config.local || !n->config.identity)
        return NINLIL_ERR_INVALID;
    if (n->sleeping || n->config.offline || !n->joined || n->status.fault)
        return NINLIL_ERR_STATE;
    index = ninlil_node_index(n, address);
    if (index < 0)
        return NINLIL_ERR_NOT_FOUND;
    rc = ninlil_node_policy(n, address, &policy);
    if (rc != NINLIL_OK)
        return rc;
    grant = &n->members[index].grant;
    root = &n->members[n->root_index].grant;
    if (!policy.session_membership_epoch ||
        policy.session_membership_epoch != grant->membership_epoch ||
        n->peers[n->local_index].revoked || n->peers[n->root_index].revoked ||
        memcmp(grant->authority, root->authority, 16u))
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(b.source_identity, n->config.identity->identity, 32u);
    memcpy(b.peer_identity, grant->identity, 32u);
    memcpy(b.authority, grant->authority, 16u);
    /* This node profile pins Root membership generation, not a boot/session
     * counter. Reboots retain it; Root replacement is a conservative hold,
     * never an implicit rewrite of already accepted commands. */
    b.authority_epoch = root->membership_epoch;
    b.membership_epoch = grant->membership_epoch;
    b.binding_epoch = grant->binding_epoch;
    if (!ninlil_delivery_binding_valid(&b))
        return NINLIL_ERR_STATE;
    *out = b;
    return NINLIL_OK;
}
int ninlil_node_binding_lookup(void *ctx, uint16_t peer,
                               ninlil_delivery_binding *out)
{
    return ninlil_node_peer_binding(ctx, peer, out);
}
