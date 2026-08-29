#ifndef NINLIL_H
#define NINLIL_H

#include <stddef.h>
#include <stdint.h>

#define NINLIL_API_VERSION 2u
#define NINLIL_ID_BYTES 16u
#define NINLIL_MAX_PAYLOAD 256u
#define NINLIL_MAX_STEP_WORK 1024u
#define NINLIL_MAX_RETRY_INTERVAL_STEPS 1024u
#define NINLIL_JOURNAL_LOCATION_MAX 255u
#define NINLIL_APPLICATION_SERVICE_MIN UINT16_C(0x0100)

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
#define NINLIL_ERR_UNAUTHORIZED (-12)
#define NINLIL_ERR_EXPIRED (-13)
#define NINLIL_ERR_STATE (-14)

#define NINLIL_CAP_APP_SEND (UINT32_C(1) << 0)
#define NINLIL_CAP_APP_RECEIVE (UINT32_C(1) << 1)
#define NINLIL_CAP_POLL_DOWNLINK (UINT32_C(1) << 2)
#define NINLIL_CAP_GROUP_RECEIVE (UINT32_C(1) << 3)
#define NINLIL_CAP_RELAY_CUSTODY (UINT32_C(1) << 4)
#define NINLIL_CAP_GATEWAY_RADIO_HEAD (UINT32_C(1) << 5)
#define NINLIL_CAP_KNOWN_MASK (UINT32_C(0x3F))

#define NINLIL_SERVICE_SEND 0x01u
#define NINLIL_SERVICE_RECEIVE 0x02u
#define NINLIL_SERVICE_BOTH (NINLIL_SERVICE_SEND | NINLIL_SERVICE_RECEIVE)

#define NINLIL_TRAFFIC_MASK(traffic_class)                                     \
    ((uint8_t)(UINT8_C(1) << (unsigned int)(traffic_class)))

typedef enum ninlil_ownership {
    NINLIL_OWNERSHIP_DURABLE = 1,
    NINLIL_OWNERSHIP_VOLATILE = 2
} ninlil_ownership;

typedef enum ninlil_evidence {
    NINLIL_EVIDENCE_NONE = 0,
    NINLIL_EVIDENCE_HOST_ADAPTER_STORED = 1,
    NINLIL_EVIDENCE_GATEWAY_CUSTODY = 2,
    NINLIL_EVIDENCE_REMOTE_RECEIVED = 3,
    NINLIL_EVIDENCE_REMOTE_STORED = 4,
    NINLIL_EVIDENCE_APPLICATION_ACCEPTED = 5
} ninlil_evidence;

typedef enum ninlil_outcome {
    NINLIL_OUTCOME_ACTIVE = 0,
    NINLIL_OUTCOME_SATISFIED = 1,
    NINLIL_OUTCOME_EXPIRED = 2,
    NINLIL_OUTCOME_FAILED = 3,
    NINLIL_OUTCOME_CANCELLED = 4,
    NINLIL_OUTCOME_UNKNOWN = 5
} ninlil_outcome;

typedef enum ninlil_traffic_class {
    NINLIL_TRAFFIC_CRITICAL = 0,
    NINLIL_TRAFFIC_CONTROL = 1,
    NINLIL_TRAFFIC_NORMAL = 2,
    NINLIL_TRAFFIC_BULK = 3
} ninlil_traffic_class;

typedef enum ninlil_role {
    NINLIL_ROLE_BATTERY_LEAF = 1,
    NINLIL_ROLE_POWERED_ENDPOINT = 2,
    NINLIL_ROLE_POWERED_RELAY_CANDIDATE = 3,
    NINLIL_ROLE_SITE_GATEWAY = 4
} ninlil_role;

typedef enum ninlil_time_quality {
    NINLIL_TIME_UNAVAILABLE = 0,
    NINLIL_TIME_RUNTIME_ONLY = 1,
    NINLIL_TIME_RESTART_SAFE = 2
} ninlil_time_quality;

typedef struct ninlil_id {
    uint8_t bytes[NINLIL_ID_BYTES];
} ninlil_id;

typedef struct ninlil_role_profile {
    uint16_t profile_version;
    ninlil_role role;
    uint16_t active_peers;
    uint16_t provisional_peers;
    uint16_t max_outbound;
    uint16_t max_inbound;
    uint16_t max_total_owned;
    uint16_t relay_custody;
    uint16_t dedupe_ids;
    uint16_t service_grants;
    uint16_t critical_reserve;
    uint16_t control_reserve;
    uint16_t shared_slots;
    uint16_t bulk_maximum;
    uint32_t dram_ceiling_bytes;
    uint32_t flash_ceiling_bytes;
} ninlil_role_profile;

typedef struct ninlil_service_grant {
    uint16_t service_id;
    uint16_t maximum_payload_bytes;
    uint16_t maximum_live_messages;
    uint8_t directions;
    uint8_t traffic_class_mask;
} ninlil_service_grant;

typedef struct ninlil_peer_policy {
    ninlil_role role;
    uint32_t capabilities;
    uint64_t membership_epoch;
    uint64_t session_membership_epoch;
    const ninlil_service_grant *grants;
    uint16_t grant_count;
} ninlil_peer_policy;

typedef int (*ninlil_policy_lookup)(void *ctx, uint16_t peer,
                                    ninlil_peer_policy *policy);

/* send returning OK means bounded link acceptance, never remote success.
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

/* now() is synchronous and must leave both outputs unchanged on error. */
typedef struct ninlil_clock {
    int (*now)(void *ctx, uint64_t *unix_ms, ninlil_time_quality *quality);
    void *ctx;
} ninlil_clock;

typedef struct ninlil_config {
    const char *journal_location;
    uint16_t node_id;
    uint32_t retry_interval_steps;
    uint32_t max_work_per_step;
    ninlil_role_profile profile;
    ninlil_link link;
    ninlil_random random;
    ninlil_clock clock;
    ninlil_policy_lookup policy_lookup;
    void *policy_ctx;
} ninlil_config;

/* The payload is borrowed only for the synchronous submit call. Durable
 * submit returns OK only after the complete body and contract are committed. */
typedef struct ninlil_submission {
    uint16_t struct_version;
    ninlil_id idempotency_key;
    uint16_t target;
    uint16_t service;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_traffic_class traffic_class;
    uint64_t absolute_deadline_ms;
    const uint8_t *payload;
    uint16_t payload_len;
} ninlil_submission;

typedef struct ninlil_inbound {
    ninlil_id message_id;
    uint16_t source;
    uint16_t service;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_traffic_class traffic_class;
    uint64_t absolute_deadline_ms;
    uint16_t payload_len;
    uint8_t payload[NINLIL_MAX_PAYLOAD];
} ninlil_inbound;

typedef struct ninlil_info {
    ninlil_id message_id;
    uint16_t peer;
    uint16_t service;
    uint16_t payload_len;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_evidence latest_evidence;
    ninlil_traffic_class traffic_class;
    ninlil_outcome outcome;
    uint64_t absolute_deadline_ms;
    uint8_t remote_boundary_may_have_been_reached;
} ninlil_info;

typedef struct ninlil_runtime ninlil_runtime;

int ninlil_role_profile_standard(ninlil_role role,
                                 ninlil_role_profile *profile);
int ninlil_role_profile_validate(const ninlil_role_profile *profile);
int ninlil_policy_validate(const ninlil_peer_policy *policy,
                           uint16_t grant_limit);
int ninlil_policy_update_validate(const ninlil_peer_policy *older,
                                  const ninlil_peer_policy *newer,
                                  uint16_t grant_limit);

void ninlil_submission_defaults(ninlil_submission *submission);
int ninlil_open(ninlil_runtime **runtime, const ninlil_config *config);
void ninlil_close(ninlil_runtime *runtime);
int ninlil_submit(ninlil_runtime *runtime, const ninlil_submission *submission,
                  ninlil_id *message_id);
int ninlil_step(ninlil_runtime *runtime);
/* receive offers each stored message at most once per boot until explicit
 * acceptance. A crash before acceptance makes it eligible again. */
int ninlil_receive(ninlil_runtime *runtime, ninlil_inbound *out);
/* The Application calls this only after durable adoption or an idempotent
 * commit. It is acceptance evidence, never business-execution success. */
int ninlil_application_accept(ninlil_runtime *runtime,
                              const ninlil_id *message_id);
int ninlil_query(ninlil_runtime *runtime, const ninlil_id *message_id,
                 ninlil_info *out);
/* Cancellation is accepted only before any durable transmission-attempt
 * marker. Missing receipts never make cancellation safe. */
int ninlil_cancel(ninlil_runtime *runtime, const ninlil_id *message_id);
/* The integration owner may record UNKNOWN only after an attempt marker and
 * only when its recovery protocol can no longer determine required evidence. */
int ninlil_mark_unknown(ninlil_runtime *runtime, const ninlil_id *message_id);

#endif
