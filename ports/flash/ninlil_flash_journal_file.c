#define _POSIX_C_SOURCE 200809L

#include "ninlil_flash_bank.h"
#include "ninlil_journal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_FLASH_SIZE (128u * 1024u)
#define IO_CHUNK 512u

struct file_flash_context {
    int fd;
    size_t base;
};

struct ninlil_journal {
    struct file_flash_context file;
    struct file_flash_context spare_file;
    ninlil_flash_bank bank;
    ninlil_journal_on_record on_record;
    void *record_ctx;
};

static int replay_record(void *ctx, uint8_t type, const uint8_t *payload,
                         uint16_t length, size_t payload_offset)
{
    ninlil_journal *journal = ctx;
    ninlil_journal_ref reference;

    reference.offset = payload_offset;
    reference.generation = (uint32_t)journal->bank.generation;
    reference.length = length;
    reference.type = type;
    return journal->on_record(journal->record_ctx, type, payload, length,
                              &reference);
}

static int pread_full(int fd, size_t offset, uint8_t *buffer, size_t length)
{
    size_t received = 0u;

    while (received < length) {
        ssize_t result = pread(fd, buffer + received, length - received,
                               (off_t)(offset + received));
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (result == 0)
            return -1;
        received += (size_t)result;
    }
    return 0;
}

static int pwrite_full(int fd, size_t offset, const uint8_t *buffer,
                       size_t length)
{
    size_t written = 0u;

    while (written < length) {
        ssize_t result = pwrite(fd, buffer + written, length - written,
                                (off_t)(offset + written));
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (result == 0)
            return -1;
        written += (size_t)result;
    }
    return 0;
}

static int file_read(void *ctx, size_t offset, uint8_t *buffer, size_t length)
{
    struct file_flash_context *file = ctx;
    return pread_full(file->fd, file->base + offset, buffer, length);
}

static int file_write(void *ctx, size_t offset, const uint8_t *buffer,
                      size_t length)
{
    struct file_flash_context *file = ctx;
    uint8_t current[IO_CHUNK];
    size_t processed = 0u;

    while (processed < length) {
        size_t chunk = length - processed;
        size_t index;

        if (chunk > sizeof(current))
            chunk = sizeof(current);
        if (pread_full(file->fd, file->base + offset + processed, current,
                       chunk) != 0)
            return -1;
        for (index = 0u; index < chunk; index++) {
            if ((current[index] & buffer[processed + index]) !=
                buffer[processed + index])
                return -1;
        }
        if (pwrite_full(file->fd, file->base + offset + processed,
                        buffer + processed, chunk) != 0)
            return -1;
        processed += chunk;
    }
    return fdatasync(file->fd) == 0 ? 0 : -1;
}

static int file_erase(void *ctx, size_t offset, size_t length)
{
    struct file_flash_context *file = ctx;
    uint8_t erased[IO_CHUNK];
    size_t processed = 0u;

    if (offset % NINLIL_FLASH_SECTOR_SIZE != 0u ||
        length % NINLIL_FLASH_SECTOR_SIZE != 0u)
        return -1;
    memset(erased, UINT8_C(0xFF), sizeof(erased));
    while (processed < length) {
        size_t chunk = length - processed;
        if (chunk > sizeof(erased))
            chunk = sizeof(erased);
        if (pwrite_full(file->fd, file->base + offset + processed, erased,
                        chunk) != 0)
            return -1;
        processed += chunk;
    }
    return fdatasync(file->fd) == 0 ? 0 : -1;
}

static int initialize_file(int fd, size_t size)
{
    struct file_flash_context context;

    if (ftruncate(fd, (off_t)size) != 0)
        return -1;
    context.fd = fd;
    context.base = 0u;
    return file_erase(&context, 0u, size);
}

static int fsync_parent(const char *path)
{
    char *copy = strdup(path);
    const char *directory = ".";
    char *slash;
    int fd;
    int rc;

    if (!copy)
        return -1;
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
        return -1;
    }
    rc = fsync(fd);
    (void)close(fd);
    free(copy);
    return rc;
}

int ninlil_journal_open(ninlil_journal **out, const char *location,
                        uint64_t maximum_bytes,
                        ninlil_journal_on_record on_record, void *ctx)
{
    ninlil_journal *journal;
    ninlil_flash_io io, spare;
    struct stat status;
    int created = 0;
    int fd;
    int rc;

    if (!out || !location || location[0] == '\0' || !on_record ||
        maximum_bytes < FILE_FLASH_SIZE)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    size_t logical = maximum_bytes >= 2u * FILE_FLASH_SIZE
                         ? 2u * FILE_FLASH_SIZE
                         : FILE_FLASH_SIZE;
    size_t physical = 2u * logical + NINLIL_FLASH_SELECT_BYTES;
    fd = open(location, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT) {
        fd = open(location, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                  0600);
        if (fd >= 0)
            created = 1;
    }
    if (fd < 0)
        return NINLIL_ERR_IO;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        (void)close(fd);
        return saved_errno == EWOULDBLOCK || saved_errno == EAGAIN
                   ? NINLIL_ERR_BUSY
                   : NINLIL_ERR_IO;
    }
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        (!created && status.st_size != (off_t)logical &&
         status.st_size != (off_t)physical) ||
        (created &&
         (initialize_file(fd, physical) != 0 || fsync_parent(location) != 0))) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }

    journal = calloc(1u, sizeof(*journal));
    if (!journal) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }
    journal->file.fd = fd;
    journal->on_record = on_record;
    journal->record_ctx = ctx;
    memset(&io, 0, sizeof(io));
    io.read = file_read;
    io.write = file_write;
    io.erase = file_erase;
    io.ctx = &journal->file;
    io.size = logical;
    journal->spare_file.fd = fd;
    journal->spare_file.base = logical;
    spare = io;
    spare.ctx = &journal->spare_file;
    spare.size = logical + NINLIL_FLASH_SELECT_BYTES;
    rc = ninlil_flash_bank_open(
        &journal->bank, &io,
        created || status.st_size == (off_t)physical ? &spare : NULL,
        replay_record, journal);
    if (rc != NINLIL_OK) {
        (void)close(fd);
        free(journal);
        return rc;
    }
    *out = journal;
    return NINLIL_OK;
}

int ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                          const uint8_t *payload, uint16_t length,
                          ninlil_journal_ref *reference)
{
    size_t payload_offset;
    int rc;

    if (!journal || journal->bank.poisoned)
        return NINLIL_ERR_INVALID;
    rc = ninlil_flash_store_append_ref(&journal->bank.store, type, payload,
                                       length, &payload_offset);
    if (rc == NINLIL_OK && reference) {
        reference->offset = payload_offset;
        reference->generation = (uint32_t)journal->bank.generation;
        reference->length = length;
        reference->type = type;
    }
    return rc;
}

int ninlil_journal_read(ninlil_journal *journal,
                        const ninlil_journal_ref *reference,
                        uint16_t relative_offset, uint8_t *buffer,
                        uint16_t length)
{
    if (!journal || !reference || journal->bank.poisoned ||
        reference->offset > SIZE_MAX)
        return NINLIL_ERR_INVALID;
    if (reference->generation != journal->bank.generation)
        return NINLIL_ERR_STATE;
    return ninlil_flash_store_read(
        &journal->bank.store, (size_t)reference->offset, reference->length,
        reference->type, relative_offset, buffer, length);
}

int ninlil_journal_usage(ninlil_journal *j, uint64_t *used, uint64_t *capacity)
{
    if (!j || !used || !capacity || j->bank.poisoned || j->bank.store.poisoned)
        return NINLIL_ERR_IO;
    *used = j->bank.store.append_offset;
    *capacity = j->bank.store.io.size;
    return NINLIL_OK;
}
int ninlil_journal_visit(ninlil_journal *j, ninlil_journal_on_record visit,
                         void *ctx)
{
    ninlil_journal scan = {0};
    uint64_t used, capacity;
    if (!visit || ninlil_journal_usage(j, &used, &capacity) != NINLIL_OK)
        return NINLIL_ERR_IO;
    scan.on_record = visit;
    scan.bank.generation = j->bank.generation;
    scan.record_ctx = ctx;
    return ninlil_flash_store_open(&scan.bank.store, &j->bank.store.io,
                                   replay_record, &scan);
}
typedef struct snapshot_context {
    ninlil_journal_snapshot snapshot;
    void *ctx;
    uint64_t generation;
} snapshot_context;
static int snapshot_bridge(void *ctx, ninlil_flash_store *store)
{
    snapshot_context *c = ctx;
    ninlil_journal next = {0};
    int rc;
    next.bank.store = *store;
    next.bank.generation = c->generation;
    rc = c->snapshot(c->ctx, &next);
    *store = next.bank.store;
    return rc;
}
int ninlil_journal_rewrite(ninlil_journal *j, ninlil_journal_snapshot snapshot,
                           void *ctx)
{
    snapshot_context c = {snapshot, ctx, j ? j->bank.generation + 1u : 0u};
    return j && snapshot
               ? ninlil_flash_bank_rewrite(&j->bank, snapshot_bridge, &c)
               : NINLIL_ERR_INVALID;
}

void ninlil_journal_close(ninlil_journal *journal)
{
    if (!journal)
        return;
    if (journal->file.fd >= 0)
        (void)close(journal->file.fd);
    free(journal);
}
