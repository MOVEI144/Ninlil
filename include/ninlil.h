#ifndef NINLIL_H
#define NINLIL_H

#include <stddef.h>
#include <stdint.h>

#define NINLIL_ID_BYTES 16u
#define NINLIL_MAX_PAYLOAD 256u
#define NINLIL_MAX_ENTRIES 1024u
#define NINLIL_MAX_STEP_WORK 1024u

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

typedef enum ninlil_progress {
    NINLIL_PROGRESS_NONE = 0,
    NINLIL_PROGRESS_RECEIVED = 1,
    NINLIL_PROGRESS_STORED = 2,
    NINLIL_PROGRESS_APPLIED = 3
} ninlil_progress;

typedef enum ninlil_outcome {
    NINLIL_OUTCOME_ACTIVE = 0,
    NINLIL_OUTCOME_SATISFIED = 1,
    NINLIL_OUTCOME_EXPIRED = 2,
    NINLIL_OUTCOME_FAILED = 3,
    NINLIL_OUTCOME_CANCELLED = 4,
    NINLIL_OUTCOME_UNKNOWN = 5
} ninlil_outcome;

typedef struct ninlil_id {
    uint8_t bytes[NINLIL_ID_BYTES];
} ninlil_id;

/* send OK means bounded local ownership, never remote/application success.
 * recv returns one packet as 1, empty as 0, or a negative error. */
typedef struct ninlil_link {
    int (*send)(void *ctx, const uint8_t *data, size_t length);
    int (*recv)(void *ctx, uint8_t *buffer, size_t capacity, size_t *length);
    void *ctx;
    size_t max_packet_size;
} ninlil_link;

typedef struct ninlil_random {
    int (*fill)(void *ctx, uint8_t *buffer, size_t length);
    void *ctx;
} ninlil_random;

typedef struct ninlil_config {
    /*
     * M0 compatibility: POSIX callers may continue to set journal_path.
     * New platform ports set journal_location (path or partition label).
     * Supplying both with different values is rejected.
     */
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

typedef struct ninlil_info {
    ninlil_id message_id;
    uint16_t peer;
    uint16_t service;
    uint16_t payload_len;
    ninlil_progress progress;
    ninlil_outcome outcome;
} ninlil_info;

typedef struct ninlil_runtime ninlil_runtime;

int ninlil_open(ninlil_runtime **runtime, const ninlil_config *config);
void ninlil_close(ninlil_runtime *runtime);
int ninlil_submit(ninlil_runtime *runtime, const ninlil_id *idempotency_key,
                  uint16_t target, uint16_t service, const uint8_t *payload,
                  uint16_t payload_len, ninlil_id *message_id);
int ninlil_step(ninlil_runtime *runtime);
int ninlil_receive(ninlil_runtime *runtime, ninlil_inbound *out);
int ninlil_complete(ninlil_runtime *runtime, const ninlil_id *message_id,
                    ninlil_progress progress);
int ninlil_query(ninlil_runtime *runtime, const ninlil_id *message_id,
                 ninlil_info *out);

#endif
