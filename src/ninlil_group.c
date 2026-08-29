#include "ninlil_group.h"

#include <string.h>

#define TARGET_PENDING 0u
#define TARGET_INFLIGHT 1u
#define TARGET_TERMINAL 2u

static int id_equal(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

static int id_valid(const ninlil_id *id)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= id->bytes[index];
    return combined != 0u;
}

static size_t target_offset(const ninlil_group_engine *engine,
                            uint8_t operation_index, uint16_t target_index)
{
    return (size_t)operation_index * engine->max_targets + target_index;
}

static ninlil_group_operation *find_operation(ninlil_group_engine *engine,
                                              const ninlil_id *id,
                                              uint8_t *operation_index)
{
    uint8_t index;

    for (index = 0u; index < engine->max_operations; index++) {
        if (engine->operations[index].used &&
            id_equal(&engine->operations[index].operation_id, id)) {
            if (operation_index)
                *operation_index = index;
            return &engine->operations[index];
        }
    }
    return NULL;
}

static int commit_record(ninlil_group_engine *engine,
                         ninlil_group_record_type type,
                         const ninlil_group_record *record)
{
    int rc = engine->commit(engine->commit_ctx, type, record);

    if (rc != NINLIL_OK)
        engine->poisoned = 1u;
    return rc;
}

int ninlil_group_open(ninlil_group_engine *engine, uint16_t *target_workspace,
                      uint8_t *state_workspace, uint8_t max_operations,
                      uint16_t max_targets, ninlil_group_commit commit,
                      void *commit_ctx)
{
    size_t slots;

    if (!engine || !target_workspace || !state_workspace || !commit ||
        max_operations == 0u || max_operations > NINLIL_GROUP_OPERATION_MAX ||
        max_targets == 0u || max_targets > NINLIL_GROUP_TARGET_MAX)
        return NINLIL_ERR_INVALID;
    slots = (size_t)max_operations * max_targets;
    memset(engine, 0, sizeof(*engine));
    memset(target_workspace, 0, slots * sizeof(*target_workspace));
    memset(state_workspace, 0, slots * sizeof(*state_workspace));
    engine->targets = target_workspace;
    engine->states = state_workspace;
    engine->max_operations = max_operations;
    engine->max_targets = max_targets;
    engine->commit = commit;
    engine->commit_ctx = commit_ctx;
    return NINLIL_OK;
}

static int targets_valid(const uint16_t *targets, uint16_t target_count)
{
    uint16_t index;

    if (!targets || target_count == 0u)
        return 0;
    for (index = 0u; index < target_count; index++) {
        uint16_t previous;

        if (targets[index] == 0u || targets[index] == UINT16_MAX)
            return 0;
        for (previous = 0u; previous < index; previous++) {
            if (targets[previous] == targets[index])
                return 0;
        }
    }
    return 1;
}

int ninlil_group_start(ninlil_group_engine *engine,
                       const ninlil_id *operation_id, const uint16_t *targets,
                       uint16_t target_count)
{
    ninlil_group_record record;
    ninlil_group_operation *operation;
    uint8_t operation_index;
    uint8_t operation_slot;
    uint16_t target_index;
    int rc;

    if (!engine || !operation_id || !id_valid(operation_id) ||
        engine->poisoned || target_count > engine->max_targets ||
        !targets_valid(targets, target_count))
        return NINLIL_ERR_INVALID;
    operation = find_operation(engine, operation_id, &operation_index);
    if (operation) {
        if (operation->target_count != target_count)
            return NINLIL_ERR_CONFLICT;
        for (target_index = 0u; target_index < target_count; target_index++) {
            if (engine->targets[target_offset(engine, operation_index,
                                              target_index)] !=
                targets[target_index])
                return NINLIL_ERR_CONFLICT;
        }
        return NINLIL_OK;
    }
    for (operation_slot = 0u; operation_slot < engine->max_operations;
         operation_slot++) {
        if (!engine->operations[operation_slot].used)
            break;
    }
    if (operation_slot == engine->max_operations)
        return NINLIL_ERR_CAPACITY;
    memset(&record, 0, sizeof(record));
    record.operation_id = *operation_id;
    record.targets = targets;
    record.target_count = target_count;
    rc = commit_record(engine, NINLIL_GROUP_RECORD_START, &record);
    if (rc != NINLIL_OK)
        return rc;
    operation_index = operation_slot;
    operation = &engine->operations[operation_index];
    memset(operation, 0, sizeof(*operation));
    operation->used = 1u;
    operation->operation_id = *operation_id;
    operation->target_count = target_count;
    for (target_index = 0u; target_index < target_count; target_index++) {
        size_t offset = target_offset(engine, operation_index, target_index);

        engine->targets[offset] = targets[target_index];
        engine->states[offset] = TARGET_PENDING;
    }
    return NINLIL_OK;
}

static int find_target(const ninlil_group_engine *engine,
                       uint8_t operation_index, uint16_t target,
                       uint16_t *target_index);

int ninlil_group_restore(ninlil_group_engine *engine,
                         ninlil_group_record_type record_type,
                         const ninlil_group_record *record)
{
    ninlil_group_operation *operation;
    uint8_t operation_index;
    uint16_t target_index;
    size_t offset;
    int rc;

    if (!engine || !record || !id_valid(&record->operation_id) ||
        engine->poisoned)
        return NINLIL_ERR_INVALID;
    operation = find_operation(engine, &record->operation_id, &operation_index);
    if (record_type == NINLIL_GROUP_RECORD_START) {
        uint8_t index;

        if (record->target_count > engine->max_targets ||
            !targets_valid(record->targets, record->target_count))
            return NINLIL_ERR_CORRUPT;
        if (operation) {
            if (operation->target_count != record->target_count)
                return NINLIL_ERR_CORRUPT;
            for (target_index = 0u; target_index < record->target_count;
                 target_index++) {
                if (engine->targets[target_offset(engine, operation_index,
                                                  target_index)] !=
                    record->targets[target_index])
                    return NINLIL_ERR_CORRUPT;
            }
            return NINLIL_OK;
        }
        for (index = 0u; index < engine->max_operations; index++) {
            if (!engine->operations[index].used)
                break;
        }
        if (index == engine->max_operations)
            return NINLIL_ERR_CAPACITY;
        operation = &engine->operations[index];
        memset(operation, 0, sizeof(*operation));
        operation->used = 1u;
        operation->operation_id = record->operation_id;
        operation->target_count = record->target_count;
        for (target_index = 0u; target_index < record->target_count;
             target_index++) {
            offset = target_offset(engine, index, target_index);
            engine->targets[offset] = record->targets[target_index];
            engine->states[offset] = TARGET_PENDING;
        }
        return NINLIL_OK;
    }
    if (!operation)
        return NINLIL_ERR_CORRUPT;
    if (record_type == NINLIL_GROUP_RECORD_FORGET) {
        if (!operation->finished || operation->inflight != 0u ||
            operation->completed != operation->target_count)
            return NINLIL_ERR_CORRUPT;
        memset(operation, 0, sizeof(*operation));
        return NINLIL_OK;
    }
    rc = find_target(engine, operation_index, record->target, &target_index);
    if (rc != NINLIL_OK)
        return NINLIL_ERR_CORRUPT;
    offset = target_offset(engine, operation_index, target_index);
    if (record_type == NINLIL_GROUP_RECORD_ADMIT) {
        if (engine->states[offset] != TARGET_PENDING ||
            engine->inflight >= NINLIL_GROUP_GATEWAY_WAVE_MAX)
            return NINLIL_ERR_CORRUPT;
        engine->states[offset] = TARGET_INFLIGHT;
        operation->inflight++;
        engine->inflight++;
        return NINLIL_OK;
    }
    if (record_type == NINLIL_GROUP_RECORD_OUTCOME) {
        if (record->outcome <= NINLIL_OUTCOME_ACTIVE ||
            record->outcome > NINLIL_OUTCOME_UNKNOWN ||
            engine->states[offset] != TARGET_INFLIGHT ||
            operation->inflight == 0u || engine->inflight == 0u)
            return NINLIL_ERR_CORRUPT;
        engine->states[offset] = TARGET_TERMINAL;
        operation->inflight--;
        operation->completed++;
        engine->inflight--;
        if (operation->completed == operation->target_count)
            operation->finished = 1u;
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}

int ninlil_group_peek(const ninlil_group_engine *engine,
                      ninlil_id *operation_id, uint16_t *target)
{
    uint8_t scanned;

    if (!engine || !operation_id || !target || engine->poisoned)
        return NINLIL_ERR_INVALID;
    if (engine->inflight >= NINLIL_GROUP_GATEWAY_WAVE_MAX)
        return NINLIL_ERR_CAPACITY;
    for (scanned = 0u; scanned < engine->max_operations; scanned++) {
        uint8_t operation_index =
            (uint8_t)((engine->operation_cursor + scanned) %
                      engine->max_operations);
        const ninlil_group_operation *operation =
            &engine->operations[operation_index];
        uint16_t target_index;

        if (!operation->used)
            continue;
        for (target_index = 0u; target_index < operation->target_count;
             target_index++) {
            size_t offset =
                target_offset(engine, operation_index, target_index);

            if (engine->states[offset] == TARGET_PENDING) {
                *operation_id = operation->operation_id;
                *target = engine->targets[offset];
                return NINLIL_OK;
            }
        }
    }
    return NINLIL_ERR_EMPTY;
}

static int find_target(const ninlil_group_engine *engine,
                       uint8_t operation_index, uint16_t target,
                       uint16_t *target_index)
{
    const ninlil_group_operation *operation =
        &engine->operations[operation_index];
    uint16_t index;

    for (index = 0u; index < operation->target_count; index++) {
        if (engine->targets[target_offset(engine, operation_index, index)] ==
            target) {
            *target_index = index;
            return NINLIL_OK;
        }
    }
    return NINLIL_ERR_NOT_FOUND;
}

int ninlil_group_mark_admitted(ninlil_group_engine *engine,
                               const ninlil_id *operation_id, uint16_t target)
{
    ninlil_group_operation *operation;
    ninlil_group_record record;
    uint8_t operation_index;
    uint16_t target_index;
    size_t offset;
    int rc;

    if (!engine || !operation_id || !id_valid(operation_id) || engine->poisoned)
        return NINLIL_ERR_INVALID;
    if (engine->inflight >= NINLIL_GROUP_GATEWAY_WAVE_MAX)
        return NINLIL_ERR_CAPACITY;
    operation = find_operation(engine, operation_id, &operation_index);
    if (!operation)
        return NINLIL_ERR_NOT_FOUND;
    rc = find_target(engine, operation_index, target, &target_index);
    if (rc != NINLIL_OK)
        return rc;
    offset = target_offset(engine, operation_index, target_index);
    if (engine->states[offset] != TARGET_PENDING)
        return NINLIL_ERR_STATE;
    memset(&record, 0, sizeof(record));
    record.operation_id = *operation_id;
    record.target = target;
    rc = commit_record(engine, NINLIL_GROUP_RECORD_ADMIT, &record);
    if (rc != NINLIL_OK)
        return rc;
    engine->states[offset] = TARGET_INFLIGHT;
    operation->inflight++;
    engine->inflight++;
    engine->operation_cursor =
        (uint8_t)((operation_index + 1u) % engine->max_operations);
    return NINLIL_OK;
}

int ninlil_group_mark_terminal(ninlil_group_engine *engine,
                               const ninlil_id *operation_id, uint16_t target,
                               ninlil_outcome outcome)
{
    ninlil_group_operation *operation;
    ninlil_group_record record;
    uint8_t operation_index;
    uint16_t target_index;
    size_t offset;
    int rc;

    if (!engine || !operation_id || !id_valid(operation_id) ||
        engine->poisoned || outcome <= NINLIL_OUTCOME_ACTIVE ||
        outcome > NINLIL_OUTCOME_UNKNOWN)
        return NINLIL_ERR_INVALID;
    operation = find_operation(engine, operation_id, &operation_index);
    if (!operation)
        return NINLIL_ERR_NOT_FOUND;
    rc = find_target(engine, operation_index, target, &target_index);
    if (rc != NINLIL_OK)
        return rc;
    offset = target_offset(engine, operation_index, target_index);
    if (engine->states[offset] != TARGET_INFLIGHT)
        return NINLIL_ERR_STATE;
    memset(&record, 0, sizeof(record));
    record.operation_id = *operation_id;
    record.target = target;
    record.outcome = outcome;
    rc = commit_record(engine, NINLIL_GROUP_RECORD_OUTCOME, &record);
    if (rc != NINLIL_OK)
        return rc;
    engine->states[offset] = TARGET_TERMINAL;
    operation->inflight--;
    operation->completed++;
    engine->inflight--;
    if (operation->completed == operation->target_count)
        operation->finished = 1u;
    return NINLIL_OK;
}

int ninlil_group_forget(ninlil_group_engine *engine,
                        const ninlil_id *operation_id)
{
    ninlil_group_operation *operation;
    ninlil_group_record record;
    int rc;

    if (!engine || !operation_id || !id_valid(operation_id) || engine->poisoned)
        return NINLIL_ERR_INVALID;
    operation = find_operation(engine, operation_id, NULL);
    if (!operation)
        return NINLIL_ERR_NOT_FOUND;
    if (!operation->finished || operation->inflight != 0u ||
        operation->completed != operation->target_count)
        return NINLIL_ERR_STATE;
    memset(&record, 0, sizeof(record));
    record.operation_id = *operation_id;
    rc = commit_record(engine, NINLIL_GROUP_RECORD_FORGET, &record);
    if (rc == NINLIL_OK)
        memset(operation, 0, sizeof(*operation));
    return rc;
}
