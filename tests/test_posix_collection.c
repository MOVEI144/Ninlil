#define _POSIX_C_SOURCE 200809L
#include "ninlil.h"
#include "ninlil_journal.h"
#include "test_support.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
int __real_rename(const char *old, const char *next);
int __wrap_rename(const char *old, const char *next);
int __real_fsync(int fd);
int __wrap_fsync(int fd);
static int failure, renamed;
int __wrap_rename(const char *old, const char *next)
{
    int rc;
    if (failure == 1) {
        errno = EIO;
        return -1;
    }
    rc = __real_rename(old, next);
    if (rc == 0) {
        renamed = 1;
        if (failure == 3)
            _exit(73);
    }
    return rc;
}
int __wrap_fsync(int fd)
{
    if (failure == 2 && renamed) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}
typedef struct seen {
    uint8_t value;
    unsigned int count;
} seen;
static int replay(void *ctx, uint8_t type, const uint8_t *data, uint16_t len,
                  const ninlil_journal_ref *ref)
{
    seen *s = ctx;
    (void)ref;
    if (type != 1u || len != 1u)
        return NINLIL_ERR_CORRUPT;
    s->count++;
    s->value = data[0];
    return NINLIL_OK;
}
static int snapshot(void *ctx, ninlil_journal *out)
{
    return ninlil_journal_append(out, 1u, ctx, 1u, NULL);
}
int main(void)
{
    char directory[128], path[256], scratch[262];
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "journal") == 0);
    CHECK(snprintf(scratch, sizeof(scratch), "%s.next", path) > 0);
    for (int mode = 0; mode <= 4; mode++) {
        ninlil_journal *j;
        seen s = {0};
        uint8_t old = 3u, next = 4u, actual;
        ninlil_journal_ref ref;
        failure = renamed = 0;
        CHECK(ninlil_journal_open(&j, path, 4096u, replay, &s) == NINLIL_OK);
        CHECK(ninlil_journal_append(j, 1u, &old, 1u, &ref) == NINLIL_OK);
        if (mode == 0 || mode == 4) {
            CHECK((mode == 0 ? symlink(path, scratch) : link(path, scratch)) ==
                  0);
            CHECK(ninlil_journal_rewrite(j, snapshot, &next) == NINLIL_ERR_IO);
            CHECK(ninlil_journal_read(j, &ref, 0u, &actual, 1u) == NINLIL_OK &&
                  actual == old);
        } else if (mode == 3) {
            pid_t pid = fork();
            int status;
            CHECK(pid >= 0);
            if (!pid) {
                failure = mode;
                (void)ninlil_journal_rewrite(j, snapshot, &next);
                _exit(74);
            }
            CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                  WEXITSTATUS(status) == 73);
        } else {
            failure = mode;
            CHECK(ninlil_journal_rewrite(j, snapshot, &next) == NINLIL_ERR_IO);
        }
        ninlil_journal_close(j);
        failure = renamed = 0;
        s.count = 0u;
        CHECK(ninlil_journal_open(&j, path, 4096u, replay, &s) == NINLIL_OK);
        CHECK(s.count == 1u &&
              s.value == (mode == 2 || mode == 3 ? next : old));
        ninlil_journal_close(j);
        CHECK(unlink(path) == 0);
        if (mode == 0 || mode == 1 || mode == 4)
            CHECK(unlink(scratch) == 0);
    }
    CHECK(rmdir(directory) == 0);
    puts("POSIX scratch symlink/hardlink refusal, failed rename/directory "
         "sync, exit after publication PASS");
    return 0;
}
