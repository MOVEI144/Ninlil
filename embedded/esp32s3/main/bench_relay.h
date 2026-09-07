#ifndef NINLIL_BENCH_RELAY_H
#define NINLIL_BENCH_RELAY_H
#include "ninlil_control_log.h"
int ninlil_bench_relay_open(ninlil_control_log **log);
int ninlil_bench_relay_restore(void *ctx, const ninlil_relay_record *record);
int ninlil_bench_relay_command(char command, const uint8_t *input,
                               size_t length, uint64_t now, uint8_t *output,
                               size_t capacity, size_t *written);
#endif
