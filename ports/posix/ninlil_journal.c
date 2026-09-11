#define _POSIX_C_SOURCE 200809L

#include "ninlil_journal.h"
#include "ninlil.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define JRN_VERSION 4u
#define JRN_HEADER 10u
#define JRN_CRC 4u
#define JRN_MAX_PAYLOAD 320u
#define JRN_MAX_TYPE 12u

struct ninlil_journal {
    int fd;
    int poisoned;
    uint64_t maximum_bytes;
    char *path;
    uint32_t generation;
};

static uint32_t crc32_ieee(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    unsigned int bit;

    for (index = 0u; index < length; index++) {
        crc ^= data[index];
        for (bit = 0u; bit < 8u; bit++)
            crc =
                (crc & 1u) != 0u ? (crc >> 1) ^ UINT32_C(0xEDB88320) : crc >> 1;
    }
    return ~crc;
}

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static int write_full(int fd, const uint8_t *data, size_t length)
{
    while (length > 0u) {
        ssize_t written = write(fd, data, length);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return NINLIL_ERR_IO;
        }
        if (written == 0)
            return NINLIL_ERR_IO;
        data += (size_t)written;
        length -= (size_t)written;
    }
    return NINLIL_OK;
}

static int read_full_at(int fd, off_t offset, uint8_t *data, size_t length)
{
    size_t received = 0u;
    while (received < length) {
        ssize_t result = pread(fd, data + received, length - received,
                               offset + (off_t)received);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return NINLIL_ERR_IO;
        }
        if (result == 0)
            return NINLIL_ERR_IO;
        received += (size_t)result;
    }
    return NINLIL_OK;
}

static int fsync_parent(const char *path)
{
    char *copy = strdup(path);
    const char *directory = ".";
    char *slash;
    int fd;
    int rc;

    if (!copy)
        return NINLIL_ERR_IO;
    slash = strrchr(copy, '/');
    if (slash) {
        if (slash == copy)
            copy[1] = '\0';
        else
            *slash = '\0';
        directory = copy;
    }
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        free(copy);
        return NINLIL_ERR_IO;
    }
    rc = fsync(fd);
    (void)close(fd);
    free(copy);
    return rc == 0 ? NINLIL_OK : NINLIL_ERR_IO;
}

static int header_valid(const uint8_t *header, uint16_t *length)
{
    uint16_t stored_length;
    uint16_t complement;

    if (header[0] != 'N' || header[1] != 'J' || header[2] != 'L' ||
        header[3] != '4' || header[4] != JRN_VERSION || header[5] < 1u ||
        header[5] > JRN_MAX_TYPE)
        return 0;
    stored_length = get_be16(header + 6);
    complement = get_be16(header + 8);
    if ((uint16_t)~stored_length != complement ||
        stored_length > JRN_MAX_PAYLOAD)
        return 0;
    *length = stored_length;
    return 1;
}

int ninlil_journal_open(ninlil_journal **out, const char *path,
                        uint64_t maximum_bytes,
                        ninlil_journal_on_record on_record, void *ctx)
{
    struct stat status;
    ninlil_journal *journal;
    off_t position = 0;
    off_t good = 0;
    int created = 0;
    int fd;

    if (!out || !path || path[0] == '\0' || !on_record ||
        maximum_bytes < JRN_HEADER + JRN_CRC ||
        maximum_bytes > (uint64_t)INT64_MAX)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT) {
        fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                  0600);
        if (fd >= 0)
            created = 1;
        else if (errno == EEXIST)
            fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    }
    if (fd < 0)
        return NINLIL_ERR_IO;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved = errno;
        (void)close(fd);
        return saved == EWOULDBLOCK || saved == EAGAIN ? NINLIL_ERR_BUSY
                                                       : NINLIL_ERR_IO;
    }
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode)) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }
    if (status.st_size < 0 || (uint64_t)status.st_size > maximum_bytes) {
        (void)close(fd);
        return NINLIL_ERR_CAPACITY;
    }
    if (created && (fsync(fd) != 0 || fsync_parent(path) != NINLIL_OK)) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }

    while (position < status.st_size) {
        uint8_t header[JRN_HEADER];
        uint8_t body[JRN_MAX_PAYLOAD + JRN_CRC];
        uint8_t checksum_input[JRN_HEADER + JRN_MAX_PAYLOAD];
        off_t remaining = status.st_size - position;
        off_t required;
        uint16_t length;
        int rc;

        if (remaining < (off_t)JRN_HEADER)
            break;
        if (read_full_at(fd, position, header, sizeof(header)) != NINLIL_OK) {
            (void)close(fd);
            return NINLIL_ERR_IO;
        }
        if (!header_valid(header, &length)) {
            (void)close(fd);
            return NINLIL_ERR_CORRUPT;
        }
        required = (off_t)JRN_HEADER + (off_t)length + (off_t)JRN_CRC;
        if (remaining < required)
            break;
        if (read_full_at(fd, position + (off_t)JRN_HEADER, body,
                         (size_t)length + JRN_CRC) != NINLIL_OK) {
            (void)close(fd);
            return NINLIL_ERR_IO;
        }
        memcpy(checksum_input, header, JRN_HEADER);
        if (length > 0u)
            memcpy(checksum_input + JRN_HEADER, body, length);
        if (get_be32(body + length) !=
            crc32_ieee(checksum_input, JRN_HEADER + (size_t)length)) {
            (void)close(fd);
            return NINLIL_ERR_CORRUPT;
        }
        {
            ninlil_journal_ref reference;

            reference.offset = (uint64_t)(position + (off_t)JRN_HEADER);
            reference.generation = 0u;
            reference.length = length;
            reference.type = header[5];
            rc = on_record(ctx, header[5], body, length, &reference);
            if (rc != NINLIL_OK) {
                (void)close(fd);
                return rc;
            }
        }
        position += required;
        good = position;
    }
    if (good != status.st_size &&
        (ftruncate(fd, good) != 0 || fdatasync(fd) != 0)) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }
    if (lseek(fd, 0, SEEK_END) == (off_t)-1) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }
    journal = calloc(1u, sizeof(*journal));
    if (!journal) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }
    journal->fd = fd;
    journal->maximum_bytes = maximum_bytes;
    journal->path = strdup(path);
    if (!journal->path) {
        ninlil_journal_close(journal);
        return NINLIL_ERR_CAPACITY;
    }
    *out = journal;
    return NINLIL_OK;
}

int ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                          const uint8_t *payload, uint16_t length,
                          ninlil_journal_ref *reference)
{
    uint8_t record[JRN_HEADER + JRN_MAX_PAYLOAD + JRN_CRC];
    off_t start;
    int rc;

    if (!journal || journal->fd < 0 || journal->poisoned ||
        (length > 0u && !payload) || length > JRN_MAX_PAYLOAD || type < 1u ||
        type > JRN_MAX_TYPE)
        return journal && journal->poisoned ? NINLIL_ERR_IO
                                            : NINLIL_ERR_INVALID;
    record[0] = 'N';
    record[1] = 'J';
    record[2] = 'L';
    record[3] = '4';
    record[4] = JRN_VERSION;
    record[5] = type;
    put_be16(record + 6, length);
    put_be16(record + 8, (uint16_t)~length);
    if (length > 0u)
        memcpy(record + JRN_HEADER, payload, length);
    put_be32(record + JRN_HEADER + length,
             crc32_ieee(record, JRN_HEADER + (size_t)length));
    start = lseek(journal->fd, 0, SEEK_END);
    if (start == (off_t)-1) {
        journal->poisoned = 1;
        return NINLIL_ERR_IO;
    }
    if (start < 0 || (uint64_t)start > journal->maximum_bytes ||
        JRN_HEADER + (uint64_t)length + JRN_CRC >
            journal->maximum_bytes - (uint64_t)start)
        return NINLIL_ERR_CAPACITY;
    rc = write_full(journal->fd, record, JRN_HEADER + (size_t)length + JRN_CRC);
    if (rc != NINLIL_OK || fdatasync(journal->fd) != 0) {
        (void)ftruncate(journal->fd, start);
        (void)fdatasync(journal->fd);
        (void)lseek(journal->fd, 0, SEEK_END);
        journal->poisoned = 1;
        return NINLIL_ERR_IO;
    }
    if (reference) {
        reference->offset = (uint64_t)(start + (off_t)JRN_HEADER);
        reference->generation = journal->generation;
        reference->length = length;
        reference->type = type;
    }
    return NINLIL_OK;
}

int ninlil_journal_read(ninlil_journal *journal,
                        const ninlil_journal_ref *reference,
                        uint16_t relative_offset, uint8_t *buffer,
                        uint16_t length)
{
    uint8_t record[JRN_HEADER + JRN_MAX_PAYLOAD + JRN_CRC];
    uint64_t record_offset;
    uint16_t stored_length;
    uint16_t verified_length;
    size_t total_length;
    int rc;

    if (!journal || journal->fd < 0 || journal->poisoned || !reference ||
        (length > 0u && !buffer) || relative_offset > reference->length ||
        length > (uint16_t)(reference->length - relative_offset) ||
        reference->offset < JRN_HEADER ||
        reference->offset > (uint64_t)INT64_MAX)
        return NINLIL_ERR_INVALID;
    if (reference->generation != journal->generation)
        return NINLIL_ERR_STATE;
    record_offset = reference->offset - JRN_HEADER;
    rc = read_full_at(journal->fd, (off_t)record_offset, record, JRN_HEADER);
    if (rc != NINLIL_OK)
        return rc;
    if (!header_valid(record, &stored_length) || record[5] != reference->type ||
        stored_length != reference->length)
        return NINLIL_ERR_CORRUPT;
    total_length = JRN_HEADER + (size_t)stored_length + JRN_CRC;
    if (record_offset > (uint64_t)INT64_MAX - total_length)
        return NINLIL_ERR_INVALID;
    rc = read_full_at(journal->fd, (off_t)record_offset, record, total_length);
    if (rc != NINLIL_OK)
        return rc;
    if (!header_valid(record, &verified_length) ||
        record[5] != reference->type || verified_length != stored_length ||
        get_be32(record + JRN_HEADER + stored_length) !=
            crc32_ieee(record, JRN_HEADER + (size_t)stored_length))
        return NINLIL_ERR_CORRUPT;
    if (length > 0u)
        memcpy(buffer, record + JRN_HEADER + relative_offset, length);
    return NINLIL_OK;
}

int ninlil_journal_usage(ninlil_journal *j, uint64_t *used, uint64_t *capacity)
{
    struct stat status;
    if (!j || !used || !capacity || j->poisoned || fstat(j->fd, &status) != 0 ||
        status.st_size < 0)
        return NINLIL_ERR_IO;
    *used = (uint64_t)status.st_size;
    *capacity = j->maximum_bytes;
    return *used <= *capacity ? NINLIL_OK : NINLIL_ERR_CORRUPT;
}

int ninlil_journal_visit(ninlil_journal *j, ninlil_journal_on_record visit,
                         void *ctx)
{
    uint64_t used, capacity, offset = 0u;
    int rc = ninlil_journal_usage(j, &used, &capacity);
    if (rc != NINLIL_OK || !visit)
        return rc != NINLIL_OK ? rc : NINLIL_ERR_INVALID;
    while (offset < used) {
        uint8_t bytes[JRN_HEADER + JRN_MAX_PAYLOAD + JRN_CRC];
        uint16_t length;
        ninlil_journal_ref ref;
        if (used - offset < JRN_HEADER ||
            read_full_at(j->fd, (off_t)offset, bytes, JRN_HEADER) != NINLIL_OK)
            return NINLIL_ERR_CORRUPT;
        if (!header_valid(bytes, &length) ||
            used - offset < JRN_HEADER + (uint64_t)length + JRN_CRC)
            return NINLIL_ERR_CORRUPT;
        ref.offset = offset + JRN_HEADER;
        ref.generation = j->generation;
        ref.length = length;
        ref.type = bytes[5];
        rc = ninlil_journal_read(j, &ref, 0u, bytes, length);
        if (rc == NINLIL_OK)
            rc = visit(ctx, ref.type, bytes, length, &ref);
        if (rc != NINLIL_OK)
            return rc;
        offset += JRN_HEADER + (uint64_t)length + JRN_CRC;
    }
    return NINLIL_OK;
}

int ninlil_journal_rewrite(ninlil_journal *j, ninlil_journal_snapshot snapshot,
                           void *ctx)
{
    ninlil_journal next = {0};
    struct stat old_stat, new_stat;
    char *path;
    int rc = NINLIL_ERR_IO, old_fd;
    if (!j || !snapshot || !j->path || j->poisoned ||
        j->generation == UINT32_MAX || strlen(j->path) > 4090u)
        return NINLIL_ERR_STATE;
    path = malloc(strlen(j->path) + 6u);
    if (!path)
        return NINLIL_ERR_CAPACITY;
    (void)snprintf(path, strlen(j->path) + 6u, "%s.next", j->path);
    next.fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    next.maximum_bytes = j->maximum_bytes;
    next.generation = j->generation + 1u;
    /* .next is reserved scratch. Never truncate a symlink, shared inode or
     * another user's file, including a hard link to the authoritative journal.
     */
    if (next.fd < 0 || flock(next.fd, LOCK_EX | LOCK_NB) != 0 ||
        fstat(j->fd, &old_stat) != 0 || fstat(next.fd, &new_stat) != 0 ||
        !S_ISREG(new_stat.st_mode) || new_stat.st_nlink != 1 ||
        new_stat.st_uid != geteuid() ||
        (new_stat.st_dev == old_stat.st_dev &&
         new_stat.st_ino == old_stat.st_ino) ||
        ftruncate(next.fd, 0) != 0)
        goto cleanup;
    rc = snapshot(ctx, &next);
    if (rc != NINLIL_OK)
        goto cleanup;
    if (next.poisoned || fdatasync(next.fd) != 0 ||
        rename(path, j->path) != 0) {
        rc = NINLIL_ERR_IO;
        goto cleanup;
    }
    old_fd = j->fd;
    j->fd = next.fd;
    j->generation = next.generation;
    next.fd = -1;
    (void)close(old_fd);
    rc = fsync_parent(j->path);
    if (rc != NINLIL_OK)
        j->poisoned = 1;
cleanup:
    if (next.fd >= 0)
        (void)close(next.fd);
    free(path);
    return rc;
}

void ninlil_journal_close(ninlil_journal *journal)
{
    if (!journal)
        return;
    if (journal->fd >= 0)
        (void)close(journal->fd);
    free(journal->path);
    free(journal);
}
