#include "ninlil_flash_store.h"

#include <stdio.h>
#include <string.h>

#define FLASH_TEST_SECTORS 3u
#define FLASH_TEST_SIZE (FLASH_TEST_SECTORS * NINLIL_FLASH_SECTOR_SIZE)
#define MAX_CAPTURED 64u
#define TEST_FLASH_HEADER_SIZE 32u
#define TEST_FLASH_COMMIT_OFFSET 24u

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct memory_flash {
    uint8_t bytes[FLASH_TEST_SIZE];
    unsigned int read_calls;
    unsigned int write_calls;
    unsigned int erase_calls;
    unsigned int fail_write_call;
    size_t partial_write_bytes;
    uint8_t write_all_then_fail;
    unsigned int corrupt_read_call;
    size_t corrupt_read_index;
    uint8_t fail_erase;
} memory_flash;

typedef struct capture {
    uint8_t types[MAX_CAPTURED];
    uint16_t lengths[MAX_CAPTURED];
    uint8_t payloads[MAX_CAPTURED][NINLIL_FLASH_MAX_PAYLOAD];
    size_t count;
} capture;

static void memory_flash_init(memory_flash *flash)
{
    memset(flash, 0, sizeof(*flash));
    memset(flash->bytes, UINT8_C(0xFF), sizeof(flash->bytes));
}

static int memory_read(void *ctx, size_t offset, uint8_t *buffer, size_t length)
{
    memory_flash *flash = ctx;

    flash->read_calls++;
    if (offset > sizeof(flash->bytes) || length > sizeof(flash->bytes) - offset)
        return -1;
    memcpy(buffer, flash->bytes + offset, length);
    if (flash->corrupt_read_call == flash->read_calls &&
        flash->corrupt_read_index < length) {
        buffer[flash->corrupt_read_index] ^= UINT8_C(0x01);
    }
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
    if (flash->fail_erase || offset % NINLIL_FLASH_SECTOR_SIZE != 0u ||
        length % NINLIL_FLASH_SECTOR_SIZE != 0u ||
        offset > sizeof(flash->bytes) || length > sizeof(flash->bytes) - offset)
        return -1;
    memset(flash->bytes + offset, UINT8_C(0xFF), length);
    return 0;
}

static ninlil_flash_io memory_io(memory_flash *flash, size_t size)
{
    ninlil_flash_io io;

    memset(&io, 0, sizeof(io));
    io.read = memory_read;
    io.write = memory_write;
    io.erase = memory_erase;
    io.ctx = flash;
    io.size = size;
    return io;
}

static int capture_record(void *ctx, uint8_t type, const uint8_t *payload,
                          uint16_t length, size_t payload_offset)
{
    capture *records = ctx;

    (void)payload_offset;

    if (records->count >= MAX_CAPTURED || length > NINLIL_FLASH_MAX_PAYLOAD)
        return NINLIL_ERR_CAPACITY;
    records->types[records->count] = type;
    records->lengths[records->count] = length;
    if (length > 0u)
        memcpy(records->payloads[records->count], payload, length);
    records->count++;
    return NINLIL_OK;
}

static int test_append_replay(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"one", 3u) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 9u, (const uint8_t *)"two", 3u) ==
          NINLIL_OK);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(records.count == 2u);
    CHECK(records.types[0] == 1u && records.types[1] == 9u);
    CHECK(records.lengths[0] == 3u &&
          memcmp(records.payloads[0], "one", 3u) == 0);
    CHECK(records.lengths[1] == 3u &&
          memcmp(records.payloads[1], "two", 3u) == 0);
    return 0;
}

static int test_torn_record_abandons_sector(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"good", 4u) ==
          NINLIL_OK);
    flash.fail_write_call = flash.write_calls + 2u;
    flash.partial_write_bytes = 12u;
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"torn", 4u) ==
          NINLIL_ERR_IO);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(records.count == 1u);
    CHECK(ninlil_flash_store_append_offset(&store) == NINLIL_FLASH_SECTOR_SIZE);
    flash.fail_write_call = 0u;
    CHECK(ninlil_flash_store_append(&store, 2u, (const uint8_t *)"next", 4u) ==
          NINLIL_OK);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(records.count == 2u);
    CHECK(records.types[0] == 1u && records.types[1] == 2u);
    return 0;
}

static int test_commit_completed_but_error_is_recoverable(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    flash.fail_write_call = flash.write_calls + 3u;
    flash.write_all_then_fail = 1u;
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"ambiguous",
                                    9u) == NINLIL_ERR_IO);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(records.count == 1u);
    CHECK(records.lengths[0] == 9u);
    CHECK(memcmp(records.payloads[0], "ambiguous", 9u) == 0);
    return 0;
}

static int test_committed_corruption_is_hard_failure(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"crc", 3u) ==
          NINLIL_OK);
    flash.bytes[TEST_FLASH_HEADER_SIZE] ^= UINT8_C(0x01);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_sector_boundary_and_capacity(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;
    uint8_t payload[NINLIL_FLASH_MAX_PAYLOAD];
    unsigned int index;

    memory_flash_init(&flash);
    io = memory_io(&flash, NINLIL_FLASH_SECTOR_SIZE);
    memset(&records, 0, sizeof(records));
    memset(payload, UINT8_C(0x3C), sizeof(payload));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    for (index = 0u; index < 11u; index++) {
        CHECK(ninlil_flash_store_append(&store, 1u, payload, sizeof(payload)) ==
              NINLIL_OK);
    }
    CHECK(ninlil_flash_store_append(&store, 1u, payload, sizeof(payload)) ==
          NINLIL_ERR_CAPACITY);
    CHECK(ninlil_flash_store_append_offset(&store) == NINLIL_FLASH_SECTOR_SIZE);
    return 0;
}

static int test_readback_failure_poisons(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    flash.corrupt_read_call = flash.read_calls + 1u;
    flash.corrupt_read_index = 0u;
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"verify",
                                    6u) == NINLIL_ERR_IO);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"again", 5u) ==
          NINLIL_ERR_IO);
    return 0;
}

static int test_partial_write_cut_points(void)
{
    static const size_t header_cuts[] = {1u, 15u, 23u};
    static const size_t body_cuts[] = {1u, 16u, 31u, 47u};
    size_t index;

    for (index = 0u; index < sizeof(header_cuts) / sizeof(header_cuts[0]);
         index++) {
        memory_flash flash;
        ninlil_flash_io io;
        ninlil_flash_store store;
        capture records;
        uint8_t payload[32];

        memory_flash_init(&flash);
        io = memory_io(&flash, sizeof(flash.bytes));
        memset(&records, 0, sizeof(records));
        memset(payload, UINT8_C(0xA5), sizeof(payload));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 1u;
        flash.partial_write_bytes = header_cuts[index];
        CHECK(ninlil_flash_store_append(&store, 1u, payload, sizeof(payload)) ==
              NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        memset(&records, 0, sizeof(records));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        CHECK(records.count == 0u);
        CHECK(ninlil_flash_store_append_offset(&store) ==
              NINLIL_FLASH_SECTOR_SIZE);
    }
    for (index = 0u; index < sizeof(body_cuts) / sizeof(body_cuts[0]);
         index++) {
        memory_flash flash;
        ninlil_flash_io io;
        ninlil_flash_store store;
        capture records;
        uint8_t payload[32];

        memory_flash_init(&flash);
        io = memory_io(&flash, sizeof(flash.bytes));
        memset(&records, 0, sizeof(records));
        memset(payload, UINT8_C(0x5A), sizeof(payload));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 2u;
        flash.partial_write_bytes = body_cuts[index];
        CHECK(ninlil_flash_store_append(&store, 1u, payload, sizeof(payload)) ==
              NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        memset(&records, 0, sizeof(records));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        CHECK(records.count == 0u);
        CHECK(ninlil_flash_store_append_offset(&store) ==
              NINLIL_FLASH_SECTOR_SIZE);
    }
    return 0;
}

static int test_partial_commit_is_not_accepted(void)
{
    unsigned int bytes;

    for (bytes = 1u; bytes < 8u; bytes++) {
        memory_flash flash;
        ninlil_flash_io io;
        ninlil_flash_store store;
        capture records;

        memory_flash_init(&flash);
        io = memory_io(&flash, sizeof(flash.bytes));
        memset(&records, 0, sizeof(records));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        flash.fail_write_call = flash.write_calls + 3u;
        flash.partial_write_bytes = bytes;
        CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"commit",
                                        6u) == NINLIL_ERR_IO);
        flash.fail_write_call = 0u;
        memset(&records, 0, sizeof(records));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        CHECK(records.count == 0u);
        CHECK(ninlil_flash_store_append_offset(&store) ==
              NINLIL_FLASH_SECTOR_SIZE);
    }
    return 0;
}

static int test_committed_header_corruption_is_hard_failure(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"header",
                                    6u) == NINLIL_OK);
    flash.bytes[0] ^= UINT8_C(0x01);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_committed_marker_corruption_is_hard_failure(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"marker",
                                    6u) == NINLIL_OK);
    /* Clear a bit that must remain one in the committed marker. */
    flash.bytes[TEST_FLASH_COMMIT_OFFSET] &= UINT8_C(0xBF);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_erased_sector_gap_is_corrupt(void)
{
    memory_flash flash;
    ninlil_flash_io io;
    ninlil_flash_store store;
    capture records;

    memory_flash_init(&flash);
    io = memory_io(&flash, sizeof(flash.bytes));
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_OK);
    CHECK(ninlil_flash_store_append(&store, 1u, (const uint8_t *)"first", 5u) ==
          NINLIL_OK);
    store.append_offset = 2u * NINLIL_FLASH_SECTOR_SIZE;
    CHECK(ninlil_flash_store_append(&store, 2u, (const uint8_t *)"gap", 3u) ==
          NINLIL_OK);
    memset(&records, 0, sizeof(records));
    CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
          NINLIL_ERR_CORRUPT);
    return 0;
}

static int test_referenced_reads_revalidate_complete_record(void)
{
    static const size_t mutation_offsets[] = {0u, 24u, 32u, 33u, 40u, 48u};
    size_t index;

    for (index = 0u;
         index < sizeof(mutation_offsets) / sizeof(mutation_offsets[0]);
         index++) {
        memory_flash flash;
        ninlil_flash_io io;
        ninlil_flash_store store;
        capture records;
        size_t payload_offset;
        uint8_t payload = UINT8_C(0xA5);
        uint8_t result = 0u;

        memory_flash_init(&flash);
        io = memory_io(&flash, sizeof(flash.bytes));
        memset(&records, 0, sizeof(records));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_OK);
        CHECK(ninlil_flash_store_append_ref(&store, 1u, &payload, 1u,
                                            &payload_offset) == NINLIL_OK);
        CHECK(payload_offset == TEST_FLASH_HEADER_SIZE);
        flash.bytes[mutation_offsets[index]] ^=
            mutation_offsets[index] == TEST_FLASH_COMMIT_OFFSET ? UINT8_C(0x02)
                                                                : UINT8_C(0x01);
        CHECK(ninlil_flash_store_read(&store, payload_offset, 1u, 0u, &result,
                                      1u) == NINLIL_ERR_CORRUPT);
        memset(&records, 0, sizeof(records));
        CHECK(ninlil_flash_store_open(&store, &io, capture_record, &records) ==
              NINLIL_ERR_CORRUPT);
    }
    return 0;
}

static int (*const tests[])(void) = {
    test_append_replay,
    test_torn_record_abandons_sector,
    test_commit_completed_but_error_is_recoverable,
    test_committed_corruption_is_hard_failure,
    test_sector_boundary_and_capacity,
    test_readback_failure_poisons,
    test_partial_write_cut_points,
    test_partial_commit_is_not_accepted,
    test_committed_header_corruption_is_hard_failure,
    test_committed_marker_corruption_is_hard_failure,
    test_erased_sector_gap_is_corrupt,
    test_referenced_reads_revalidate_complete_record,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();
        printf("flash_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
