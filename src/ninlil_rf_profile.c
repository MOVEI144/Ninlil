#include "ninlil_rf_profile.h"

int ninlil_rf_profile_validate(const ninlil_rf_profile *profile)
{
    if (!profile)
        return NINLIL_ERR_INVALID;
    if (profile->frequency_hz == 0u)
        return profile->tx_enabled ? NINLIL_ERR_INVALID : NINLIL_OK;
    if (profile->spreading_factor < 5u || profile->spreading_factor > 12u ||
        (profile->bandwidth_hz != 125000u && profile->bandwidth_hz != 250000u &&
         profile->bandwidth_hz != 500000u) ||
        profile->coding_rate_denominator < 5u ||
        profile->coding_rate_denominator > 8u || profile->preamble_symbols < 6u)
        return NINLIL_ERR_INVALID;
    if (!profile->tx_enabled)
        return NINLIL_OK;
    if (!profile->rf_gate_polarity_confirmed || !profile->region ||
        profile->region[0] == '\0' || profile->tx_power_dbm < -9 ||
        profile->tx_power_dbm > 22)
        return NINLIL_ERR_INVALID;
    return NINLIL_OK;
}
