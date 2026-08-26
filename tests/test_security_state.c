#include "ninlil_security_state.h"

#include <stdio.h>
#include <string.h>

#define TEST_RECORD_SIZE 64u
#define TEST_CRC_OFFSET 48u
#define TEST_COMMIT_OFFSET 56u
#define TEST_SECTOR_SCAN_READS (NINLIL_SECURITY_SECTOR_SIZE / TEST_RECORD_SIZE)

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct memory_flash {
    uint8_t bytes[NINLIL_SECURITY_PARTITION_SIZE];
    unsigned int read_calls;
    unsigned int write_calls;
    unsigned int erase_calls;
    unsigned int fail_read_call;
    unsigned int fail_write_call;
    unsigned int fail_erase_call;
    size_t partial_write_bytes;
    uint8_t write_all_then_fail;
    size_t partial_erase_bytes;
    uint8_t erase_noop;
} memory_flash;

static void memory_flash_init(memory_flash *flash)
{
    memset(flash, 0, sizeof(*flash));
    memset(flash->bytes, UINT8_C(0xFF), sizeof(flash->bytes));
}

static int memory_read(void *ctx, size_t offset, uint8_t *buffer, size_t length)
{
    memory_flash *flash = ctx;

    flash->read_calls++;
    if (flash->fail_read_call == flash->read_calls ||
        offset > sizeof(flash->bytes) || length > sizeof(flash->bytes) - offset)
        return -1;
    memcpy(buffer, flash->bytes + offset, length);
    return 0;
}

static int program_bytes(memory_flash *flash, size_t offset,
                         const uint8_t *buffer, size_t length)
{
    size_t index;

    if (offset > sizeof(flash->bytes) || length > sizeof(flash->bytes) - offset)
        return -1;
    for (index = 0u; index < length; index++) {
        if ((flash->bytes[offset + index] & buffer[index]) != buffer[index])
            return -1;
    }
    for (index = 0u; index < length; index++)
        flash->bytes[offset + index] &= buffer[index];
    return 0;
}

static int memory_write(void *ctx, size_t offset, const uint8_t *buffer,
                        size_t length)
{
    memory_flash *flash = ctx;

    flash->write_calls++;
    if (flash->fail_write_call == flash->write_calls) {
        size_t written =
            flash->write_all_then_fail ? length : flash->partial_write_bytes;

        if (written > length)
            written = length;
        if (written > 0u && program_bytes(flash, offset, buffer, written) != 0)
            return -1;
        return -1;
    }
    return program_bytes(flash, offset, buffer, length);
}

static int memory_erase(void *ctx, size_t offset, size_t length)
{
    memory_flash *flash = ctx;

    flash->erase_calls++;
    if (offset % NINLIL_SECURITY_SECTOR_SIZE != 0u || length == 0u ||
        length % NINLIL_SECURITY_SECTOR_SIZE != 0u ||
        offset > sizeof(flash->bytes) || length > sizeof(flash->bytes) - offset)
        return -1;
    if (flash->fail_erase_call == flash->erase_calls) {
        size_t erased = flash->partial_erase_bytes;

        if (erased > length)
            erased = length;
        if (erased > 0u)
            memset(flash->bytes + offset, UINT8_C(0xFF), erased);
        return -1;
    }
    if (!flash->erase_noop)
        memset(flash->bytes + offset, UINT8_C(0xFF), length);
    return 0;
}

static ninlil_security_io memory_io(memory_flash *flash)
{
    ninlil_security_io io;

    memset(&io, 0, sizeof(io));
    io.read = memory_read;
    io.write = memory_write;
    io.erase = memory_erase;
    io.ctx = flash;
    io.size = sizeof(flash->bytes);
    return io;
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
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

static void refresh_record_crc(uint8_t record[TEST_RECORD_SIZE])
{
    uint32_t checksum = crc32_ieee(record, TEST_CRC_OFFSET);

    put_be32(record + TEST_CRC_OFFSET, checksum);
    put_be32(record + TEST_CRC_OFFSET + 4u, ~checksum);
}

static void fill_fingerprint(uint8_t *fingerprint, uint8_t seed)
{
    size_t index;

    for (index = 0u; index < NINLIL_SECURITY_FINGERPRINT_BYTES; index++)
        fingerprint[index] = (uint8_t)(seed + index);
}

static ninlil_counter_config
counter_config(uint8_t seed, uint32_t reservation_size, uint64_t max_counter)
{
    ninlil_counter_config config;

    memset(&config, 0, sizeof(config));
    fill_fingerprint(config.session_fingerprint, seed);
    config.direction = NINLIL_DIRECTION_INITIATOR_TO_RESPONDER;
    config.reservation_size = reservation_size;
    config.max_counter_exclusive = max_counter;
    return config;
}

static ninlil_membership_record
membership_record(uint8_t seed, uint16_t node_id, uint64_t membership_epoch,
                  uint64_t binding_epoch, uint32_t capabilities)
{
    ninlil_membership_record record;

    memset(&record, 0, sizeof(record));
    fill_fingerprint(record.authority_fingerprint, seed);
    record.node_id = node_id;
    record.membership_epoch = membership_epoch;
    record.binding_epoch = binding_epoch;
    record.capabilities = capabilities;
    record.state = NINLIL_MEMBERSHIP_ACTIVE;
    return record;
}

static int test_format_and_invalid_input(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_security_io wrong_size;
    ninlil_counter_store counter;
    ninlil_counter_config config;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(1u, 4u, 16u);
    wrong_size = io;
    wrong_size.size -= NINLIL_SECURITY_SECTOR_SIZE;
    CHECK(ninlil_security_format(NULL) == NINLIL_ERR_INVALID);
    CHECK(ninlil_security_format(&wrong_size) == NINLIL_ERR_INVALID);
    CHECK(ninlil_counter_open(&counter, &wrong_size, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_ERR_INVALID);
    flash.bytes[0] = 0u;
    CHECK(ninlil_security_format(&io) == NINLIL_OK);
    CHECK(flash.erase_calls == 2u);
    CHECK(flash.bytes[0] == UINT8_C(0xFF));
    flash.bytes[0] = 0u;
    flash.erase_noop = 1u;
    CHECK(ninlil_security_format(&io) == NINLIL_ERR_IO);
    return 0;
}

static int test_counter_config_generation_budget(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(3u, 1u, NINLIL_SECURITY_COUNTER_MAX_EXCLUSIVE);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_ERR_INVALID);
    config.reservation_size = 4096u;
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    return 0;
}

static int test_counter_create_reserve_and_resume(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;
    uint64_t counter;
    unsigned int index;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(7u, 4u, 12u);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    for (index = 0u; index < 5u; index++) {
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        CHECK(counter == index);
    }
    ninlil_counter_close(&store);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 8u);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 9u);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 10u);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 11u);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_CAPACITY);
    return 0;
}

static int test_counter_conflict_and_dirty_partition(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store first;
    ninlil_counter_store second;
    ninlil_counter_config config;
    ninlil_counter_config wrong;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(11u, 8u, 32u);
    wrong = config;
    wrong.session_fingerprint[0] ^= UINT8_C(0x80);
    CHECK(ninlil_counter_open(&first, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_open(&second, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_counter_open(&second, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &wrong) == NINLIL_ERR_CONFLICT);

    memory_flash_init(&flash);
    flash.bytes[3] = 0u;
    CHECK(ninlil_counter_open(&first, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_security_format(&io) == NINLIL_OK);
    CHECK(ninlil_counter_open(&first, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    return 0;
}

static int consume_reservation(ninlil_counter_store *store,
                               uint32_t reservation_size)
{
    uint64_t counter;
    uint32_t index;

    for (index = 0u; index < reservation_size; index++) {
        if (ninlil_counter_next(store, &counter) != NINLIL_OK ||
            counter != index)
            return 1;
    }
    return 0;
}

static int test_counter_torn_update_uses_old_high_water(void)
{
    size_t cut;

    for (cut = 0u; cut <= TEST_COMMIT_OFFSET; cut++) {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_counter_store store;
        ninlil_counter_config config;
        uint64_t counter = UINT64_MAX;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        config = counter_config(21u, 4u, 16u);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                                  &config) == NINLIL_OK);
        CHECK(consume_reservation(&store, config.reservation_size) == 0);
        flash.fail_write_call = flash.write_calls + 1u;
        flash.partial_write_bytes = cut;
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
        CHECK(counter == UINT64_MAX);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        ninlil_counter_close(&store);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                                  &config) == NINLIL_OK);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        CHECK(counter == 4u);
    }

    {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_counter_store store;
        ninlil_counter_config config;
        uint64_t counter = UINT64_MAX;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        config = counter_config(23u, 4u, 16u);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                                  &config) == NINLIL_OK);
        CHECK(consume_reservation(&store, config.reservation_size) == 0);
        flash.fail_write_call = flash.write_calls + 2u;
        flash.partial_write_bytes = 0u;
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        ninlil_counter_close(&store);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                                  &config) == NINLIL_OK);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        CHECK(counter == 4u);
    }
    return 0;
}

static int test_counter_commit_then_error_skips_ambiguous_block(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;
    uint64_t counter;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(31u, 4u, 16u);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    CHECK(consume_reservation(&store, config.reservation_size) == 0);
    flash.fail_write_call = flash.write_calls + 2u;
    flash.write_all_then_fail = 1u;
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
    flash.fail_write_call = 0u;
    flash.write_all_then_fail = 0u;
    ninlil_counter_close(&store);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 8u);
    return 0;
}

static int test_counter_partial_commit_is_fail_closed(void)
{
    size_t cut;

    for (cut = 1u; cut < 8u; cut++) {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_counter_store store;
        ninlil_counter_config config;
        uint64_t counter = UINT64_MAX;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        config = counter_config(37u, 4u, 16u);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                                  &config) == NINLIL_OK);
        CHECK(consume_reservation(&store, config.reservation_size) == 0);
        flash.fail_write_call = flash.write_calls + 2u;
        flash.partial_write_bytes = cut;
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
        CHECK(counter == UINT64_MAX);
        flash.fail_write_call = 0u;
        ninlil_counter_close(&store);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                                  &config) == NINLIL_ERR_CORRUPT);
    }
    return 0;
}

static int test_counter_erase_verification_poisons(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;
    uint64_t counter;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(39u, 2u, 16u);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    flash.erase_noop = 1u;
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
    return 0;
}

static int test_counter_partial_erase_is_fail_closed(void)
{
    size_t cut;

    for (cut = 0u; cut <= TEST_RECORD_SIZE; cut++) {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_counter_store store;
        ninlil_counter_config config;
        uint64_t counter = UINT64_MAX;
        int rc;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        config = counter_config(41u, 2u, 8u);
        CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                                  &config) == NINLIL_OK);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
        flash.fail_erase_call = flash.erase_calls + 1u;
        flash.partial_erase_bytes = cut;
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
        CHECK(counter == 3u);
        flash.fail_erase_call = 0u;
        ninlil_counter_close(&store);
        rc = ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                                 &config);
        if (cut == 0u || cut == TEST_RECORD_SIZE) {
            CHECK(rc == NINLIL_OK);
            CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
            CHECK(counter == 4u);
        } else {
            CHECK(rc == NINLIL_ERR_CORRUPT);
        }
    }
    return 0;
}

static int test_counter_read_failures_preserve_commit_boundary(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;
    uint64_t counter = UINT64_MAX;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(43u, 4u, 16u);
    flash.fail_read_call = 1u;
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_ERR_IO);

    memory_flash_init(&flash);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    CHECK(consume_reservation(&store, config.reservation_size) == 0);
    flash.fail_read_call = flash.read_calls + TEST_SECTOR_SCAN_READS + 1u;
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
    CHECK(counter == UINT64_MAX);
    flash.fail_read_call = 0u;
    ninlil_counter_close(&store);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 4u);

    memory_flash_init(&flash);
    counter = UINT64_MAX;
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    CHECK(consume_reservation(&store, config.reservation_size) == 0);
    flash.fail_read_call = flash.read_calls + TEST_SECTOR_SCAN_READS + 2u;
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_ERR_IO);
    CHECK(counter == UINT64_MAX);
    flash.fail_read_call = 0u;
    ninlil_counter_close(&store);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    CHECK(counter == 8u);
    return 0;
}

static int test_counter_corruption_is_fail_closed(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;
    uint64_t counter;
    unsigned int index;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(41u, 2u, 8u);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    for (index = 0u; index < 3u; index++)
        CHECK(ninlil_counter_next(&store, &counter) == NINLIL_OK);
    ninlil_counter_close(&store);
    flash.bytes[12] ^= UINT8_C(0x01);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_counter_tail_and_duplicate_generation_are_corrupt(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store store;
    ninlil_counter_config config;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(51u, 4u, 16u);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    memcpy(flash.bytes + NINLIL_SECURITY_SECTOR_SIZE, flash.bytes,
           TEST_RECORD_SIZE);
    ninlil_counter_close(&store);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_ERR_CORRUPT);

    memory_flash_init(&flash);
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    ninlil_counter_close(&store);
    flash.bytes[TEST_RECORD_SIZE + 10u] = 0u;
    CHECK(ninlil_counter_open(&store, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_adjacent_records_must_share_security_context(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_counter_store counter_store;
    ninlil_counter_config config;
    ninlil_membership_store membership_store;
    ninlil_membership_record first;
    ninlil_membership_record next;
    uint64_t counter;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    config = counter_config(57u, 2u, 8u);
    CHECK(ninlil_counter_open(&counter_store, &io, NINLIL_COUNTER_CREATE_NEW,
                              &config) == NINLIL_OK);
    CHECK(ninlil_counter_next(&counter_store, &counter) == NINLIL_OK);
    CHECK(ninlil_counter_next(&counter_store, &counter) == NINLIL_OK);
    CHECK(ninlil_counter_next(&counter_store, &counter) == NINLIL_OK);
    ninlil_counter_close(&counter_store);
    flash.bytes[12] ^= UINT8_C(0x01);
    refresh_record_crc(flash.bytes);
    CHECK(ninlil_counter_open(&counter_store, &io,
                              NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_ERR_CORRUPT);

    memory_flash_init(&flash);
    first = membership_record(59u, 5u, 1u, 1u, 1u);
    next = first;
    next.membership_epoch = 2u;
    CHECK(ninlil_membership_open(&membership_store, &io) == NINLIL_OK);
    CHECK(ninlil_membership_activate(&membership_store, &first) == NINLIL_OK);
    CHECK(ninlil_membership_activate(&membership_store, &next) == NINLIL_OK);
    ninlil_membership_close(&membership_store);
    flash.bytes[12] ^= UINT8_C(0x01);
    refresh_record_crc(flash.bytes);
    CHECK(ninlil_membership_open(&membership_store, &io) == NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_membership_lifecycle(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_membership_store store;
    ninlil_membership_record first;
    ninlil_membership_record next;
    ninlil_membership_record loaded;
    unsigned int writes;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    first = membership_record(61u, 7u, 1u, 10u, UINT32_C(0x00000003));
    CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
    CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_ERR_NOT_FOUND);
    CHECK(ninlil_membership_activate(&store, &first) == NINLIL_OK);
    writes = flash.write_calls;
    CHECK(ninlil_membership_activate(&store, &first) == NINLIL_OK);
    CHECK(flash.write_calls == writes);
    ninlil_membership_close(&store);

    CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
    CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_OK);
    CHECK(loaded.state == NINLIL_MEMBERSHIP_ACTIVE);
    CHECK(loaded.node_id == first.node_id);
    CHECK(loaded.membership_epoch == 1u && loaded.binding_epoch == 10u);

    next = first;
    next.node_id = 8u;
    next.membership_epoch = 2u;
    next.binding_epoch = 11u;
    next.capabilities = UINT32_C(0x00000007);
    CHECK(ninlil_membership_activate(&store, &next) == NINLIL_OK);
    CHECK(ninlil_membership_revoke(&store) == NINLIL_OK);
    CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_OK);
    CHECK(loaded.state == NINLIL_MEMBERSHIP_REVOKED);
    CHECK(ninlil_membership_activate(&store, &next) == NINLIL_ERR_CONFLICT);
    next.membership_epoch = 3u;
    CHECK(ninlil_membership_activate(&store, &next) == NINLIL_OK);
    CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_OK);
    CHECK(loaded.state == NINLIL_MEMBERSHIP_ACTIVE);
    CHECK(loaded.membership_epoch == 3u);
    return 0;
}

static int test_membership_rejects_rollback_and_authority_change(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_membership_store store;
    ninlil_membership_record active;
    ninlil_membership_record changed;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    active = membership_record(71u, 12u, 5u, 9u, 1u);
    CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
    CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);

    changed = active;
    changed.membership_epoch = 4u;
    CHECK(ninlil_membership_activate(&store, &changed) == NINLIL_ERR_CONFLICT);
    changed = active;
    changed.membership_epoch = 6u;
    changed.binding_epoch = 8u;
    CHECK(ninlil_membership_activate(&store, &changed) == NINLIL_ERR_CONFLICT);
    changed = active;
    changed.membership_epoch = 6u;
    changed.authority_fingerprint[0] ^= UINT8_C(0x01);
    CHECK(ninlil_membership_activate(&store, &changed) == NINLIL_ERR_CONFLICT);
    return 0;
}

static int test_membership_torn_and_ambiguous_commit(void)
{
    size_t cut;

    for (cut = 0u; cut <= TEST_COMMIT_OFFSET; cut++) {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_membership_store store;
        ninlil_membership_record active;
        ninlil_membership_record loaded;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        active = membership_record(81u, 20u, 1u, 1u, 1u);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 1u;
        flash.partial_write_bytes = cut;
        CHECK(ninlil_membership_revoke(&store) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        ninlil_membership_close(&store);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_OK);
        CHECK(loaded.state == NINLIL_MEMBERSHIP_ACTIVE);
    }

    {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_membership_store store;
        ninlil_membership_record active;
        ninlil_membership_record loaded;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        active = membership_record(83u, 21u, 1u, 1u, 1u);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 2u;
        flash.partial_write_bytes = 0u;
        CHECK(ninlil_membership_revoke(&store) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        ninlil_membership_close(&store);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_OK);
        CHECK(loaded.state == NINLIL_MEMBERSHIP_ACTIVE);
    }

    {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_membership_store store;
        ninlil_membership_record active;
        ninlil_membership_record loaded;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        active = membership_record(85u, 22u, 1u, 1u, 1u);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 2u;
        flash.write_all_then_fail = 1u;
        CHECK(ninlil_membership_revoke(&store) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        flash.write_all_then_fail = 0u;
        ninlil_membership_close(&store);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_get(&store, &loaded) == NINLIL_OK);
        CHECK(loaded.state == NINLIL_MEMBERSHIP_REVOKED);
    }
    return 0;
}

static int test_membership_partial_commit_is_fail_closed(void)
{
    size_t cut;

    for (cut = 1u; cut < 8u; cut++) {
        memory_flash flash;
        ninlil_security_io io;
        ninlil_membership_store store;
        ninlil_membership_record active;

        memory_flash_init(&flash);
        io = memory_io(&flash);
        active = membership_record(87u, 25u, 1u, 1u, 1u);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
        CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 2u;
        flash.partial_write_bytes = cut;
        CHECK(ninlil_membership_revoke(&store) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        ninlil_membership_close(&store);
        CHECK(ninlil_membership_open(&store, &io) == NINLIL_ERR_CORRUPT);
    }
    return 0;
}

static int test_membership_corruption_is_fail_closed(void)
{
    memory_flash flash;
    ninlil_security_io io;
    ninlil_membership_store store;
    ninlil_membership_record active;

    memory_flash_init(&flash);
    io = memory_io(&flash);
    active = membership_record(91u, 30u, 1u, 1u, 1u);
    CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
    CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);
    CHECK(ninlil_membership_revoke(&store) == NINLIL_OK);
    ninlil_membership_close(&store);
    flash.bytes[28] ^= UINT8_C(0x01);
    CHECK(ninlil_membership_open(&store, &io) == NINLIL_ERR_CORRUPT);

    memory_flash_init(&flash);
    CHECK(ninlil_membership_open(&store, &io) == NINLIL_OK);
    CHECK(ninlil_membership_activate(&store, &active) == NINLIL_OK);
    memcpy(flash.bytes + NINLIL_SECURITY_SECTOR_SIZE, flash.bytes,
           TEST_RECORD_SIZE);
    ninlil_membership_close(&store);
    CHECK(ninlil_membership_open(&store, &io) == NINLIL_ERR_CORRUPT);
    return 0;
}

static int (*const tests[])(void) = {
    test_format_and_invalid_input,
    test_counter_config_generation_budget,
    test_counter_create_reserve_and_resume,
    test_counter_conflict_and_dirty_partition,
    test_counter_torn_update_uses_old_high_water,
    test_counter_commit_then_error_skips_ambiguous_block,
    test_counter_partial_commit_is_fail_closed,
    test_counter_erase_verification_poisons,
    test_counter_partial_erase_is_fail_closed,
    test_counter_read_failures_preserve_commit_boundary,
    test_counter_corruption_is_fail_closed,
    test_counter_tail_and_duplicate_generation_are_corrupt,
    test_adjacent_records_must_share_security_context,
    test_membership_lifecycle,
    test_membership_rejects_rollback_and_authority_change,
    test_membership_torn_and_ambiguous_commit,
    test_membership_partial_commit_is_fail_closed,
    test_membership_corruption_is_fail_closed,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("security_state_%02zu %s\n", index + 1u,
               rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
