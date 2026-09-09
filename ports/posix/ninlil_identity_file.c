#define _POSIX_C_SOURCE 200809L
#include "ninlil_identity_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int private_file(int fd, off_t size)
{
    struct stat s;
    return fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && s.st_uid == geteuid() &&
           s.st_nlink == 1 && (s.st_mode & 0077) == 0 &&
           (size < 0 || s.st_size == size);
}

static int initialized(int fd)
{
    uint8_t marker;
    if (private_file(fd, 0))
        return 0;
    return private_file(fd, 1) && pread(fd, &marker, 1u, 0) == 1 &&
                   marker == 'P'
               ? 1
               : -1;
}

static int read_record(void *ctx, uint8_t record[NINLIL_IDENTITY_RECORD_SIZE])
{
    ninlil_identity_file *f = ctx;
    size_t at = 0u;
    unsigned int interrupts = 0u;
    int fd, rc = NINLIL_OK;
    int provisioned;
    if (!f || f->lock_fd < 0 || f->directory_fd < 0)
        return NINLIL_ERR_STATE;
    provisioned = initialized(f->lock_fd);
    if (provisioned < 0)
        return NINLIL_ERR_CORRUPT;
    fd = openat(f->directory_fd, "identity.bin",
                O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return errno != ENOENT ? NINLIL_ERR_IO
               : provisioned   ? NINLIL_ERR_CORRUPT
                               : NINLIL_ERR_EMPTY;
    if (!provisioned || !private_file(fd, NINLIL_IDENTITY_RECORD_SIZE))
        rc = NINLIL_ERR_CORRUPT;
    while (rc == NINLIL_OK && at < NINLIL_IDENTITY_RECORD_SIZE) {
        ssize_t got = read(fd, record + at, NINLIL_IDENTITY_RECORD_SIZE - at);
        if (got > 0)
            at += (size_t)got;
        else if (got == 0 || errno != EINTR || interrupts++ >= 8u)
            rc = NINLIL_ERR_IO;
    }
    if (close(fd) != 0)
        rc = NINLIL_ERR_IO;
    return rc;
}

static int commit_record(void *ctx,
                         const uint8_t record[NINLIL_IDENTITY_RECORD_SIZE])
{
    ninlil_identity_file *f = ctx;
    size_t at = 0u;
    unsigned int interrupts = 0u;
    int fd, rc = NINLIL_OK;
    if (!f || f->lock_fd < 0 || f->directory_fd < 0)
        return NINLIL_ERR_STATE;
    fd = openat(f->directory_fd, "identity.next",
                O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return NINLIL_ERR_IO;
    if (!private_file(fd, -1) || ftruncate(fd, 0) != 0)
        rc = NINLIL_ERR_IO;
    while (rc == NINLIL_OK && at < NINLIL_IDENTITY_RECORD_SIZE) {
        ssize_t wrote =
            write(fd, record + at, NINLIL_IDENTITY_RECORD_SIZE - at);
        if (wrote > 0)
            at += (size_t)wrote;
        else if (wrote == 0 || errno != EINTR || interrupts++ >= 8u)
            rc = NINLIL_ERR_IO;
    }
    if (rc == NINLIL_OK && fsync(fd) != 0)
        rc = NINLIL_ERR_IO;
    if (close(fd) != 0)
        rc = NINLIL_ERR_IO;
    /* Persist the fact of provisioning before the identity can be used. A
     * missing key after this point is corruption, never automatic reprovision.
     */
    if (rc == NINLIL_OK &&
        (initialized(f->lock_fd) < 0 || pwrite(f->lock_fd, "P", 1u, 0) != 1 ||
         fsync(f->lock_fd) != 0 || fsync(f->directory_fd) != 0))
        rc = NINLIL_ERR_IO;
    if (rc == NINLIL_OK && renameat(f->directory_fd, "identity.next",
                                    f->directory_fd, "identity.bin") != 0)
        rc = NINLIL_ERR_IO;
    if (rc == NINLIL_OK && fsync(f->directory_fd) != 0)
        rc = NINLIL_ERR_IO;
    return rc;
}

void ninlil_identity_file_close(ninlil_identity_file *f)
{
    if (f) {
        if (f->lock_fd >= 0)
            (void)close(f->lock_fd);
        if (f->directory_fd >= 0)
            (void)close(f->directory_fd);
        f->lock_fd = f->directory_fd = -1;
    }
}

int ninlil_identity_file_open(ninlil_identity_file *f, const char *path,
                              ninlil_identity_io *io)
{
    struct stat s;
    int rc = NINLIL_ERR_IO;
    if (!f || !path || !io)
        return NINLIL_ERR_INVALID;
    *io = (ninlil_identity_io){0};
    f->directory_fd = f->lock_fd = -1;
    if (mkdir(path, 0700) != 0 && errno != EEXIST)
        return NINLIL_ERR_IO;
    f->directory_fd =
        open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (f->directory_fd < 0 || fstat(f->directory_fd, &s) != 0 ||
        s.st_uid != geteuid() || (s.st_mode & 0077) != 0)
        goto fail;
    {
        int parent =
            openat(f->directory_fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        int result = parent >= 0 ? fsync(parent) : -1;
        if (parent >= 0 && close(parent) != 0)
            result = -1;
        if (result != 0)
            goto fail;
    }
    f->lock_fd = openat(f->directory_fd, ".identity.lock",
                        O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (f->lock_fd < 0 || !private_file(f->lock_fd, -1))
        goto fail;
    if (flock(f->lock_fd, LOCK_EX | LOCK_NB) != 0) {
        rc = NINLIL_ERR_BUSY;
        goto fail;
    }
    if (initialized(f->lock_fd) < 0) {
        rc = NINLIL_ERR_CORRUPT;
        goto fail;
    }
    io->read = read_record;
    io->commit = commit_record;
    io->ctx = f;
    return NINLIL_OK;
fail:
    ninlil_identity_file_close(f);
    return rc;
}
