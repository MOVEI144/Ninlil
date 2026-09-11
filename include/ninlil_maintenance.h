#ifndef NINLIL_MAINTENANCE_H
#define NINLIL_MAINTENANCE_H
#include "ninlil.h"
/* Read-only pending-ownership gate. peer=0 checks all peers. Does not infer
 * failure or discard anything. Verifies retained journal references first. */
int ninlil_peer_idle(ninlil_runtime *runtime, uint16_t peer);
/* Explicit deployment retirement: only with no pending ownership. Atomically
 * retires completed transport dedupe records, preserving device binding. The
 * application ledger is separate and untouched. Close this handle afterward. */
int ninlil_retire_completed(ninlil_runtime *runtime);
#endif
