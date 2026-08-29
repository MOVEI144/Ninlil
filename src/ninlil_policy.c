#include "ninlil_internal.h"

#include <string.h>

static const ninlil_service_grant *find_grant(const ninlil_peer_policy *policy,
                                              uint16_t service)
{
    uint16_t index;

    for (index = 0u; index < policy->grant_count; index++) {
        if (policy->grants[index].service_id == service)
            return &policy->grants[index];
    }
    return NULL;
}

int ninlil_authorize(ninlil_runtime *runtime, uint16_t peer, uint16_t service,
                     uint16_t payload_len, ninlil_traffic_class traffic_class,
                     uint8_t direction, uint16_t live_messages)
{
    ninlil_peer_policy policy;
    const ninlil_service_grant *grant;
    uint32_t capability;
    int rc;

    if (service < NINLIL_APPLICATION_SERVICE_MIN ||
        traffic_class > NINLIL_TRAFFIC_BULK ||
        (direction != NINLIL_SERVICE_SEND &&
         direction != NINLIL_SERVICE_RECEIVE))
        return NINLIL_ERR_INVALID;
    if (!runtime->config.policy_lookup)
        return NINLIL_ERR_STATE;
    memset(&policy, 0, sizeof(policy));
    rc = runtime->config.policy_lookup(runtime->config.policy_ctx, peer,
                                       &policy);
    if (rc == NINLIL_ERR_NOT_FOUND || rc == NINLIL_ERR_UNAUTHORIZED)
        return NINLIL_ERR_UNAUTHORIZED;
    if (rc != NINLIL_OK)
        return rc;
    if (ninlil_policy_validate(
            &policy, runtime->config.profile.service_grants) != NINLIL_OK)
        return NINLIL_ERR_CORRUPT;
    if (policy.session_membership_epoch != policy.membership_epoch)
        return NINLIL_ERR_STATE;
    capability = direction == NINLIL_SERVICE_SEND ? NINLIL_CAP_APP_SEND
                                                  : NINLIL_CAP_APP_RECEIVE;
    if ((policy.capabilities & capability) == 0u)
        return NINLIL_ERR_UNAUTHORIZED;
    grant = find_grant(&policy, service);
    if (!grant || (grant->directions & direction) == 0u ||
        payload_len > grant->maximum_payload_bytes ||
        (grant->traffic_class_mask & NINLIL_TRAFFIC_MASK(traffic_class)) ==
            0u ||
        live_messages >= grant->maximum_live_messages)
        return NINLIL_ERR_UNAUTHORIZED;
    return NINLIL_OK;
}

uint16_t ninlil_live_service(const ninlil_runtime *runtime, uint16_t peer,
                             uint16_t service, uint8_t direction)
{
    uint16_t count = 0u;
    uint16_t index;

    if (direction == NINLIL_SERVICE_RECEIVE) {
        for (index = 0u; index < runtime->outbound_capacity; index++) {
            const ninlil_outbound_entry *entry = &runtime->outbound[index];

            if (entry->used && entry->target == peer &&
                entry->service == service)
                count++;
        }
    } else {
        for (index = 0u; index < runtime->inbound_capacity; index++) {
            const ninlil_inbound_entry *entry = &runtime->inbound[index];

            if (entry->used && entry->source == peer &&
                entry->service == service)
                count++;
        }
    }
    return count;
}
