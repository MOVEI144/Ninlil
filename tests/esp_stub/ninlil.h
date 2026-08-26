#ifndef NINLIL_H
#define NINLIL_H
#include <stddef.h>
#include <stdint.h>
#define NINLIL_OK 0
#define NINLIL_ERR_INVALID (-1)
#define NINLIL_ERR_IO (-2)
#define NINLIL_ERR_CORRUPT (-3)
#define NINLIL_ERR_CAPACITY (-4)
#define NINLIL_ERR_CONFLICT (-5)
#define NINLIL_ERR_NOT_FOUND (-6)
#define NINLIL_ERR_EMPTY (-7)
#define NINLIL_ERR_BUSY (-8)
#define NINLIL_ERR_TOO_LARGE (-9)
#define NINLIL_ERR_TIMEOUT (-10)
#define NINLIL_ERR_FAULT (-11)
#define NINLIL_PROGRESS_APPLIED 3
#define NINLIL_MAX_PAYLOAD 256
typedef int ninlil_progress;
typedef struct ninlil_id {
    uint8_t bytes[16];
} ninlil_id;
typedef struct ninlil_link {
    int (*send)(void *, const uint8_t *, size_t);
    int (*recv)(void *, uint8_t *, size_t, size_t *);
    void *ctx;
    size_t max_packet_size;
} ninlil_link;
typedef struct ninlil_random {
    int (*fill)(void *, uint8_t *, size_t);
    void *ctx;
} ninlil_random;
typedef struct ninlil_config {
    const char *journal_path;
    const char *journal_location;
    uint16_t node_id;
    uint32_t max_outbound;
    uint32_t max_inbound;
    uint32_t retry_interval_steps;
    uint32_t max_work_per_step;
    ninlil_link link;
    ninlil_random random;
} ninlil_config;
typedef struct ninlil_inbound {
    ninlil_id message_id;
    uint16_t source;
    uint16_t service;
    uint16_t payload_len;
    uint8_t payload[NINLIL_MAX_PAYLOAD];
} ninlil_inbound;
typedef struct ninlil_runtime ninlil_runtime;
int ninlil_open(ninlil_runtime **runtime, const ninlil_config *config);
int ninlil_step(ninlil_runtime *runtime);
int ninlil_receive(ninlil_runtime *runtime, ninlil_inbound *inbound);
int ninlil_complete(ninlil_runtime *runtime, const ninlil_id *id,
                    ninlil_progress progress);
int ninlil_submit(ninlil_runtime *runtime, const ninlil_id *key,
                  uint16_t target, uint16_t service, const uint8_t *payload,
                  uint16_t payload_length, ninlil_id *message_id);
#endif
