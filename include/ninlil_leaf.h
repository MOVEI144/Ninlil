#ifndef NINLIL_LEAF_H
#define NINLIL_LEAF_H

#include "ninlil.h"

#include <stdint.h>

#define NINLIL_LEAF_MAX_RX_TOTAL_MS 3000u
#define NINLIL_LEAF_GATEWAY_STAGED_MAX 1u

typedef struct ninlil_leaf_window_profile {
    uint32_t rx1_delay_ms;
    uint32_t rx1_duration_ms;
    uint32_t rx2_delay_ms;
    uint32_t rx2_duration_ms;
    uint8_t rx2_enabled;
} ninlil_leaf_window_profile;

typedef struct ninlil_leaf_opportunity {
    ninlil_leaf_window_profile profile;
    uint64_t tx_complete_ms;
    ninlil_id staged_message_id;
    uint8_t open;
    uint8_t next_window;
    uint8_t staged;
    uint8_t delivered;
} ninlil_leaf_opportunity;

int ninlil_leaf_window_profile_lab(ninlil_leaf_window_profile *profile);
int ninlil_leaf_window_profile_validate(
    const ninlil_leaf_window_profile *profile);
/* Called after any successful uplink or explicit poll TX completion. */
int ninlil_leaf_opportunity_begin(ninlil_leaf_opportunity *opportunity,
                                  const ninlil_leaf_window_profile *profile,
                                  uint64_t tx_complete_ms);
/* Returns the next absolute start and bounded listen duration. */
int ninlil_leaf_opportunity_next(ninlil_leaf_opportunity *opportunity,
                                 uint64_t *start_ms, uint32_t *duration_ms);
/* The Gateway can stage at most one logical downlink in a wake cycle. */
int ninlil_leaf_stage_downlink(ninlil_leaf_opportunity *opportunity,
                               const ninlil_id *message_id);
int ninlil_leaf_take_downlink(ninlil_leaf_opportunity *opportunity,
                              ninlil_id *message_id);
void ninlil_leaf_opportunity_sleep(ninlil_leaf_opportunity *opportunity);

#endif
