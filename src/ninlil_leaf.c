#include "ninlil_leaf.h"

#include <string.h>

static int id_valid(const ninlil_id *id)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= id->bytes[index];
    return combined != 0u;
}

int ninlil_leaf_window_profile_lab(ninlil_leaf_window_profile *profile)
{
    if (!profile)
        return NINLIL_ERR_INVALID;
    memset(profile, 0, sizeof(*profile));
    profile->rx1_delay_ms = 200u;
    profile->rx1_duration_ms = 800u;
    profile->rx2_delay_ms = 2200u;
    profile->rx2_duration_ms = 1200u;
    profile->rx2_enabled = 1u;
    return NINLIL_OK;
}

int ninlil_leaf_window_profile_validate(
    const ninlil_leaf_window_profile *profile)
{
    uint64_t total;

    if (!profile || profile->rx1_duration_ms == 0u ||
        profile->rx1_delay_ms > NINLIL_LEAF_MAX_RX_DELAY_MS ||
        profile->rx2_delay_ms > NINLIL_LEAF_MAX_RX_DELAY_MS ||
        profile->rx2_enabled > 1u ||
        (profile->rx2_enabled == 0u &&
         (profile->rx2_delay_ms != 0u || profile->rx2_duration_ms != 0u)) ||
        (profile->rx2_enabled != 0u &&
         (profile->rx2_duration_ms == 0u ||
          (uint64_t)profile->rx2_delay_ms <
              (uint64_t)profile->rx1_delay_ms + profile->rx1_duration_ms)))
        return NINLIL_ERR_INVALID;
    total = profile->rx1_duration_ms;
    if (profile->rx2_enabled)
        total += profile->rx2_duration_ms;
    return total <= NINLIL_LEAF_MAX_RX_TOTAL_MS ? NINLIL_OK
                                                : NINLIL_ERR_INVALID;
}

int ninlil_leaf_opportunity_begin(ninlil_leaf_opportunity *opportunity,
                                  const ninlil_leaf_window_profile *profile,
                                  uint64_t tx_complete_ms)
{
    if (!opportunity ||
        ninlil_leaf_window_profile_validate(profile) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    if (tx_complete_ms > UINT64_MAX - profile->rx1_delay_ms ||
        (profile->rx2_enabled &&
         tx_complete_ms > UINT64_MAX - profile->rx2_delay_ms))
        return NINLIL_ERR_INVALID;
    memset(opportunity, 0, sizeof(*opportunity));
    opportunity->profile = *profile;
    opportunity->tx_complete_ms = tx_complete_ms;
    opportunity->open = 1u;
    return NINLIL_OK;
}

int ninlil_leaf_opportunity_next(ninlil_leaf_opportunity *opportunity,
                                 uint64_t *start_ms, uint32_t *duration_ms)
{
    if (!opportunity || !start_ms || !duration_ms || !opportunity->open)
        return NINLIL_ERR_INVALID;
    if (opportunity->next_window == 0u) {
        *start_ms =
            opportunity->tx_complete_ms + opportunity->profile.rx1_delay_ms;
        *duration_ms = opportunity->profile.rx1_duration_ms;
        opportunity->next_window = 1u;
        return NINLIL_OK;
    }
    if (opportunity->next_window == 1u && opportunity->profile.rx2_enabled) {
        *start_ms =
            opportunity->tx_complete_ms + opportunity->profile.rx2_delay_ms;
        *duration_ms = opportunity->profile.rx2_duration_ms;
        opportunity->next_window = 2u;
        return NINLIL_OK;
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_leaf_stage_downlink(ninlil_leaf_opportunity *opportunity,
                               const ninlil_id *message_id)
{
    if (!opportunity || !message_id || !id_valid(message_id) ||
        !opportunity->open)
        return NINLIL_ERR_INVALID;
    if (opportunity->staged)
        return memcmp(opportunity->staged_message_id.bytes, message_id->bytes,
                      NINLIL_ID_BYTES) == 0
                   ? NINLIL_OK
                   : NINLIL_ERR_CAPACITY;
    opportunity->staged_message_id = *message_id;
    opportunity->staged = 1u;
    return NINLIL_OK;
}

int ninlil_leaf_take_downlink(ninlil_leaf_opportunity *opportunity,
                              ninlil_id *message_id)
{
    if (!opportunity || !message_id || !opportunity->open)
        return NINLIL_ERR_INVALID;
    if (!opportunity->staged || opportunity->delivered)
        return NINLIL_ERR_EMPTY;
    *message_id = opportunity->staged_message_id;
    opportunity->delivered = 1u;
    return NINLIL_OK;
}

void ninlil_leaf_opportunity_sleep(ninlil_leaf_opportunity *opportunity)
{
    if (opportunity)
        opportunity->open = 0u;
}
