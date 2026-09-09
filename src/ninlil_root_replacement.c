#include "ninlil_enrollment.h"
#include "ninlil_node_internal.h"
#include <string.h>

const uint8_t *ninlil_node_admission_key(ninlil_node *n)
{
    return n->config.authority_key ? n->config.authority_key
                                   : n->members[n->root_index].public_key;
}
int ninlil_node_replace_root(ninlil_node *n, const ninlil_node_member *m,
                             int persist)
{
    const ninlil_node_member *old = &n->members[n->root_index];
    uint8_t bytes[NINLIL_MEMBER_RECORD_MAX];
    size_t length;
    int rc;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = 0;
    if (!n->config.authority_key || m->grant.node != n->config.root ||
        m->grant.role != NINLIL_ROLE_SITE_GATEWAY ||
        m->grant.membership_epoch <= old->grant.membership_epoch ||
        m->grant.membership_epoch > UINT16_MAX ||
        m->grant.binding_epoch < old->grant.binding_epoch ||
        memcmp(m->grant.authority, old->grant.authority, 16u))
        return NINLIL_ERR_CONFLICT;
    for (unsigned int i = 0u; i < m->grant.service_count; i++)
        if (m->grant.services[i].maximum_payload_bytes > 64u)
            return NINLIL_ERR_TOO_LARGE;
    psa_set_key_type(&attributes,
                     PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256u);
    rc = psa_import_key(&attributes, m->public_key, 65u, &key) == PSA_SUCCESS
             ? NINLIL_OK
             : NINLIL_ERR_INVALID;
    if (key)
        (void)psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);
    if (rc != NINLIL_OK)
        return rc;
    for (unsigned int i = 0u; i < n->config.member_count; i++)
        if (i != n->root_index &&
            (!memcmp(m->grant.identity, n->members[i].grant.identity, 32u) ||
             !memcmp(m->public_key, n->members[i].public_key, 65u)))
            return NINLIL_ERR_CONFLICT;
    length = ninlil_member_encode(m, bytes, sizeof(bytes));
    if (!length)
        return NINLIL_ERR_INVALID;
    if (persist) {
        rc = ninlil_control_log_member(n->log, bytes, (uint16_t)length);
        if (rc != NINLIL_OK)
            return n->status.fault = rc;
    }
    ninlil_edhoc_close(&n->handshake);
    n->handshake_peer = NODE_NO_PEER;
    for (unsigned int i = 0u; i < n->config.member_count; i++)
        ninlil_node_disconnect(n, (uint16_t)i);
    n->members[n->root_index] = *m;
    n->dynamic_members |= (uint16_t)(1u << n->root_index);
    n->joined = 0u;
    if (n->config.local == n->config.root) {
        /* The replaced physical Root also learns the fence, durably. It must
         * never start as the new Root using its old device private key. */
        n->status.fault = NINLIL_ERR_UNAUTHORIZED;
        return NINLIL_OK;
    }
    ninlil_lease_peer_open(&n->clock, n->now_ms);
    return ninlil_join_endpoint_open(
        &n->endpoint, n->members[n->local_index].grant.identity,
        m->grant.authority, ninlil_node_join_commit, n);
}
int ninlil_node_root_clock(ninlil_node *n, uint64_t now)
{
    uint64_t generation = n->members[n->root_index].grant.membership_epoch;
    uint64_t minimum = generation << 32;
    int rc = ninlil_lease_root_open(&n->clock, n->config.root_eras, now);
    if (rc != NINLIL_OK || !n->config.authority_key)
        return rc;
    if (!generation || generation > UINT16_MAX ||
        (n->clock.stamp >> 32) > UINT16_MAX)
        return NINLIL_ERR_CAPACITY;
    /* Root generation and physical boot counter occupy disjoint fields.
     * The physical counter is never reset or copied from the dead Root. */
    n->clock.stamp |= generation << 48;
    if (n->coordinator.last_epoch < minimum) {
        if (n->coordinator.pending.epoch) {
            /* Finish the old log generation before its monotonic fence.
             * Appending this abort afterward would be an invalid replay. */
            ninlil_network_plan retired = n->coordinator.pending;
            retired.phase = NINLIL_PLAN_ABORTED;
            rc = ninlil_control_log_plan(n->log, &retired);
            if (rc == NINLIL_OK)
                rc = ninlil_node_plan_restore(n, &retired);
            if (rc != NINLIL_OK)
                return rc;
        }
        rc = ninlil_control_log_epoch(n->log, minimum);
        if (rc == NINLIL_OK)
            n->coordinator.last_epoch = minimum;
    }
    return rc;
}
