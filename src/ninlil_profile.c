#include "ninlil.h"

#include <string.h>

#define PROFILE_VERSION 1u
#define KIB(value) ((uint32_t)(value) * UINT32_C(1024))

int ninlil_role_profile_standard(ninlil_role role,
                                 ninlil_role_profile *profile)
{
    if (!profile)
        return NINLIL_ERR_INVALID;
    memset(profile, 0, sizeof(*profile));
    profile->profile_version = PROFILE_VERSION;
    profile->role = role;
    switch (role) {
    case NINLIL_ROLE_BATTERY_LEAF:
        profile->active_peers = 1u;
        profile->max_outbound = 8u;
        profile->max_inbound = 2u;
        profile->dedupe_ids = 32u;
        profile->service_grants = 8u;
        profile->critical_reserve = 2u;
        profile->control_reserve = 1u;
        profile->shared_slots = 5u;
        profile->dram_ceiling_bytes = KIB(16);
        profile->flash_ceiling_bytes = KIB(256);
        break;
    case NINLIL_ROLE_POWERED_ENDPOINT:
        profile->active_peers = 3u;
        profile->max_outbound = 32u;
        profile->max_inbound = 32u;
        profile->dedupe_ids = 128u;
        profile->service_grants = 16u;
        profile->critical_reserve = 4u;
        profile->control_reserve = 8u;
        profile->shared_slots = 20u;
        profile->bulk_maximum = 4u;
        profile->dram_ceiling_bytes = KIB(32);
        profile->flash_ceiling_bytes = KIB(512);
        break;
    case NINLIL_ROLE_POWERED_RELAY_CANDIDATE:
        profile->active_peers = 64u;
        profile->max_outbound = 16u;
        profile->max_inbound = 16u;
        profile->relay_custody = 64u;
        profile->dedupe_ids = 256u;
        profile->service_grants = 16u;
        profile->critical_reserve = 2u;
        profile->control_reserve = 4u;
        profile->shared_slots = 10u;
        profile->dram_ceiling_bytes = KIB(64);
        profile->flash_ceiling_bytes = KIB(1024);
        break;
    case NINLIL_ROLE_SITE_GATEWAY:
        profile->active_peers = 512u;
        profile->provisional_peers = 64u;
        profile->max_outbound = 128u;
        profile->max_inbound = 128u;
        profile->max_total_owned = 128u;
        profile->dedupe_ids = 1024u;
        profile->service_grants = 64u;
        profile->critical_reserve = 16u;
        profile->control_reserve = 32u;
        profile->shared_slots = 80u;
        profile->bulk_maximum = 8u;
        profile->dram_ceiling_bytes = KIB(128);
        profile->flash_ceiling_bytes = KIB(1024);
        break;
    default:
        memset(profile, 0, sizeof(*profile));
        return NINLIL_ERR_INVALID;
    }
    return NINLIL_OK;
}

static int profile_matches(const ninlil_role_profile *profile,
                           const ninlil_role_profile *standard)
{
    return profile->active_peers == standard->active_peers &&
           profile->provisional_peers == standard->provisional_peers &&
           profile->max_outbound == standard->max_outbound &&
           profile->max_inbound == standard->max_inbound &&
           profile->max_total_owned == standard->max_total_owned &&
           profile->relay_custody == standard->relay_custody &&
           profile->dedupe_ids == standard->dedupe_ids &&
           profile->service_grants == standard->service_grants &&
           profile->critical_reserve == standard->critical_reserve &&
           profile->control_reserve == standard->control_reserve &&
           profile->shared_slots == standard->shared_slots &&
           profile->bulk_maximum == standard->bulk_maximum &&
           profile->dram_ceiling_bytes == standard->dram_ceiling_bytes &&
           profile->flash_ceiling_bytes == standard->flash_ceiling_bytes;
}

int ninlil_role_profile_validate(const ninlil_role_profile *profile)
{
    ninlil_role_profile limit;
    uint32_t admission_total;

    if (!profile || profile->profile_version != PROFILE_VERSION ||
        ninlil_role_profile_standard(profile->role, &limit) != NINLIL_OK ||
        !profile_matches(profile, &limit) || profile->active_peers == 0u ||
        profile->max_outbound == 0u || profile->max_inbound == 0u ||
        profile->dedupe_ids == 0u || profile->service_grants == 0u ||
        profile->dram_ceiling_bytes == 0u ||
        profile->flash_ceiling_bytes == 0u)
        return NINLIL_ERR_INVALID;
    admission_total = (uint32_t)profile->critical_reserve +
                      profile->control_reserve + profile->shared_slots;
    if (admission_total != profile->max_outbound ||
        profile->bulk_maximum > profile->shared_slots ||
        (profile->max_total_owned != 0u &&
         profile->max_total_owned < profile->max_outbound))
        return NINLIL_ERR_INVALID;
    return NINLIL_OK;
}
