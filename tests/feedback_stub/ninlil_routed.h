#ifndef NINLIL_ROUTED_H
#define NINLIL_ROUTED_H
#include "ninlil.h"
typedef struct ninlil_routed {
    ninlil_runtime *core;
    uint64_t now_ms;
} ninlil_routed;
int ninlil_routed_receive(ninlil_routed *, const uint8_t *, size_t, uint64_t);
int ninlil_routed_poll(ninlil_routed *, uint64_t);
int ninlil_routed_frame_current(ninlil_routed *, const uint8_t *, size_t);
void ninlil_routed_tx_done(ninlil_routed *, const uint8_t *, size_t, uint64_t);
#endif
