#include "ninlil_security_state.h"

#include <limits.h>
#include <string.h>

#define SECURITY_RECORD_SIZE 64u
#define SECURITY_CRC_OFFSET 48u
#define SECURITY_COMMIT_OFFSET 56u
#define SECURITY_VERSION 1u
#define SECURITY_ERASED UINT8_C(0xFF)
#define SECURITY_ERASED_WORD UINT32_C(0xFFFFFFFF)
#define COUNTER_COMMIT UINT32_C(0x4E435431)    /* NCT1 */
#define MEMBERSHIP_COMMIT UINT32_C(0x4E4D4231) /* NMB1 */

typedef enum slot_state {
    SLOT_EMPTY = 0,
    SLOT_INCOMPLETE = 1,
    SLOT_VALID = 2,
    SLOT_CORRUPT = 3
} slot_state;

typedef struct counter_record {
    ninlil_counter_config config;
    uint64_t reserved_until;
    uint32_t generation;
} counter_record;

typedef struct membership_disk_record {
    ninlil_membership_record record;
    uint32_t generation;
} membership_disk_record;

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t get_be64(const uint8_t *data)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; index++)
        value = (value << 8) | data[index];
    return value;
}

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void put_be64(uint8_t *data, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; index++) {
        data[7u - index] = (uint8_t)value;
        value >>= 8;
    }
}

static uint32_t crc32_ieee(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    unsigned int bit;

    for (index = 0u; index < length; index++) {
        crc ^= data[index];
        for (bit = 0u; bit < 8u; bit++) {
            crc =
                (crc & 1u) != 0u ? (crc >> 1) ^ UINT32_C(0xEDB88320) : crc >> 1;
        }
    }
    return ~crc;
}

static int all_value(const uint8_t *data, size_t length, uint8_t value)
{
    size_t index;

    for (index = 0u; index < length; index++) {
        if (data[index] != value)
            return 0;
    }
    return 1;
}

static int fingerprint_valid(const uint8_t *fingerprint)
{
    return !all_value(fingerprint, NINLIL_SECURITY_FINGERPRINT_BYTES, 0u) &&
           !all_value(fingerprint, NINLIL_SECURITY_FINGERPRINT_BYTES,
                      UINT8_C(0xFF));
}

static int io_valid(const ninlil_security_io *io)
{
    return io && io->read && io->write && io->erase &&
           io->size == NINLIL_SECURITY_PARTITION_SIZE;
}

static int read_exact(const ninlil_security_io *io, size_t offset,
                      uint8_t *buffer, size_t length)
{
    if (offset > io->size || length > io->size - offset)
        return NINLIL_ERR_INVALID;
    return io->read(io->ctx, offset, buffer, length) == 0 ? NINLIL_OK
                                                          : NINLIL_ERR_IO;
}

static int write_exact(const ninlil_security_io *io, size_t offset,
                       const uint8_t *buffer, size_t length)
{
    if (offset > io->size || length > io->size - offset)
        return NINLIL_ERR_INVALID;
    return io->write(io->ctx, offset, buffer, length) == 0 ? NINLIL_OK
                                                           : NINLIL_ERR_IO;
}

static int range_erased(const ninlil_security_io *io, size_t offset,
                        size_t length, int *erased)
{
    uint8_t buffer[64];

    if (!erased)
        return NINLIL_ERR_INVALID;
    *erased = 1;
    while (length > 0u) {
        size_t chunk = length > sizeof(buffer) ? sizeof(buffer) : length;
        int rc = read_exact(io, offset, buffer, chunk);

        if (rc != NINLIL_OK)
            return rc;
        if (!all_value(buffer, chunk, SECURITY_ERASED)) {
            *erased = 0;
            return NINLIL_OK;
        }
        offset += chunk;
        length -= chunk;
    }
    return NINLIL_OK;
}

static slot_state classify_commit(const uint8_t record[SECURITY_RECORD_SIZE],
                                  uint32_t expected)
{
    uint32_t marker = get_be32(record + SECURITY_COMMIT_OFFSET);
    uint32_t complement = get_be32(record + SECURITY_COMMIT_OFFSET + 4u);
    uint32_t expected_complement = ~expected;

    if (marker == expected && complement == expected_complement)
        return SLOT_VALID;
    if (marker == SECURITY_ERASED_WORD && complement == SECURITY_ERASED_WORD)
        return SLOT_INCOMPLETE;
    /* A partly programmed marker is indistinguishable from corruption of a
     * previously committed record. Rolling back could reuse a packet counter,
     * so ambiguous markers are always fatal. */
    return SLOT_CORRUPT;
}

static int record_crc_valid(const uint8_t record[SECURITY_RECORD_SIZE])
{
    uint32_t stored = get_be32(record + SECURITY_CRC_OFFSET);

    return stored == crc32_ieee(record, SECURITY_CRC_OFFSET) &&
           get_be32(record + SECURITY_CRC_OFFSET + 4u) == ~stored;
}

static int slot_tail_erased(const ninlil_security_io *io, unsigned int slot,
                            int *erased)
{
    size_t base = (size_t)slot * NINLIL_SECURITY_SECTOR_SIZE;

    return range_erased(io, base + SECURITY_RECORD_SIZE,
                        NINLIL_SECURITY_SECTOR_SIZE - SECURITY_RECORD_SIZE,
                        erased);
}

static uint64_t expected_high_water(uint32_t generation,
                                    uint32_t reservation_size,
                                    uint64_t max_counter_exclusive)
{
    uint64_t generation64 = generation;

    if (generation64 >
        (max_counter_exclusive + reservation_size - 1u) / reservation_size)
        return max_counter_exclusive;
    generation64 *= reservation_size;
    return generation64 > max_counter_exclusive ? max_counter_exclusive
                                                : generation64;
}

static int counter_config_equal(const ninlil_counter_config *left,
                                const ninlil_counter_config *right)
{
    return left->direction == right->direction &&
           left->reservation_size == right->reservation_size &&
           left->max_counter_exclusive == right->max_counter_exclusive &&
           memcmp(left->session_fingerprint, right->session_fingerprint,
                  NINLIL_SECURITY_FINGERPRINT_BYTES) == 0;
}

static int membership_identity_equal(const ninlil_membership_record *left,
                                     const ninlil_membership_record *right)
{
    return memcmp(left->authority_fingerprint, right->authority_fingerprint,
                  NINLIL_SECURITY_FINGERPRINT_BYTES) == 0;
}

static int membership_transition_valid(const ninlil_membership_record *older,
                                       const ninlil_membership_record *newer)
{
    if (!membership_identity_equal(older, newer) ||
        newer->membership_epoch < older->membership_epoch ||
        newer->binding_epoch < older->binding_epoch)
        return 0;
    if (newer->state == NINLIL_MEMBERSHIP_REVOKED) {
        return older->state == NINLIL_MEMBERSHIP_ACTIVE &&
               newer->node_id == older->node_id &&
               newer->membership_epoch == older->membership_epoch &&
               newer->binding_epoch == older->binding_epoch &&
               newer->capabilities == older->capabilities;
    }
    return newer->state == NINLIL_MEMBERSHIP_ACTIVE &&
           newer->membership_epoch > older->membership_epoch;
}

static slot_state read_counter_slot(const ninlil_security_io *io,
                                    unsigned int slot, counter_record *decoded,
                                    int *io_error)
{
    uint8_t record[SECURITY_RECORD_SIZE];
    size_t base = (size_t)slot * NINLIL_SECURITY_SECTOR_SIZE;
    slot_state state;
    int tail_erased;
    int rc;

    *io_error = 0;
    rc = read_exact(io, base, record, sizeof(record));
    if (rc != NINLIL_OK) {
        *io_error = rc;
        return SLOT_CORRUPT;
    }
    rc = slot_tail_erased(io, slot, &tail_erased);
    if (rc != NINLIL_OK) {
        *io_error = rc;
        return SLOT_CORRUPT;
    }
    if (!tail_erased)
        return SLOT_CORRUPT;
    if (all_value(record, sizeof(record), SECURITY_ERASED))
        return SLOT_EMPTY;

    state = classify_commit(record, COUNTER_COMMIT);
    if (state != SLOT_VALID)
        return state;
    if (record[0] != 'N' || record[1] != 'C' || record[2] != 'T' ||
        record[3] != '1' || record[4] != SECURITY_VERSION ||
        record[5] > NINLIL_DIRECTION_RESPONDER_TO_INITIATOR ||
        record[6] != 0u || record[7] != 0u || !record_crc_valid(record))
        return SLOT_CORRUPT;

    memset(decoded, 0, sizeof(*decoded));
    decoded->config.direction = record[5];
    decoded->generation = get_be32(record + 8u);
    memcpy(decoded->config.session_fingerprint, record + 12u,
           NINLIL_SECURITY_FINGERPRINT_BYTES);
    decoded->reserved_until = get_be64(record + 28u);
    decoded->config.max_counter_exclusive = get_be64(record + 36u);
    decoded->config.reservation_size = get_be32(record + 44u);
    if (decoded->generation == 0u ||
        !fingerprint_valid(decoded->config.session_fingerprint) ||
        decoded->config.reservation_size == 0u ||
        decoded->config.max_counter_exclusive == 0u ||
        decoded->config.max_counter_exclusive >
            NINLIL_SECURITY_COUNTER_MAX_EXCLUSIVE ||
        decoded->reserved_until == 0u ||
        decoded->reserved_until > decoded->config.max_counter_exclusive ||
        decoded->reserved_until !=
            expected_high_water(decoded->generation,
                                decoded->config.reservation_size,
                                decoded->config.max_counter_exclusive))
        return SLOT_CORRUPT;
    return SLOT_VALID;
}

static slot_state read_membership_slot(const ninlil_security_io *io,
                                       unsigned int slot,
                                       membership_disk_record *decoded,
                                       int *io_error)
{
    uint8_t record[SECURITY_RECORD_SIZE];
    size_t base = (size_t)slot * NINLIL_SECURITY_SECTOR_SIZE;
    slot_state state;
    int tail_erased;
    int rc;

    *io_error = 0;
    rc = read_exact(io, base, record, sizeof(record));
    if (rc != NINLIL_OK) {
        *io_error = rc;
        return SLOT_CORRUPT;
    }
    rc = slot_tail_erased(io, slot, &tail_erased);
    if (rc != NINLIL_OK) {
        *io_error = rc;
        return SLOT_CORRUPT;
    }
    if (!tail_erased)
        return SLOT_CORRUPT;
    if (all_value(record, sizeof(record), SECURITY_ERASED))
        return SLOT_EMPTY;

    state = classify_commit(record, MEMBERSHIP_COMMIT);
    if (state != SLOT_VALID)
        return state;
    if (record[0] != 'N' || record[1] != 'M' || record[2] != 'B' ||
        record[3] != '1' || record[4] != SECURITY_VERSION ||
        (record[5] != NINLIL_MEMBERSHIP_ACTIVE &&
         record[5] != NINLIL_MEMBERSHIP_REVOKED) ||
        !record_crc_valid(record))
        return SLOT_CORRUPT;

    memset(decoded, 0, sizeof(*decoded));
    decoded->record.state = (ninlil_membership_state)record[5];
    decoded->record.node_id = get_be16(record + 6u);
    decoded->generation = get_be32(record + 8u);
    memcpy(decoded->record.authority_fingerprint, record + 12u,
           NINLIL_SECURITY_FINGERPRINT_BYTES);
    decoded->record.membership_epoch = get_be64(record + 28u);
    decoded->record.binding_epoch = get_be64(record + 36u);
    decoded->record.capabilities = get_be32(record + 44u);
    if (decoded->generation == 0u || decoded->record.node_id == 0u ||
        decoded->record.node_id == UINT16_MAX ||
        !fingerprint_valid(decoded->record.authority_fingerprint) ||
        decoded->record.membership_epoch == 0u ||
        decoded->record.binding_epoch == 0u)
        return SLOT_CORRUPT;
    return SLOT_VALID;
}

static int choose_counter_record(const ninlil_security_io *io,
                                 counter_record *record, int8_t *slot)
{
    counter_record records[2];
    slot_state states[2];
    int io_error;
    unsigned int index;
    unsigned int valid_count = 0u;

    for (index = 0u; index < 2u; index++) {
        states[index] =
            read_counter_slot(io, index, &records[index], &io_error);
        if (io_error != 0)
            return io_error;
        if (states[index] == SLOT_CORRUPT)
            return NINLIL_ERR_CORRUPT;
        if (states[index] == SLOT_VALID)
            valid_count++;
    }
    if (valid_count == 0u) {
        if (states[0] == SLOT_EMPTY && states[1] == SLOT_EMPTY)
            return NINLIL_ERR_NOT_FOUND;
        return NINLIL_ERR_CORRUPT;
    }
    if (valid_count == 1u) {
        *slot = states[0] == SLOT_VALID ? 0 : 1;
        *record = records[(unsigned int)*slot];
        return NINLIL_OK;
    }
    if (!counter_config_equal(&records[0].config, &records[1].config))
        return NINLIL_ERR_CORRUPT;
    if (records[0].generation + 1u == records[1].generation) {
        *slot = 1;
    } else if (records[1].generation + 1u == records[0].generation) {
        *slot = 0;
    } else {
        return NINLIL_ERR_CORRUPT;
    }
    *record = records[(unsigned int)*slot];
    return NINLIL_OK;
}

static int choose_membership_record(const ninlil_security_io *io,
                                    membership_disk_record *record,
                                    int8_t *slot)
{
    membership_disk_record records[2];
    slot_state states[2];
    int io_error;
    unsigned int index;
    unsigned int valid_count = 0u;

    for (index = 0u; index < 2u; index++) {
        states[index] =
            read_membership_slot(io, index, &records[index], &io_error);
        if (io_error != 0)
            return io_error;
        if (states[index] == SLOT_CORRUPT)
            return NINLIL_ERR_CORRUPT;
        if (states[index] == SLOT_VALID)
            valid_count++;
    }
    if (valid_count == 0u) {
        if (states[0] == SLOT_EMPTY && states[1] == SLOT_EMPTY)
            return NINLIL_ERR_NOT_FOUND;
        return NINLIL_ERR_CORRUPT;
    }
    if (valid_count == 1u) {
        *slot = states[0] == SLOT_VALID ? 0 : 1;
        *record = records[(unsigned int)*slot];
        return NINLIL_OK;
    }
    if (records[0].generation + 1u == records[1].generation) {
        if (!membership_transition_valid(&records[0].record,
                                         &records[1].record))
            return NINLIL_ERR_CORRUPT;
        *slot = 1;
    } else if (records[1].generation + 1u == records[0].generation) {
        if (!membership_transition_valid(&records[1].record,
                                         &records[0].record))
            return NINLIL_ERR_CORRUPT;
        *slot = 0;
    } else {
        return NINLIL_ERR_CORRUPT;
    }
    *record = records[(unsigned int)*slot];
    return NINLIL_OK;
}

static int commit_record(const ninlil_security_io *io, int8_t current_slot,
                         const uint8_t record[SECURITY_RECORD_SIZE])
{
    uint8_t verify[SECURITY_RECORD_SIZE];
    unsigned int target_slot = current_slot == 0 ? 1u : 0u;
    size_t base = (size_t)target_slot * NINLIL_SECURITY_SECTOR_SIZE;
    int erased;
    int rc;

    rc = io->erase(io->ctx, base, NINLIL_SECURITY_SECTOR_SIZE) == 0
             ? NINLIL_OK
             : NINLIL_ERR_IO;
    if (rc != NINLIL_OK)
        return rc;
    rc = range_erased(io, base, NINLIL_SECURITY_SECTOR_SIZE, &erased);
    if (rc != NINLIL_OK || !erased)
        return NINLIL_ERR_IO;
    rc = write_exact(io, base, record, SECURITY_COMMIT_OFFSET);
    if (rc != NINLIL_OK)
        return rc;
    rc = read_exact(io, base, verify, SECURITY_COMMIT_OFFSET);
    if (rc != NINLIL_OK || memcmp(record, verify, SECURITY_COMMIT_OFFSET) != 0)
        return NINLIL_ERR_IO;
    rc = write_exact(io, base + SECURITY_COMMIT_OFFSET,
                     record + SECURITY_COMMIT_OFFSET,
                     SECURITY_RECORD_SIZE - SECURITY_COMMIT_OFFSET);
    if (rc != NINLIL_OK)
        return rc;
    rc = read_exact(io, base, verify, sizeof(verify));
    if (rc != NINLIL_OK || memcmp(record, verify, sizeof(verify)) != 0)
        return NINLIL_ERR_IO;
    return (int)target_slot;
}

static void build_counter_record(uint8_t record[SECURITY_RECORD_SIZE],
                                 const ninlil_counter_config *config,
                                 uint32_t generation, uint64_t reserved_until)
{
    uint32_t checksum;

    memset(record, 0, SECURITY_COMMIT_OFFSET);
    record[0] = 'N';
    record[1] = 'C';
    record[2] = 'T';
    record[3] = '1';
    record[4] = SECURITY_VERSION;
    record[5] = config->direction;
    put_be32(record + 8u, generation);
    memcpy(record + 12u, config->session_fingerprint,
           NINLIL_SECURITY_FINGERPRINT_BYTES);
    put_be64(record + 28u, reserved_until);
    put_be64(record + 36u, config->max_counter_exclusive);
    put_be32(record + 44u, config->reservation_size);
    checksum = crc32_ieee(record, SECURITY_CRC_OFFSET);
    put_be32(record + SECURITY_CRC_OFFSET, checksum);
    put_be32(record + SECURITY_CRC_OFFSET + 4u, ~checksum);
    put_be32(record + SECURITY_COMMIT_OFFSET, COUNTER_COMMIT);
    put_be32(record + SECURITY_COMMIT_OFFSET + 4u, ~COUNTER_COMMIT);
}

static void build_membership_record(uint8_t record[SECURITY_RECORD_SIZE],
                                    const ninlil_membership_record *membership,
                                    uint32_t generation)
{
    uint32_t checksum;

    memset(record, 0, SECURITY_COMMIT_OFFSET);
    record[0] = 'N';
    record[1] = 'M';
    record[2] = 'B';
    record[3] = '1';
    record[4] = SECURITY_VERSION;
    record[5] = (uint8_t)membership->state;
    put_be16(record + 6u, membership->node_id);
    put_be32(record + 8u, generation);
    memcpy(record + 12u, membership->authority_fingerprint,
           NINLIL_SECURITY_FINGERPRINT_BYTES);
    put_be64(record + 28u, membership->membership_epoch);
    put_be64(record + 36u, membership->binding_epoch);
    put_be32(record + 44u, membership->capabilities);
    checksum = crc32_ieee(record, SECURITY_CRC_OFFSET);
    put_be32(record + SECURITY_CRC_OFFSET, checksum);
    put_be32(record + SECURITY_CRC_OFFSET + 4u, ~checksum);
    put_be32(record + SECURITY_COMMIT_OFFSET, MEMBERSHIP_COMMIT);
    put_be32(record + SECURITY_COMMIT_OFFSET + 4u, ~MEMBERSHIP_COMMIT);
}

static int counter_config_valid(const ninlil_counter_config *config)
{
    uint64_t reservations;

    if (!config || !fingerprint_valid(config->session_fingerprint) ||
        config->direction > NINLIL_DIRECTION_RESPONDER_TO_INITIATOR ||
        config->reservation_size == 0u || config->max_counter_exclusive == 0u ||
        config->max_counter_exclusive > NINLIL_SECURITY_COUNTER_MAX_EXCLUSIVE)
        return 0;
    reservations = config->max_counter_exclusive / config->reservation_size;
    if (config->max_counter_exclusive % config->reservation_size != 0u)
        reservations++;
    return reservations <= UINT32_MAX;
}

static int counter_reserve(ninlil_counter_store *store,
                           uint64_t previous_high_water)
{
    uint8_t record[SECURITY_RECORD_SIZE];
    uint64_t new_high_water;
    uint32_t new_generation;
    int target_slot;

    if (previous_high_water >= store->config.max_counter_exclusive ||
        store->generation == UINT32_MAX)
        return NINLIL_ERR_CAPACITY;
    new_high_water = previous_high_water + store->config.reservation_size;
    if (new_high_water < previous_high_water ||
        new_high_water > store->config.max_counter_exclusive)
        new_high_water = store->config.max_counter_exclusive;
    new_generation = store->generation + 1u;
    build_counter_record(record, &store->config, new_generation,
                         new_high_water);
    target_slot = commit_record(&store->io, store->current_slot, record);
    if (target_slot < 0) {
        store->poisoned = 1u;
        return target_slot;
    }
    store->current_slot = (int8_t)target_slot;
    store->generation = new_generation;
    store->next_counter = previous_high_water;
    store->reserved_until = new_high_water;
    return NINLIL_OK;
}

int ninlil_security_format(const ninlil_security_io *io)
{
    unsigned int slot;

    if (!io_valid(io))
        return NINLIL_ERR_INVALID;
    for (slot = 0u; slot < 2u; slot++) {
        size_t base = (size_t)slot * NINLIL_SECURITY_SECTOR_SIZE;
        int erased;
        int rc;

        if (io->erase(io->ctx, base, NINLIL_SECURITY_SECTOR_SIZE) != 0)
            return NINLIL_ERR_IO;
        rc = range_erased(io, base, NINLIL_SECURITY_SECTOR_SIZE, &erased);
        if (rc != NINLIL_OK || !erased)
            return NINLIL_ERR_IO;
    }
    return NINLIL_OK;
}

int ninlil_counter_open(ninlil_counter_store *store,
                        const ninlil_security_io *io,
                        ninlil_counter_open_mode mode,
                        const ninlil_counter_config *config)
{
    counter_record existing;
    int8_t slot = -1;
    int rc;

    if (!store)
        return NINLIL_ERR_INVALID;
    memset(store, 0, sizeof(*store));
    store->current_slot = -1;
    if (!io_valid(io) || !counter_config_valid(config) ||
        (mode != NINLIL_COUNTER_CREATE_NEW &&
         mode != NINLIL_COUNTER_RESUME_EXISTING))
        return NINLIL_ERR_INVALID;
    store->io = *io;
    store->config = *config;
    rc = choose_counter_record(io, &existing, &slot);
    if (mode == NINLIL_COUNTER_CREATE_NEW) {
        uint8_t record[SECURITY_RECORD_SIZE];
        uint64_t high_water;
        int target_slot;

        if (rc == NINLIL_OK)
            return NINLIL_ERR_CONFLICT;
        if (rc != NINLIL_ERR_NOT_FOUND)
            return rc;
        high_water = config->reservation_size;
        if (high_water > config->max_counter_exclusive)
            high_water = config->max_counter_exclusive;
        build_counter_record(record, config, 1u, high_water);
        target_slot = commit_record(io, -1, record);
        if (target_slot < 0) {
            store->poisoned = 1u;
            return target_slot;
        }
        store->current_slot = (int8_t)target_slot;
        store->generation = 1u;
        store->next_counter = 0u;
        store->reserved_until = high_water;
        store->opened = 1u;
        return NINLIL_OK;
    }
    if (rc != NINLIL_OK)
        return rc;
    if (!counter_config_equal(config, &existing.config))
        return NINLIL_ERR_CONFLICT;
    store->current_slot = slot;
    store->generation = existing.generation;
    rc = counter_reserve(store, existing.reserved_until);
    if (rc != NINLIL_OK)
        return rc;
    store->opened = 1u;
    return NINLIL_OK;
}

int ninlil_counter_next(ninlil_counter_store *store, uint64_t *counter)
{
    int rc;

    if (!store || !counter || !store->opened)
        return NINLIL_ERR_INVALID;
    if (store->poisoned)
        return NINLIL_ERR_IO;
    if (store->next_counter > store->reserved_until)
        return NINLIL_ERR_CORRUPT;
    if (store->next_counter == store->reserved_until) {
        rc = counter_reserve(store, store->reserved_until);
        if (rc != NINLIL_OK)
            return rc;
    }
    *counter = store->next_counter;
    store->next_counter++;
    return NINLIL_OK;
}

void ninlil_counter_close(ninlil_counter_store *store)
{
    if (store)
        memset(store, 0, sizeof(*store));
}

static int membership_record_valid(const ninlil_membership_record *record)
{
    return record && record->state == NINLIL_MEMBERSHIP_ACTIVE &&
           fingerprint_valid(record->authority_fingerprint) &&
           record->node_id > 0u && record->node_id < UINT16_MAX &&
           record->membership_epoch > 0u && record->binding_epoch > 0u;
}

static int membership_same(const ninlil_membership_record *left,
                           const ninlil_membership_record *right)
{
    return left->node_id == right->node_id &&
           left->membership_epoch == right->membership_epoch &&
           left->binding_epoch == right->binding_epoch &&
           left->capabilities == right->capabilities &&
           left->state == right->state &&
           memcmp(left->authority_fingerprint, right->authority_fingerprint,
                  NINLIL_SECURITY_FINGERPRINT_BYTES) == 0;
}

static int membership_commit(ninlil_membership_store *store,
                             const ninlil_membership_record *record)
{
    uint8_t disk[SECURITY_RECORD_SIZE];
    uint32_t generation;
    int target_slot;

    if (store->generation == UINT32_MAX)
        return NINLIL_ERR_CAPACITY;
    generation = store->generation + 1u;
    build_membership_record(disk, record, generation);
    target_slot = commit_record(&store->io, store->current_slot, disk);
    if (target_slot < 0) {
        store->poisoned = 1u;
        return target_slot;
    }
    store->current_slot = (int8_t)target_slot;
    store->generation = generation;
    store->record = *record;
    store->has_record = 1u;
    return NINLIL_OK;
}

int ninlil_membership_open(ninlil_membership_store *store,
                           const ninlil_security_io *io)
{
    membership_disk_record existing;
    int8_t slot = -1;
    int rc;

    if (!store)
        return NINLIL_ERR_INVALID;
    memset(store, 0, sizeof(*store));
    store->current_slot = -1;
    if (!io_valid(io))
        return NINLIL_ERR_INVALID;
    store->io = *io;
    rc = choose_membership_record(io, &existing, &slot);
    if (rc != NINLIL_OK && rc != NINLIL_ERR_NOT_FOUND)
        return rc;
    if (rc == NINLIL_OK) {
        store->record = existing.record;
        store->generation = existing.generation;
        store->current_slot = slot;
        store->has_record = 1u;
    }
    store->opened = 1u;
    return NINLIL_OK;
}

int ninlil_membership_get(const ninlil_membership_store *store,
                          ninlil_membership_record *record)
{
    if (!store || !record || !store->opened)
        return NINLIL_ERR_INVALID;
    if (store->poisoned)
        return NINLIL_ERR_IO;
    if (!store->has_record)
        return NINLIL_ERR_NOT_FOUND;
    *record = store->record;
    return NINLIL_OK;
}

int ninlil_membership_activate(ninlil_membership_store *store,
                               const ninlil_membership_record *record)
{
    ninlil_membership_record active;

    if (!store || !store->opened || !membership_record_valid(record))
        return NINLIL_ERR_INVALID;
    if (store->poisoned)
        return NINLIL_ERR_IO;
    active = *record;
    active.state = NINLIL_MEMBERSHIP_ACTIVE;
    if (!store->has_record)
        return membership_commit(store, &active);
    if (membership_same(&store->record, &active))
        return NINLIL_OK;
    if (memcmp(store->record.authority_fingerprint,
               active.authority_fingerprint,
               NINLIL_SECURITY_FINGERPRINT_BYTES) != 0)
        return NINLIL_ERR_CONFLICT;
    if (active.membership_epoch <= store->record.membership_epoch ||
        active.binding_epoch < store->record.binding_epoch)
        return NINLIL_ERR_CONFLICT;
    return membership_commit(store, &active);
}

int ninlil_membership_revoke(ninlil_membership_store *store)
{
    ninlil_membership_record revoked;

    if (!store || !store->opened)
        return NINLIL_ERR_INVALID;
    if (store->poisoned)
        return NINLIL_ERR_IO;
    if (!store->has_record)
        return NINLIL_ERR_NOT_FOUND;
    if (store->record.state == NINLIL_MEMBERSHIP_REVOKED)
        return NINLIL_OK;
    revoked = store->record;
    revoked.state = NINLIL_MEMBERSHIP_REVOKED;
    return membership_commit(store, &revoked);
}

void ninlil_membership_close(ninlil_membership_store *store)
{
    if (store)
        memset(store, 0, sizeof(*store));
}
