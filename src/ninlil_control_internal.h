#ifndef NINLIL_CONTROL_INTERNAL_H
#define NINLIL_CONTROL_INTERNAL_H
#include "ninlil_control_log.h"
#include "ninlil_journal.h"
// Record numbers are local to this separate journal; typed payload magics
// prevent confusing it with a delivery journal. Existing envelopes stay v4/v5.
#define JOIN_RECORD 1u
#define PLAN_RECORD 2u
#define RELAY_RECORD 3u
#define STORAGE_BINDING 4u
#define EPOCH_FENCE 5u

typedef struct packet_reference {
    uint8_t id[16];
    ninlil_journal_ref reference;
    uint8_t used;
} packet_reference;

struct ninlil_control_log {
    ninlil_journal *journal;
    ninlil_control_replay replay;
    packet_reference packets[NINLIL_RELAY_PACKETS_MAX];
    uint8_t poisoned;
    uint8_t identity[32];
    uint8_t bound;
    uint8_t has_records;
    uint64_t collected_bytes;
};

#endif
