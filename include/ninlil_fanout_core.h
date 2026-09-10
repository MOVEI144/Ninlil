#ifndef NINLIL_FANOUT_CORE_H
#define NINLIL_FANOUT_CORE_H
#include "ninlil_binding.h"
#include "ninlil_fanout_store.h"

typedef struct ninlil_fanout_core {
    ninlil_runtime *core;
    ninlil_fanout_sha256 sha256;
    void *hash_ctx;
    uint16_t release_cursor;
} ninlil_fanout_core;
/* Before store_open: install the real Core adapter into a caller's store
 * config. Borrows Core/hash contexts until store close; adapter must remain at
 * the same address. Same execution owner only, no allocation, I/O or radio
 * effects. Core must use its binding_lookup and a source-bound journal. This
 * adapter does not invent credentials or alter the source/target snapshots. */
int ninlil_fanout_core_connect(ninlil_fanout_core *adapter,
                               ninlil_runtime *core,
                               ninlil_fanout_store_config *config);
/* Drive the connected store, then release at most work terminal Core retention
 * pins. Releases happen ONLY after verified durable Group TERMINAL records.
 * A missing/reclaimed Core result is safe only for an already terminal target;
 * ACTIVE/mismatching results never authorize release. Does not call Core step.
 * Drive that same real Core separately for DATA/receipt progress. Limits and
 * backpressure remain the Core's configured profile, not 512 active sessions.
 * work is 1..32. A release failure preserves the durable Group result. */
int ninlil_fanout_core_step(ninlil_fanout_core *adapter,
                            ninlil_fanout_store *store, uint64_t now_ms,
                            unsigned int work);
#endif
