#ifndef NINLIL_RF_PROFILE_H
#define NINLIL_RF_PROFILE_H

#include "ninlil.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ninlil_rf_profile {
    bool tx_enabled;
    bool rf_gate_polarity_confirmed;
    const char *region;
    uint32_t frequency_hz;
    int8_t tx_power_dbm;
    uint8_t spreading_factor;
    uint32_t bandwidth_hz;
    uint8_t coding_rate_denominator;
    uint16_t preamble_symbols;
} ninlil_rf_profile;

int ninlil_rf_profile_validate(const ninlil_rf_profile *profile);

#endif
