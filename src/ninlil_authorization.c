#include "ninlil.h"

#include <string.h>

static int role_capabilities_valid(const ninlil_peer_policy *policy)
{
    if ((policy->capabilities & ~NINLIL_CAP_KNOWN_MASK) != 0u)
        return 0;
    if (policy->role == NINLIL_ROLE_BATTERY_LEAF &&
        (policy->capabilities & NINLIL_CAP_RELAY_CUSTODY) != 0u)
        return 0;
    if (policy->role != NINLIL_ROLE_SITE_GATEWAY &&
        (policy->capabilities & NINLIL_CAP_GATEWAY_RADIO_HEAD) != 0u)
        return 0;
    return policy->role >= NINLIL_ROLE_BATTERY_LEAF &&
           policy->role <= NINLIL_ROLE_SITE_GATEWAY;
}

static int grant_valid(const ninlil_service_grant *grant)
{
    return grant->service_id >= NINLIL_APPLICATION_SERVICE_MIN &&
           grant->maximum_payload_bytes > 0u &&
           grant->maximum_payload_bytes <= NINLIL_MAX_PAYLOAD &&
           grant->maximum_live_messages > 0u &&
           grant->directions != 0u &&
           (grant->directions & (uint8_t)~NINLIL_SERVICE_BOTH) == 0u &&
           grant->traffic_class_mask != 0u &&
           (grant->traffic_class_mask & UINT8_C(0xF0)) == 0u;
}

int ninlil_policy_validate(const ninlil_peer_policy *policy,
                           uint16_t grant_limit)
{
    uint16_t index;

    if (!policy || !role_capabilities_valid(policy) ||
        policy->membership_epoch == 0u || policy->grant_count > grant_limit ||
        (policy->grant_count > 0u && !policy->grants))
        return NINLIL_ERR_INVALID;
    for (index = 0u; index < policy->grant_count; index++) {
        uint16_t previous;

        if (!grant_valid(&policy->grants[index]))
            return NINLIL_ERR_INVALID;
        for (previous = 0u; previous < index; previous++) {
            if (policy->grants[previous].service_id ==
                policy->grants[index].service_id)
                return NINLIL_ERR_CONFLICT;
        }
    }
    return NINLIL_OK;
}

static int grants_equal(const ninlil_peer_policy *left,
                        const ninlil_peer_policy *right)
{
    if (left->grant_count != right->grant_count)
        return 0;
    if (left->grant_count == 0u)
        return 1;
    return memcmp(left->grants, right->grants,
                  (size_t)left->grant_count * sizeof(*left->grants)) == 0;
}

int ninlil_policy_update_validate(const ninlil_peer_policy *older,
                                  const ninlil_peer_policy *newer,
                                  uint16_t grant_limit)
{
    int changed;

    if (ninlil_policy_validate(older, grant_limit) != NINLIL_OK ||
        ninlil_policy_validate(newer, grant_limit) != NINLIL_OK ||
        newer->membership_epoch < older->membership_epoch)
        return NINLIL_ERR_INVALID;
    changed = older->role != newer->role ||
              older->capabilities != newer->capabilities ||
              !grants_equal(older, newer);
    if (changed && newer->membership_epoch <= older->membership_epoch)
        return NINLIL_ERR_CONFLICT;
    if (newer->session_membership_epoch != newer->membership_epoch)
        return NINLIL_ERR_STATE;
    return NINLIL_OK;
}
