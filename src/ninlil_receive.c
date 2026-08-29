#include "ninlil_internal.h"

#include <string.h>

static int payload_matches(ninlil_runtime *runtime,
                           const ninlil_journal_ref *reference,
                           uint16_t payload_offset, const uint8_t *payload,
                           uint16_t payload_len, int *matches)
{
    uint8_t stored[NINLIL_MAX_PAYLOAD];
    int rc;

    *matches = 1;
    if (payload_len == 0u)
        return NINLIL_OK;
    rc = ninlil_read_payload(runtime, reference, payload_offset, stored,
                             payload_len);
    if (rc != NINLIL_OK)
        return rc;
    *matches = memcmp(stored, payload, payload_len) == 0;
    return NINLIL_OK;
}

static int inbound_contract_matches(ninlil_runtime *runtime,
                                    const ninlil_inbound_entry *entry,
                                    const ninlil_wire_data_view *view,
                                    int *matches)
{
    *matches = entry->source == view->source &&
               entry->service == view->service &&
               entry->payload_len == view->payload_length &&
               entry->ownership == view->ownership &&
               entry->required_evidence == view->required_evidence &&
               entry->traffic_class == view->traffic_class &&
               entry->absolute_deadline_ms == view->absolute_deadline_ms;
    return *matches ? payload_matches(runtime, &entry->record_ref,
                                      NINLIL_JRN_IN_HEADER, view->payload,
                                      view->payload_length, matches)
                    : NINLIL_OK;
}

static int archive_contract_matches(ninlil_runtime *runtime,
                                    const ninlil_archive_entry *entry,
                                    const ninlil_wire_data_view *view,
                                    int *matches)
{
    *matches = entry->kind == NINLIL_ARCHIVE_INBOUND &&
               entry->peer == view->source && entry->service == view->service &&
               entry->payload_len == view->payload_length &&
               entry->ownership == view->ownership &&
               entry->required_evidence == view->required_evidence &&
               entry->traffic_class == view->traffic_class &&
               entry->absolute_deadline_ms == view->absolute_deadline_ms;
    return *matches ? payload_matches(runtime, &entry->record_ref,
                                      NINLIL_JRN_IN_HEADER, view->payload,
                                      view->payload_length, matches)
                    : NINLIL_OK;
}

static int queue_rejection(ninlil_runtime *runtime, const ninlil_id *id,
                           uint16_t target, uint8_t status)
{
    uint16_t index;
    ninlil_rejection_entry *entry = NULL;

    for (index = 0u; index < runtime->rejection_capacity; index++) {
        if (runtime->rejections[index].used &&
            ninlil_id_equal(&runtime->rejections[index].message_id, id)) {
            entry = &runtime->rejections[index];
            break;
        }
        if (!entry && !runtime->rejections[index].used)
            entry = &runtime->rejections[index];
    }
    if (!entry) {
        entry = &runtime->rejections[runtime->rejection_cursor];
        runtime->rejection_cursor =
            (uint16_t)((runtime->rejection_cursor + 1u) %
                       runtime->rejection_capacity);
    }
    if (entry->used &&
        (!ninlil_id_equal(&entry->message_id, id) || entry->target != target))
        memset(entry, 0, sizeof(*entry));
    entry->used = 1u;
    entry->message_id = *id;
    entry->target = target;
    entry->status = status;
    entry->pending = 1u;
    return NINLIL_OK;
}

static int authorize_data(ninlil_runtime *runtime,
                          const ninlil_wire_data_view *view,
                          uint16_t live_messages)
{
    return ninlil_authorize(runtime, view->source, view->service,
                            view->payload_length, view->traffic_class,
                            NINLIL_SERVICE_SEND, live_messages);
}

static int handle_duplicate(ninlil_runtime *runtime,
                            const ninlil_wire_data_view *view,
                            ninlil_inbound_entry *inbound,
                            ninlil_archive_entry *archive)
{
    int matches;
    int rc;

    if (inbound) {
        rc = inbound_contract_matches(runtime, inbound, view, &matches);
        if (rc != NINLIL_OK)
            return rc;
        if (!matches)
            return NINLIL_ERR_CONFLICT;
        inbound->need_receipt = 1u;
        return NINLIL_OK;
    }
    rc = archive_contract_matches(runtime, archive, view, &matches);
    if (rc != NINLIL_OK)
        return rc;
    if (!matches)
        return NINLIL_ERR_CONFLICT;
    archive->need_receipt = 1u;
    return NINLIL_OK;
}

static int handle_data(ninlil_runtime *runtime, const uint8_t *packet,
                       size_t length)
{
    ninlil_wire_data_view view;
    ninlil_inbound_entry candidate;
    ninlil_inbound_entry *inbound;
    ninlil_archive_entry *archive;
    ninlil_journal_ref reference;
    int expired;
    int rc;
    uint16_t index;

    if (ninlil_wire_decode_data(packet, length, &view) != NINLIL_OK ||
        view.target != runtime->config.node_id)
        return NINLIL_OK;
    inbound = ninlil_find_inbound(runtime, &view.message_id);
    archive = ninlil_find_archive_id(runtime, &view.message_id);
    if (inbound || archive)
        return handle_duplicate(runtime, &view, inbound, archive);
    if (ninlil_find_outbound(runtime, &view.message_id))
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_deadline_passed(runtime, view.absolute_deadline_ms, &expired);
    if (rc != NINLIL_OK)
        return rc;
    if (expired) {
        (void)queue_rejection(runtime, &view.message_id, view.source,
                              NINLIL_RECEIPT_EXPIRED);
        return NINLIL_OK;
    }
    if (authorize_data(runtime, &view,
                       ninlil_live_service(runtime, view.source, view.service,
                                           NINLIL_SERVICE_SEND)) != NINLIL_OK) {
        (void)queue_rejection(runtime, &view.message_id, view.source,
                              NINLIL_RECEIPT_PERMANENT_REJECTION);
        return NINLIL_OK;
    }
    if (runtime->inbound_live >= runtime->config.profile.max_inbound ||
        !ninlil_total_owned_available(runtime))
        return NINLIL_ERR_CAPACITY;
    for (index = 0u; index < runtime->inbound_capacity; index++) {
        if (!runtime->inbound[index].used)
            break;
    }
    if (index == runtime->inbound_capacity)
        return NINLIL_ERR_CAPACITY;
    memset(&candidate, 0, sizeof(candidate));
    candidate.message_id = view.message_id;
    candidate.source = view.source;
    candidate.service = view.service;
    candidate.payload_len = view.payload_length;
    candidate.ownership = view.ownership;
    candidate.required_evidence = view.required_evidence;
    candidate.traffic_class = view.traffic_class;
    candidate.absolute_deadline_ms = view.absolute_deadline_ms;
    rc = ninlil_log_inbound(runtime, &candidate, view.payload, &reference);
    if (rc != NINLIL_OK)
        return rc;
    candidate.record_ref = reference;
    candidate.used = 1u;
    candidate.need_receipt = 1u;
    runtime->inbound[index] = candidate;
    runtime->inbound_live++;
    return NINLIL_OK;
}

int ninlil_finish_outbound(ninlil_runtime *runtime,
                           ninlil_outbound_entry *entry, ninlil_outcome outcome)
{
    int rc = ninlil_log_terminal(runtime, &entry->message_id, outcome);

    if (rc == NINLIL_OK)
        ninlil_archive_outbound(runtime, entry, outcome);
    return rc;
}

static int handle_receipt(ninlil_runtime *runtime, const uint8_t *packet,
                          size_t length)
{
    ninlil_wire_receipt_view view;
    ninlil_outbound_entry *entry;
    int rc;

    if (ninlil_wire_decode_receipt(packet, length, &view) != NINLIL_OK ||
        view.target != runtime->config.node_id)
        return NINLIL_OK;
    entry = ninlil_find_outbound(runtime, &view.message_id);
    if (!entry || entry->target != view.source)
        return NINLIL_OK;
    if (view.status == NINLIL_RECEIPT_PERMANENT_REJECTION)
        return ninlil_finish_outbound(runtime, entry, NINLIL_OUTCOME_FAILED);
    if (view.status == NINLIL_RECEIPT_EXPIRED)
        return ninlil_finish_outbound(runtime, entry, NINLIL_OUTCOME_EXPIRED);
    if (entry->latest_evidence == view.evidence)
        return NINLIL_OK;
    if (view.evidence < entry->latest_evidence)
        return NINLIL_ERR_CONFLICT;
    rc = ninlil_log_evidence(runtime, &entry->message_id, view.evidence);
    if (rc != NINLIL_OK)
        return rc;
    entry->latest_evidence = view.evidence;
    if (ninlil_evidence_satisfies(entry->required_evidence, view.evidence))
        ninlil_archive_outbound(runtime, entry, NINLIL_OUTCOME_SATISFIED);
    return NINLIL_OK;
}

int ninlil_process_receive(ninlil_runtime *runtime, int *worked)
{
    uint8_t packet[NINLIL_WIRE_PACKET_MAX];
    size_t length = 0u;
    uint8_t type;
    int rc;

    *worked = 0;
    rc = runtime->config.link.recv(runtime->config.link.ctx, packet,
                                   sizeof(packet), &length);
    if (rc == 0)
        return NINLIL_OK;
    *worked = 1;
    if (rc < 0)
        return rc;
    if (rc != 1 || length > sizeof(packet))
        return NINLIL_ERR_INVALID;
    if (ninlil_wire_packet_type(packet, length, &type) != NINLIL_OK)
        return NINLIL_OK;
    return type == NINLIL_WIRE_DATA ? handle_data(runtime, packet, length)
                                    : handle_receipt(runtime, packet, length);
}

static int send_receipt(ninlil_runtime *runtime, uint16_t target,
                        const ninlil_id *message_id, uint8_t status,
                        ninlil_evidence evidence)
{
    uint8_t packet[NINLIL_WIRE_RECEIPT_SIZE];
    size_t length = ninlil_wire_encode_receipt(
        packet, runtime->config.node_id, target, message_id, status, evidence);
    return runtime->config.link.send(runtime->config.link.ctx, packet, length);
}

static int send_inbound_receipt(ninlil_runtime *runtime, int *worked)
{
    uint16_t scanned;

    for (scanned = 0u; scanned < runtime->inbound_capacity; scanned++) {
        uint16_t index =
            (uint16_t)((runtime->inbound_receipt_cursor + scanned) %
                       runtime->inbound_capacity);
        ninlil_inbound_entry *entry = &runtime->inbound[index];
        int rc;

        if (!entry->used || !entry->need_receipt)
            continue;
        runtime->inbound_receipt_cursor =
            (uint16_t)((index + 1u) % runtime->inbound_capacity);
        *worked = 1;
        rc = send_receipt(runtime, entry->source, &entry->message_id,
                          NINLIL_RECEIPT_EVIDENCE,
                          NINLIL_EVIDENCE_REMOTE_STORED);
        if (rc == NINLIL_OK)
            entry->need_receipt = 0u;
        return rc;
    }
    return NINLIL_ERR_EMPTY;
}

static int send_archive_receipt(ninlil_runtime *runtime, int *worked)
{
    uint16_t scanned;

    for (scanned = 0u; scanned < runtime->archive_capacity; scanned++) {
        uint16_t index =
            (uint16_t)((runtime->archive_receipt_cursor + scanned) %
                       runtime->archive_capacity);
        ninlil_archive_entry *entry = &runtime->archive[index];
        int rc;

        if (!entry->used || entry->kind != NINLIL_ARCHIVE_INBOUND ||
            !entry->need_receipt)
            continue;
        runtime->archive_receipt_cursor =
            (uint16_t)((index + 1u) % runtime->archive_capacity);
        *worked = 1;
        rc = send_receipt(runtime, entry->peer, &entry->message_id,
                          NINLIL_RECEIPT_EVIDENCE, entry->latest_evidence);
        if (rc == NINLIL_OK)
            entry->need_receipt = 0u;
        return rc;
    }
    return NINLIL_ERR_EMPTY;
}

static int send_rejection(ninlil_runtime *runtime, int *worked)
{
    uint16_t scanned;

    if (runtime->last_rejection_step != 0u &&
        runtime->step_count - runtime->last_rejection_step <
            NINLIL_REJECTION_INTERVAL_STEPS)
        return NINLIL_ERR_EMPTY;
    for (scanned = 0u; scanned < runtime->rejection_capacity; scanned++) {
        uint16_t index = (uint16_t)((runtime->rejection_cursor + scanned) %
                                    runtime->rejection_capacity);
        ninlil_rejection_entry *entry = &runtime->rejections[index];
        int rc;

        if (!entry->used || !entry->pending)
            continue;
        runtime->rejection_cursor =
            (uint16_t)((index + 1u) % runtime->rejection_capacity);
        *worked = 1;
        rc = send_receipt(runtime, entry->target, &entry->message_id,
                          entry->status, NINLIL_EVIDENCE_NONE);
        if (rc == NINLIL_OK) {
            entry->pending = 0u;
            runtime->last_rejection_step = runtime->step_count;
        }
        return rc;
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_process_receipt_send(ninlil_runtime *runtime, int *worked)
{
    int rc;

    *worked = 0;
    rc = send_inbound_receipt(runtime, worked);
    if (rc != NINLIL_ERR_EMPTY)
        return rc;
    rc = send_archive_receipt(runtime, worked);
    if (rc != NINLIL_ERR_EMPTY)
        return rc;
    rc = send_rejection(runtime, worked);
    return rc == NINLIL_ERR_EMPTY ? NINLIL_OK : rc;
}
