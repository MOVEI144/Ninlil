#ifndef NINLIL_SECURITY_PARTITIONS_H
#define NINLIL_SECURITY_PARTITIONS_H

#include "ninlil_security_state.h"

#ifdef ESP_PLATFORM

#include "esp_partition.h"

#define NINLIL_COUNTER_PARTITION_LABEL "ninlil_counter"
#define NINLIL_MEMBERSHIP_PARTITION_LABEL "ninlil_membership"
#define NINLIL_COUNTER_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x41)
#define NINLIL_MEMBERSHIP_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x42)

#define NINLIL_SESSION_COUNTER_SLOTS 32u
#define NINLIL_SESSION_PARTITION_LABEL "ninlil_sessions"
#define NINLIL_SESSION_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x43)

typedef struct ninlil_esp_security_partition {
    const esp_partition_t *partition;
    size_t offset;
    size_t size;
} ninlil_esp_security_partition;

/* Assign one persistent slot per live TX context. E2E and hop never share
 * slots. Reuse only after closing the prior session and fresh EDHOC. */
int ninlil_esp_session_counter_io(ninlil_esp_security_partition *context,
                                  ninlil_security_io *io, uint16_t slot);
int ninlil_esp_counter_io(ninlil_esp_security_partition *context,
                          ninlil_security_io *io);
int ninlil_esp_membership_io(ninlil_esp_security_partition *context,
                             ninlil_security_io *io);

#endif

#endif
