#ifndef NINLIL_GROUP_H
#define NINLIL_GROUP_H

#include "ninlil.h"

#include <stdint.h>

#define NINLIL_GROUP_TARGET_MAX 512u
#define NINLIL_GROUP_OPERATION_MAX 4u
#define NINLIL_GROUP_GATEWAY_WAVE_MAX 32u

typedef enum ninlil_group_record_type {
    NINLIL_GROUP_RECORD_START = 1,
    NINLIL_GROUP_RECORD_ADMIT = 2,
    NINLIL_GROUP_RECORD_OUTCOME = 3,
    NINLIL_GROUP_RECORD_FINISH = 4
} ninlil_group_record_type;

typedef struct ninlil_group_record {
    ninlil_id operation_id;
    const uint16_t *targets;
    uint16_t target_count;
    uint16_t target;
    ninlil_outcome outcome;
} ninlil_group_record;

typedef int (*ninlil_group_commit)(void *ctx,
                                   ninlil_group_record_type record_type,
                                   const ninlil_group_record *record);

typedef struct ninlil_group_operation {
    ninlil_id operation_id;
    uint16_t target_count;
    uint16_t inflight;
    uint16_t completed;
    uint8_t used;
} ninlil_group_operation;

typedef struct ninlil_group_engine {
    ninlil_group_operation operations[NINLIL_GROUP_OPERATION_MAX];
    uint16_t *targets;
    uint8_t *states;
    uint16_t max_targets;
    uint16_t inflight;
    uint8_t max_operations;
    uint8_t operation_cursor;
    uint8_t poisoned;
    ninlil_group_commit commit;
    void *commit_ctx;
} ninlil_group_engine;

/* target and state workspaces require max_operations * max_targets elements
 * and remain caller-owned. A target snapshot is copied only after START is
 * durably committed. */
int ninlil_group_open(ninlil_group_engine *engine, uint16_t *target_workspace,
                      uint8_t *state_workspace, uint8_t max_operations,
                      uint16_t max_targets, ninlil_group_commit commit,
                      void *commit_ctx);
int ninlil_group_start(ninlil_group_engine *engine,
                       const ninlil_id *operation_id, const uint16_t *targets,
                       uint16_t target_count);
/* peek does not reserve or submit; the caller durably submits one logical
 * delivery and then calls mark_admitted. */
int ninlil_group_peek(const ninlil_group_engine *engine,
                      ninlil_id *operation_id, uint16_t *target);
int ninlil_group_mark_admitted(ninlil_group_engine *engine,
                               const ninlil_id *operation_id,
                               uint16_t target);
int ninlil_group_mark_terminal(ninlil_group_engine *engine,
                               const ninlil_id *operation_id, uint16_t target,
                               ninlil_outcome outcome);

#endif
