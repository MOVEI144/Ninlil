#ifndef NINLIL_INTERNAL_H
#define NINLIL_INTERNAL_H

#include "ninlil.h"
#include "ninlil_journal.h"
#include "ninlil_wire.h"

#define NINLIL_JRN_OUT_CREATE 1u
#define NINLIL_JRN_OUT_ATTEMPT 2u
#define NINLIL_JRN_OUT_EVIDENCE 3u
#define NINLIL_JRN_OUT_TERMINAL 4u
#define NINLIL_JRN_IN_ACCEPT 5u
#define NINLIL_JRN_IN_APPLICATION_ACCEPT 6u
#define NINLIL_JRN_IN_RECEIPT_HANDOFF 7u
#define NINLIL_JRN_IN_REJECTION 8u
#define NINLIL_JRN_RECORD_VERSION 4u
#define NINLIL_JRN_OUT_HEADER 52u
#define NINLIL_JRN_IN_HEADER 36u
#define NINLIL_JRN_DEADLINE_PRESENT 0x01u
#define NINLIL_REJECTION_INTERVAL_STEPS 4u
#define NINLIL_STEP_PHASES 3u
#define NINLIL_RECEIPT_CLASS_INBOUND 0u
#define NINLIL_RECEIPT_CLASS_ARCHIVE 1u
#define NINLIL_RECEIPT_CLASS_REJECTION 2u
#define NINLIL_RECEIPT_CLASSES 3u
#define NINLIL_TRAFFIC_CLASS_COUNT 4u
#define NINLIL_SCHEDULE_SLOTS 16u

#define NINLIL_ARCHIVE_OUTBOUND 1u
#define NINLIL_ARCHIVE_INBOUND 2u
#define NINLIL_ARCHIVE_SLOT_NONE UINT16_MAX

typedef struct ninlil_outbound_entry {
    ninlil_id message_id;
    ninlil_id idempotency_key;
    ninlil_journal_ref record_ref;
    uint64_t absolute_deadline_ms;
    uint64_t last_sent_step;
    uint16_t target;
    uint16_t service;
    uint16_t payload_len;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_evidence latest_evidence;
    ninlil_traffic_class traffic_class;
    uint8_t used;
    uint8_t attempted;
} ninlil_outbound_entry;

typedef struct ninlil_inbound_entry {
    ninlil_id message_id;
    ninlil_journal_ref record_ref;
    uint64_t absolute_deadline_ms;
    uint16_t source;
    uint16_t service;
    uint16_t payload_len;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_traffic_class traffic_class;
    uint8_t used;
    uint8_t handed;
    uint8_t need_receipt;
    uint8_t receipt_handoff_committed;
    uint8_t receipt_handoff_commit_pending;
} ninlil_inbound_entry;

typedef struct ninlil_archive_entry {
    ninlil_id message_id;
    ninlil_id idempotency_key;
    ninlil_journal_ref record_ref;
    uint64_t absolute_deadline_ms;
    uint16_t peer;
    uint16_t service;
    uint16_t payload_len;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_evidence latest_evidence;
    ninlil_traffic_class traffic_class;
    ninlil_outcome outcome;
    uint8_t kind;
    uint8_t used;
    uint8_t attempted;
    uint8_t need_receipt;
    uint8_t receipt_handoff_committed;
    uint8_t receipt_handoff_commit_pending;
} ninlil_archive_entry;

typedef struct ninlil_rejection_entry {
    ninlil_id message_id;
    ninlil_journal_ref record_ref;
    uint64_t absolute_deadline_ms;
    uint16_t target;
    uint16_t service;
    uint16_t payload_len;
    ninlil_ownership ownership;
    ninlil_evidence required_evidence;
    ninlil_traffic_class traffic_class;
    uint8_t status;
    uint8_t pending;
    uint8_t durable;
    uint8_t used;
} ninlil_rejection_entry;

struct ninlil_runtime {
    ninlil_config config;
    ninlil_journal *journal;
    ninlil_outbound_entry *outbound;
    ninlil_inbound_entry *inbound;
    ninlil_archive_entry *archive;
    ninlil_rejection_entry *rejections;
    uint16_t outbound_capacity;
    uint16_t inbound_capacity;
    uint16_t archive_capacity;
    uint16_t rejection_capacity;
    uint16_t outbound_live;
    uint16_t inbound_live;
    uint16_t live_by_class[NINLIL_TRAFFIC_CLASS_COUNT];
    uint16_t bulk_live;
    uint16_t outbound_cursor[NINLIL_TRAFFIC_CLASS_COUNT];
    uint16_t inbound_receipt_cursor;
    uint16_t archive_receipt_cursor;
    uint16_t rejection_cursor;
    uint16_t archive_replace_cursor;
    uint8_t receipt_class_cursor;
    uint8_t schedule_cursor;
    uint8_t phase_cursor;
    uint8_t replaying;
    uint64_t step_count;
    uint64_t last_rejection_step;
    int fatal_error;
};

int ninlil_id_equal(const ninlil_id *left, const ninlil_id *right);
int ninlil_evidence_satisfies(ninlil_evidence required, ninlil_evidence actual);
int ninlil_traffic_class_valid(ninlil_traffic_class traffic_class);
int ninlil_clock_now(ninlil_runtime *runtime, uint64_t *now,
                     ninlil_time_quality *quality);
int ninlil_deadline_passed(ninlil_runtime *runtime, uint64_t deadline,
                           int *passed);
int ninlil_append_record(ninlil_runtime *runtime, uint8_t type,
                         const uint8_t *payload, uint16_t length,
                         ninlil_journal_ref *reference);
int ninlil_read_payload(ninlil_runtime *runtime,
                        const ninlil_journal_ref *reference,
                        uint16_t payload_offset, uint8_t *payload,
                        uint16_t payload_len);

ninlil_outbound_entry *ninlil_find_outbound(ninlil_runtime *runtime,
                                            const ninlil_id *id);
ninlil_outbound_entry *ninlil_find_idempotency(ninlil_runtime *runtime,
                                               const ninlil_id *key);
ninlil_inbound_entry *ninlil_find_inbound(ninlil_runtime *runtime,
                                          const ninlil_id *id);
ninlil_archive_entry *ninlil_find_archive_id(ninlil_runtime *runtime,
                                             const ninlil_id *id);
ninlil_archive_entry *ninlil_find_archive_key(ninlil_runtime *runtime,
                                              const ninlil_id *key);
ninlil_rejection_entry *ninlil_find_rejection(ninlil_runtime *runtime,
                                              const ninlil_id *id);
int ninlil_id_in_use(ninlil_runtime *runtime, const ninlil_id *id);

int ninlil_replay_record(void *ctx, uint8_t type, const uint8_t *payload,
                         uint16_t length, const ninlil_journal_ref *reference);
int ninlil_log_outbound(ninlil_runtime *runtime,
                        const ninlil_outbound_entry *entry,
                        const uint8_t *payload, ninlil_journal_ref *reference);
int ninlil_log_inbound(ninlil_runtime *runtime,
                       const ninlil_inbound_entry *entry,
                       const uint8_t *payload, ninlil_journal_ref *reference);
int ninlil_log_rejection(ninlil_runtime *runtime,
                         const ninlil_rejection_entry *entry,
                         const uint8_t *payload, ninlil_journal_ref *reference);
int ninlil_log_id(ninlil_runtime *runtime, uint8_t type, const ninlil_id *id);
int ninlil_log_evidence(ninlil_runtime *runtime, const ninlil_id *id,
                        ninlil_evidence evidence, uint16_t archive_slot);
int ninlil_log_terminal(ninlil_runtime *runtime, const ninlil_id *id,
                        ninlil_outcome outcome, uint16_t archive_slot);
int ninlil_log_application_accept(ninlil_runtime *runtime, const ninlil_id *id,
                                  uint16_t archive_slot);
int ninlil_archive_admission(const ninlil_runtime *runtime, uint16_t *slot);
int ninlil_archive_outbound(ninlil_runtime *runtime,
                            ninlil_outbound_entry *entry,
                            ninlil_outcome outcome, uint16_t archive_slot);
int ninlil_archive_inbound(ninlil_runtime *runtime, ninlil_inbound_entry *entry,
                           ninlil_evidence evidence, uint8_t need_receipt,
                           uint16_t archive_slot);
int ninlil_outbound_admission(const ninlil_runtime *runtime,
                              ninlil_traffic_class traffic_class);
int ninlil_total_owned_available(const ninlil_runtime *runtime);

int ninlil_authorize(ninlil_runtime *runtime, uint16_t peer, uint16_t service,
                     uint16_t payload_len, ninlil_traffic_class traffic_class,
                     uint8_t direction, uint16_t live_messages);
uint16_t ninlil_live_service(const ninlil_runtime *runtime, uint16_t peer,
                             uint16_t service, uint8_t direction);

int ninlil_process_receive(ninlil_runtime *runtime, int *worked);
int ninlil_process_receipt_send(ninlil_runtime *runtime, int *worked);
int ninlil_process_outbound(ninlil_runtime *runtime, int *worked);
int ninlil_expire_outbound(ninlil_runtime *runtime, int *worked);
int ninlil_finish_outbound(ninlil_runtime *runtime,
                           ninlil_outbound_entry *entry,
                           ninlil_outcome outcome);

#endif
