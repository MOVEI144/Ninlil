#include "ninlil_flash_store.h"

#include <string.h>

#define FLASH_VERSION 3u
#define FLASH_HEADER_SIZE 32u
#define FLASH_TRAILER_SIZE 16u
#define FLASH_COMMIT UINT32_C(0x4E434D33) /* NCM3 */
#define FLASH_ERASED UINT8_C(0xFF)
#define FLASH_ERASED_WORD UINT32_C(0xFFFFFFFF)
#define FLASH_MAX_RECORD_SIZE 368u
#define FLASH_MAX_TYPE 6u
#define FLASH_HEADER_CRC_OFFSET 16u
#define FLASH_COMMIT_OFFSET 24u

typedef enum commit_state {
    COMMIT_STATE_INCOMPLETE = 0,
    COMMIT_STATE_COMMITTED = 1,
    COMMIT_STATE_CORRUPT = 2
} commit_state;

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
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

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t record_crc32(const uint8_t *record, uint16_t payload_length)
{
    uint8_t input[FLASH_COMMIT_OFFSET + NINLIL_FLASH_MAX_PAYLOAD];

    memcpy(input, record, FLASH_COMMIT_OFFSET);
    if (payload_length > 0u) {
        memcpy(input + FLASH_COMMIT_OFFSET, record + FLASH_HEADER_SIZE,
               payload_length);
    }
    return crc32_ieee(input, FLASH_COMMIT_OFFSET + (size_t)payload_length);
}

static size_t record_size(uint16_t payload_length)
{
    return FLASH_HEADER_SIZE +
           align_up((size_t)payload_length, NINLIL_FLASH_ALIGNMENT) +
           FLASH_TRAILER_SIZE;
}

static int all_erased(const uint8_t *data, size_t length)
{
    size_t index;

    for (index = 0u; index < length; index++) {
        if (data[index] != FLASH_ERASED)
            return 0;
    }
    return 1;
}

static int io_valid(const ninlil_flash_io *io)
{
    return io && io->read && io->write && io->erase &&
           io->size >= NINLIL_FLASH_SECTOR_SIZE &&
           io->size % NINLIL_FLASH_SECTOR_SIZE == 0u;
}

static int read_exact(const ninlil_flash_io *io, size_t offset, uint8_t *buffer,
                      size_t length)
{
    if (offset > io->size || length > io->size - offset)
        return NINLIL_ERR_INVALID;
    return io->read(io->ctx, offset, buffer, length) == 0 ? NINLIL_OK
                                                          : NINLIL_ERR_IO;
}

static int write_exact(const ninlil_flash_io *io, size_t offset,
                       const uint8_t *buffer, size_t length)
{
    if (offset > io->size || length > io->size - offset)
        return NINLIL_ERR_INVALID;
    return io->write(io->ctx, offset, buffer, length) == 0 ? NINLIL_OK
                                                           : NINLIL_ERR_IO;
}

static int can_be_partial_program(uint32_t value, uint32_t target)
{
    /* NOR flash programming changes only 1 bits to 0 bits. */
    return (value & target) == target;
}

static commit_state classify_commit(const uint8_t *header)
{
    uint32_t marker = get_be32(header + FLASH_COMMIT_OFFSET);
    uint32_t complement = get_be32(header + FLASH_COMMIT_OFFSET + 4u);
    uint32_t expected_complement = ~FLASH_COMMIT;

    if (marker == FLASH_COMMIT && complement == expected_complement)
        return COMMIT_STATE_COMMITTED;
    if ((marker == FLASH_ERASED_WORD && complement == FLASH_ERASED_WORD) ||
        (can_be_partial_program(marker, FLASH_COMMIT) &&
         can_be_partial_program(complement, expected_complement)))
        return COMMIT_STATE_INCOMPLETE;
    return COMMIT_STATE_CORRUPT;
}

static int header_parse(const uint8_t *header, uint8_t *type,
                        uint16_t *payload_length, uint16_t *total_length,
                        uint32_t *sequence)
{
    uint16_t stored_payload_length;
    uint16_t stored_total_length;
    uint32_t stored_header_crc;

    if (classify_commit(header) != COMMIT_STATE_COMMITTED || header[0] != 'N' ||
        header[1] != 'J' || header[2] != 'F' || header[3] != '3' ||
        header[4] != FLASH_VERSION || header[5] < 1u ||
        header[5] > FLASH_MAX_TYPE)
        return 0;
    stored_header_crc = get_be32(header + FLASH_HEADER_CRC_OFFSET);
    if (stored_header_crc != crc32_ieee(header, 16u) ||
        get_be32(header + FLASH_HEADER_CRC_OFFSET + 4u) != ~stored_header_crc)
        return 0;
    stored_payload_length = get_be16(header + 6);
    if (get_be16(header + 8) != (uint16_t)~stored_payload_length ||
        stored_payload_length > NINLIL_FLASH_MAX_PAYLOAD)
        return 0;
    stored_total_length = get_be16(header + 10);
    if (stored_total_length != record_size(stored_payload_length) ||
        stored_total_length % NINLIL_FLASH_ALIGNMENT != 0u ||
        stored_total_length > NINLIL_FLASH_SECTOR_SIZE)
        return 0;
    *type = header[5];
    *payload_length = stored_payload_length;
    *total_length = stored_total_length;
    *sequence = get_be32(header + 12);
    return *sequence != 0u;
}

static int validate_committed_record(const uint8_t *record,
                                     uint16_t payload_length,
                                     uint16_t total_length, uint32_t sequence)
{
    size_t payload_area = align_up(payload_length, NINLIL_FLASH_ALIGNMENT);
    const uint8_t *trailer = record + FLASH_HEADER_SIZE + payload_area;
    size_t padding_index;

    if (get_be32(trailer) != record_crc32(record, payload_length) ||
        get_be32(trailer + 4u) != ~get_be32(trailer) ||
        get_be32(trailer + 8u) != ~sequence ||
        get_be32(trailer + 12u) != UINT32_C(0xFFFFFFFF))
        return NINLIL_ERR_CORRUPT;
    for (padding_index = payload_length; padding_index < payload_area;
         padding_index++) {
        if (record[FLASH_HEADER_SIZE + padding_index] != FLASH_ERASED)
            return NINLIL_ERR_CORRUPT;
    }
    if ((size_t)total_length !=
        FLASH_HEADER_SIZE + payload_area + FLASH_TRAILER_SIZE)
        return NINLIL_ERR_CORRUPT;
    return NINLIL_OK;
}

int ninlil_flash_store_open(ninlil_flash_store *store,
                            const ninlil_flash_io *io,
                            ninlil_flash_on_record on_record, void *record_ctx)
{
    size_t sector_base;
    size_t highest_active_sector = 0u;
    size_t highest_end = 0u;
    int highest_abandoned = 0;
    int any_activity = 0;
    int empty_sector_seen = 0;
    uint32_t expected_sequence = 1u;

    if (!store || !io_valid(io) || !on_record)
        return NINLIL_ERR_INVALID;
    memset(store, 0, sizeof(*store));
    store->io = *io;

    for (sector_base = 0u; sector_base < io->size;
         sector_base += NINLIL_FLASH_SECTOR_SIZE) {
        size_t offset = sector_base;
        size_t sector_end = sector_base + NINLIL_FLASH_SECTOR_SIZE;
        int sector_activity = 0;
        int sector_abandoned = 0;

        while (offset + FLASH_HEADER_SIZE <= sector_end) {
            uint8_t header[FLASH_HEADER_SIZE];
            uint8_t record[FLASH_MAX_RECORD_SIZE];
            uint8_t type;
            uint16_t payload_length;
            uint16_t total_length;
            uint32_t sequence;
            commit_state commit;
            int rc;

            rc = read_exact(io, offset, header, sizeof(header));
            if (rc != NINLIL_OK)
                return rc;
            if (all_erased(header, sizeof(header)))
                break;
            sector_activity = 1;
            any_activity = 1;
            commit = classify_commit(header);
            if (commit == COMMIT_STATE_INCOMPLETE) {
                sector_abandoned = 1;
                break;
            }
            if (commit == COMMIT_STATE_CORRUPT ||
                !header_parse(header, &type, &payload_length, &total_length,
                              &sequence) ||
                offset + total_length > sector_end)
                return NINLIL_ERR_CORRUPT;
            rc = read_exact(io, offset, record, total_length);
            if (rc != NINLIL_OK)
                return rc;
            rc = validate_committed_record(record, payload_length, total_length,
                                           sequence);
            if (rc != NINLIL_OK)
                return rc;
            if (sequence != expected_sequence)
                return NINLIL_ERR_CORRUPT;
            rc = on_record(record_ctx, type, record + FLASH_HEADER_SIZE,
                           payload_length, offset + FLASH_HEADER_SIZE);
            if (rc != NINLIL_OK)
                return rc;
            expected_sequence++;
            offset += total_length;
        }
        if (sector_activity) {
            if (empty_sector_seen)
                return NINLIL_ERR_CORRUPT;
            highest_active_sector = sector_base;
            highest_end = offset;
            highest_abandoned = sector_abandoned;
        } else {
            empty_sector_seen = 1;
        }
    }

    store->next_sequence = expected_sequence;
    if (!any_activity) {
        store->append_offset = 0u;
        return NINLIL_OK;
    }
    if (!highest_abandoned &&
        highest_end < highest_active_sector + NINLIL_FLASH_SECTOR_SIZE) {
        store->append_offset = highest_end;
    } else {
        store->append_offset = highest_active_sector + NINLIL_FLASH_SECTOR_SIZE;
    }
    if (store->append_offset > io->size)
        return NINLIL_ERR_CORRUPT;
    return NINLIL_OK;
}

int ninlil_flash_store_append_ref(ninlil_flash_store *store, uint8_t type,
                                  const uint8_t *payload, uint16_t length,
                                  size_t *payload_offset)
{
    uint8_t record[FLASH_MAX_RECORD_SIZE];
    uint8_t verify[FLASH_MAX_RECORD_SIZE];
    size_t total_length;
    size_t payload_area;
    size_t sector_base;
    size_t trailer_offset;
    uint32_t checksum;
    uint32_t header_checksum;
    uint32_t sequence;
    int rc;

    if (!store || !io_valid(&store->io) || store->poisoned || type < 1u ||
        type > FLASH_MAX_TYPE || length > NINLIL_FLASH_MAX_PAYLOAD ||
        (length > 0u && !payload))
        return store && store->poisoned ? NINLIL_ERR_IO : NINLIL_ERR_INVALID;
    total_length = record_size(length);
    sector_base =
        store->append_offset - store->append_offset % NINLIL_FLASH_SECTOR_SIZE;
    if (store->append_offset + total_length >
        sector_base + NINLIL_FLASH_SECTOR_SIZE) {
        store->append_offset = sector_base + NINLIL_FLASH_SECTOR_SIZE;
    }
    if (store->append_offset > store->io.size ||
        total_length > store->io.size - store->append_offset)
        return NINLIL_ERR_CAPACITY;

    memset(record, FLASH_ERASED, total_length);
    record[0] = 'N';
    record[1] = 'J';
    record[2] = 'F';
    record[3] = '3';
    record[4] = FLASH_VERSION;
    record[5] = type;
    put_be16(record + 6, length);
    put_be16(record + 8, (uint16_t)~length);
    put_be16(record + 10, (uint16_t)total_length);
    sequence = store->next_sequence;
    put_be32(record + 12, sequence);
    header_checksum = crc32_ieee(record, 16u);
    put_be32(record + FLASH_HEADER_CRC_OFFSET, header_checksum);
    put_be32(record + FLASH_HEADER_CRC_OFFSET + 4u, ~header_checksum);
    if (length > 0u)
        memcpy(record + FLASH_HEADER_SIZE, payload, length);
    payload_area = align_up(length, NINLIL_FLASH_ALIGNMENT);
    trailer_offset = FLASH_HEADER_SIZE + payload_area;
    checksum = record_crc32(record, length);
    put_be32(record + trailer_offset, checksum);
    put_be32(record + trailer_offset + 4u, ~checksum);
    put_be32(record + trailer_offset + 8u, ~sequence);

    rc = write_exact(&store->io, store->append_offset, record,
                     FLASH_COMMIT_OFFSET);
    if (rc != NINLIL_OK)
        goto poison;
    rc = write_exact(&store->io, store->append_offset + FLASH_HEADER_SIZE,
                     record + FLASH_HEADER_SIZE,
                     total_length - FLASH_HEADER_SIZE);
    if (rc != NINLIL_OK)
        goto poison;
    rc = read_exact(&store->io, store->append_offset, verify, total_length);
    if (rc != NINLIL_OK || memcmp(record, verify, total_length) != 0) {
        rc = NINLIL_ERR_IO;
        goto poison;
    }
    put_be32(record + FLASH_COMMIT_OFFSET, FLASH_COMMIT);
    put_be32(record + FLASH_COMMIT_OFFSET + 4u, ~FLASH_COMMIT);
    rc = write_exact(&store->io, store->append_offset + FLASH_COMMIT_OFFSET,
                     record + FLASH_COMMIT_OFFSET, 8u);
    if (rc != NINLIL_OK)
        goto poison;
    rc = read_exact(&store->io, store->append_offset + FLASH_COMMIT_OFFSET,
                    verify, 8u);
    if (rc != NINLIL_OK ||
        memcmp(record + FLASH_COMMIT_OFFSET, verify, 8u) != 0) {
        rc = NINLIL_ERR_IO;
        goto poison;
    }

    if (payload_offset)
        *payload_offset = store->append_offset + FLASH_HEADER_SIZE;
    store->append_offset += total_length;
    store->next_sequence++;
    return NINLIL_OK;

poison:
    store->poisoned = 1u;
    return rc == NINLIL_OK ? NINLIL_ERR_IO : rc;
}

int ninlil_flash_store_append(ninlil_flash_store *store, uint8_t type,
                              const uint8_t *payload, uint16_t length)
{
    return ninlil_flash_store_append_ref(store, type, payload, length, NULL);
}

int ninlil_flash_store_read(const ninlil_flash_store *store,
                            size_t payload_offset, uint16_t record_length,
                            uint16_t relative_offset, uint8_t *buffer,
                            uint16_t length)
{
    if (!store || !io_valid(&store->io) || store->poisoned ||
        (length > 0u && !buffer) || relative_offset > record_length ||
        length > (uint16_t)(record_length - relative_offset) ||
        payload_offset > store->io.size ||
        relative_offset > store->io.size - payload_offset ||
        length > store->io.size - payload_offset - relative_offset)
        return NINLIL_ERR_INVALID;
    return read_exact(&store->io, payload_offset + relative_offset, buffer,
                      length);
}

int ninlil_flash_store_format(const ninlil_flash_io *io)
{
    if (!io_valid(io))
        return NINLIL_ERR_INVALID;
    return io->erase(io->ctx, 0u, io->size) == 0 ? NINLIL_OK : NINLIL_ERR_IO;
}

size_t ninlil_flash_store_append_offset(const ninlil_flash_store *store)
{
    return store ? store->append_offset : 0u;
}
