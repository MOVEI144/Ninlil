#ifndef NINLIL_RADIO_ADAPT_H
#define NINLIL_RADIO_ADAPT_H
#include "ninlil.h"
/* Explicit local TX-only policy. No frequency/modulation negotiation.
 * Eight authenticated, completed probe results form a window. Three perfect
 * fresh windows and a 30 s dwell permit one 3 dB decrease. <=6/8 successes
 * restores the maximum. Missing/stale evidence also restores the maximum. */
typedef struct ninlil_radio_adapt {
    uint64_t observed_ms, changed_ms, now_ms;
    int8_t maximum_dbm, minimum_dbm, power_dbm;
    uint8_t good;
} ninlil_radio_adapt;
int ninlil_radio_adapt_open(ninlil_radio_adapt *state, int8_t minimum_dbm,
                            int8_t maximum_dbm);
int ninlil_radio_adapt_observe(ninlil_radio_adapt *state, uint64_t now_ms,
                               uint64_t observed_ms, uint16_t attempts,
                               uint16_t delivered, int8_t *power_dbm);
#endif
