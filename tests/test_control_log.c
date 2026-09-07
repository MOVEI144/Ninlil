#define _POSIX_C_SOURCE 200809L
#include "ninlil_control_log.h"
#include "test_support.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check %d: %s\n", __LINE__, #x);                   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct replayed {
    ninlil_relay_record packet;
    size_t count;
} replayed;
static int replay_relay(void *ctx, const ninlil_relay_record *p)
{
    replayed *r = ctx;
    r->packet = *p;
    r->count++;
    return NINLIL_OK;
}

int main(void)
{
    char directory[256], path[320], unused[320];
    ninlil_control_log *log = NULL;
    ninlil_relay_record packet;
    ninlil_control_replay callbacks;
    replayed result;
    uint8_t bytes[4096];
    int fd;
    ssize_t read_count;
    size_t i, found = 0u;
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "control.journal") ==
          0);
    CHECK(test_make_path(unused, sizeof(unused), directory, "unused.journal") ==
          0);
    memset(&result, 0, sizeof(result));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.relay = replay_relay;
    callbacks.ctx = &result;
    CHECK(ninlil_control_log_open(&log, path, 128u * 1024u, callbacks) ==
          NINLIL_OK);
    memset(&packet, 0, sizeof(packet));
    packet.path.count = 3u;
    packet.path.nodes[0] = 1u;
    packet.path.nodes[1] = 2u;
    packet.path.nodes[2] = 3u;
    packet.route_epoch = 1u;
    packet.packet_id[0] = 3u;
    packet.length = 64u;
    memset(packet.ciphertext, 0xA5, packet.length);
    CHECK(ninlil_control_log_relay(log, &packet) == NINLIL_OK);
    CHECK(ninlil_control_log_verify_relay(log, &packet) == NINLIL_OK);
    ninlil_control_log_close(log);
    log = NULL;
    CHECK(ninlil_control_log_open(&log, path, 128u * 1024u, callbacks) ==
          NINLIL_OK);
    CHECK(result.count == 1u && result.packet.length == packet.length);
    CHECK(ninlil_control_log_verify_relay(log, &packet) == NINLIL_OK);
    fd = open(path, O_RDWR);
    CHECK(fd >= 0);
    read_count = pread(fd, bytes, sizeof(bytes), 0);
    CHECK(read_count > 64);
    // Locate the explicit test ciphertext in either POSIX or raw-Flash
    // envelope.
    for (i = 0u; i + 64u <= (size_t)read_count; i++)
        if (memcmp(bytes + i, packet.ciphertext, 64u) == 0) {
            found = i;
            break;
        }
    CHECK(found > 0u);
    bytes[found] ^= 1u;
    CHECK(pwrite(fd, bytes + found, 1u, (off_t)found) == 1);
    CHECK(fsync(fd) == 0);
    CHECK(close(fd) == 0);
    CHECK(ninlil_control_log_verify_relay(log, &packet) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_control_log_relay(log, &packet) != NINLIL_OK);
    ninlil_control_log_close(log);
    log = NULL;
    CHECK(ninlil_control_log_open(&log, path, 128u * 1024u, callbacks) ==
          NINLIL_ERR_CORRUPT);
    test_remove_directory(directory, path, unused);
    puts("real control journal replay/reference corruption/fail-closed PASS");
    return 0;
}
