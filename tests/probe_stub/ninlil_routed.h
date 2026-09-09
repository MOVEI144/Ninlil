#ifndef NINLIL_PROBE_FIXTURE_ROUTED_H
#define NINLIL_PROBE_FIXTURE_ROUTED_H
#include "ninlil_node_internal.h"
typedef struct ninlil_routed { ninlil_runtime *core; uint64_t now_ms; } ninlil_routed;
int ninlil_routed_receive(ninlil_routed *, const uint8_t *, size_t, uint64_t);
int ninlil_routed_poll(ninlil_routed *, uint64_t);
int ninlil_routed_frame_current(ninlil_routed *, const uint8_t *, size_t);
#endif
