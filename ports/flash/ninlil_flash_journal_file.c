#define _POSIX_C_SOURCE 200809L

#include "ninlil_flash_store.h"
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
};

struct ninlil_journal {
    struct file_flash_context file;
    ninlil_flash_store store;
};

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

static int pwrite_full(int fd,
                       size_t offset,
                       const uint8_t *buffer,
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

static int file_read(void *ctx,
                     size_t offset,
                     uint8_t *buffer,
                     size_t length)
{
    struct file_flash_context *file = ctx;
    return pread_full(file->fd, offset, buffer, length);
}

static int file_write(void *ctx,
                      size_t offset,
                      const uint8_t *buffer,
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
        if (pread_full(file->fd, offset + processed, current, chunk) != 0)
            return -1;
        for (index = 0u; index < chunk; index++) {
            if ((current[index] & buffer[processed + index]) !=
                buffer[processed + index])
                return -1;
        }
        if (pwrite_full(file->fd, offset + processed, buffer + processed,
                        chunk) != 0)
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
        if (pwrite_full(file->fd, offset + processed, erased, chunk) != 0)
            return -1;
        processed += chunk;
    }
    return fdatasync(file->fd) == 0 ? 0 : -1;
}

static int initialize_file(int fd)
{
    struct file_flash_context context;

    if (ftruncate(fd, (off_t)FILE_FLASH_SIZE) != 0)
        return -1;
    context.fd = fd;
    return file_erase(&context, 0u, FILE_FLASH_SIZE);
}

int ninlil_journal_open(ninlil_journal **out,
                        const char *location,
                        ninlil_journal_on_record on_record,
                        void *ctx)
{
    ninlil_journal *journal;
    ninlil_flash_io io;
    struct stat status;
    int created = 0;
    int fd;
    int rc;

    if (!out || !location || location[0] == '\0' || !on_record)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    fd = open(location, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT) {
        fd = open(location,
                  O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
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
        (created ? initialize_file(fd) != 0
                 : status.st_size != (off_t)FILE_FLASH_SIZE)) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }

    journal = calloc(1u, sizeof(*journal));
    if (!journal) {
        (void)close(fd);
        return NINLIL_ERR_IO;
    }
    journal->file.fd = fd;
    memset(&io, 0, sizeof(io));
    io.read = file_read;
    io.write = file_write;
    io.erase = file_erase;
    io.ctx = &journal->file;
    io.size = FILE_FLASH_SIZE;
    rc = ninlil_flash_store_open(&journal->store, &io, on_record, ctx);
    if (rc != NINLIL_OK) {
        (void)close(fd);
        free(journal);
        return rc;
    }
    *out = journal;
    return NINLIL_OK;
}

int ninlil_journal_append(ninlil_journal *journal,
                          uint8_t type,
                          const uint8_t *payload,
                          uint16_t length)
{
    if (!journal)
        return NINLIL_ERR_INVALID;
    return ninlil_flash_store_append(&journal->store, type, payload, length);
}

void ninlil_journal_close(ninlil_journal *journal)
{
    if (!journal)
        return;
    if (journal->file.fd >= 0)
        (void)close(journal->file.fd);
    free(journal);
}
