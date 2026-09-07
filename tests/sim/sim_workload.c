#define _POSIX_C_SOURCE 200809L
#include "sim.h"
#include "test_support.h"

#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#define SERVICE UINT16_C(0x0100)

static int policy_lookup(void *ctx, uint16_t peer, ninlil_peer_policy *policy)
{
    sim_node *node = ctx;
    sim_network *network = node->network;
    static const ninlil_service_grant grant = {
        .service_id = SERVICE,
        .maximum_payload_bytes = SIM_MTU - 40u,
        .maximum_live_messages = 128u,
        .directions = NINLIL_SERVICE_BOTH,
        .traffic_class_mask = UINT8_C(0x0F)};
    if (peer == 0u || peer > network->manifest.nodes || peer == node->id ||
        (node->id != 1u && peer != 1u))
        return NINLIL_ERR_UNAUTHORIZED;
    memset(policy, 0, sizeof(*policy));
    policy->role =
        peer == 1u ? NINLIL_ROLE_SITE_GATEWAY : NINLIL_ROLE_POWERED_ENDPOINT;
    policy->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    /* Synthetic pre-authorized peers exercise P0 policy, not authentication. */
    policy->membership_epoch = 1u;
    policy->session_membership_epoch = 1u;
    policy->grants = &grant;
    policy->grant_count = 1u;
    return NINLIL_OK;
}

static int clock_now(void *ctx, uint64_t *unix_ms, ninlil_time_quality *quality)
{
    sim_node *node = ctx;
    sim_network *network = node->network;
    *unix_ms = network->now_us / 1000u;
    *quality = NINLIL_TIME_RUNTIME_ONLY;
    return NINLIL_OK;
}

static int open_node(sim_node *node)
{
    ninlil_config config = {0};
    config.journal_location = node->path;
    config.node_id = node->id;
    config.retry_interval_steps = 100u;
    config.max_work_per_step = 8u;
    config.link = node->link;
    config.random = (ninlil_random){test_rng_fill, &node->rng};
    config.clock = (ninlil_clock){clock_now, node};
    config.policy_lookup = policy_lookup;
    config.policy_ctx = node;
    if (ninlil_role_profile_standard(node->id == 1u
                                         ? NINLIL_ROLE_SITE_GATEWAY
                                         : NINLIL_ROLE_POWERED_ENDPOINT,
                                     &config.profile) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    return ninlil_open(&node->runtime, &config);
}

static void payload_for(const sim_message *message, uint8_t *payload,
                        size_t length)
{
    memset(payload, (int)message->sequence, length);
    payload[0] = (uint8_t)message->source;
    payload[1] = (uint8_t)message->target;
    payload[2] = (uint8_t)message->sequence;
    payload[3] = UINT8_C(0xA5);
}

static int submit(sim_run *run, sim_message *message, ninlil_id *id)
{
    ninlil_submission request;
    uint8_t payload[SIM_MTU];
    payload_for(message, payload, run->network.manifest.payload_bytes);
    ninlil_submission_defaults(&request);
    request.idempotency_key = message->key;
    request.target = message->target;
    request.service = SERVICE;
    request.payload = payload;
    request.payload_len = (uint16_t)run->network.manifest.payload_bytes;
    return ninlil_submit(run->network.nodes[message->source - 1u].runtime,
                         &request, id);
}

int sim_run_open(sim_run *run, const sim_manifest *manifest)
{
    uint32_t node, sequence, direction;
    if (!run || sim_manifest_validate(manifest) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    memset(run, 0, sizeof(*run));
    if (sim_network_init(&run->network, manifest) != NINLIL_OK ||
        test_make_directory(run->directory, sizeof(run->directory)) != 0)
        return NINLIL_ERR_IO;
    for (node = 0u; node < manifest->nodes; node++) {
        char name[32];
        sim_node *entry = &run->network.nodes[node];
        int rc;
        (void)snprintf(name, sizeof(name), "node-%u.journal", node + 1u);
        if (test_make_path(entry->path, sizeof(entry->path), run->directory,
                           name) != 0)
            return NINLIL_ERR_IO;
        rc = open_node(entry);
        if (rc != NINLIL_OK)
            return rc;
    }
    for (sequence = 0u; sequence < manifest->burst; sequence++)
        for (node = 2u; node <= manifest->nodes; node++)
            for (direction = 0u; direction < 2u; direction++) {
                sim_message *message = &run->messages[run->count++];
                message->source = direction == 0u ? 1u : (uint16_t)node;
                message->target = direction == 0u ? (uint16_t)node : 1u;
                message->sequence = (uint16_t)sequence;
                message->key.bytes[0] = (uint8_t)message->source;
                message->key.bytes[1] = (uint8_t)message->target;
                message->key.bytes[2] = (uint8_t)sequence;
                message->key.bytes[3] = UINT8_C(0x5A);
            }
    return NINLIL_OK;
}

int sim_run_reopen(sim_run *run, uint32_t node)
{
    sim_node *entry;
    size_t index;
    int rc;
    if (node == 0u || node > run->network.manifest.nodes)
        return NINLIL_ERR_INVALID;
    entry = &run->network.nodes[node - 1u];
    sim_node_online(entry, 0);
    ninlil_close(entry->runtime);
    entry->runtime = NULL;
    rc = open_node(entry);
    if (rc != NINLIL_OK)
        return rc;
    entry->boot++;
    for (index = 0u; index < run->count; index++) {
        sim_message *message = &run->messages[index];
        ninlil_id id;
        if (!message->admitted || message->source != node)
            continue;
        rc = submit(run, message, &id);
        if (rc != NINLIL_OK ||
            memcmp(id.bytes, message->id.bytes, NINLIL_ID_BYTES) != 0)
            return NINLIL_ERR_CORRUPT;
        run->reoffers++;
    }
    sim_node_online(entry, 1);
    run->restarts++;
    return NINLIL_OK;
}

static int admit_due(sim_run *run)
{
    size_t index;
    for (index = 0u; index < run->count; index++) {
        sim_message *message = &run->messages[index];
        int rc;
        if (message->admitted || message->rejected ||
            !run->network.nodes[message->source - 1u].online ||
            run->network.now_us < (uint64_t)message->sequence *
                                      run->network.manifest.interval_ms * 1000u)
            continue;
        run->offered++;
        rc = submit(run, message, &message->id);
        if (rc == NINLIL_ERR_CAPACITY) {
            message->rejected = 1;
            run->rejected++;
        } else if (rc == NINLIL_OK) {
            message->admitted = 1;
            message->admitted_us = run->network.now_us;
            run->admitted++;
        } else {
            return rc;
        }
    }
    return NINLIL_OK;
}

static int observe_inbox(sim_run *run, sim_node *node)
{
    size_t work;
    for (work = 0u; work < SIM_MESSAGES; work++) {
        ninlil_inbound inbound;
        size_t index;
        uint8_t expected[SIM_MTU];
        int rc = ninlil_receive(node->runtime, &inbound);
        if (rc == NINLIL_ERR_EMPTY)
            return NINLIL_OK;
        if (rc != NINLIL_OK)
            return rc;
        for (index = 0u; index < run->count; index++) {
            sim_message *message = &run->messages[index];
            if (!message->admitted ||
                memcmp(inbound.message_id.bytes, message->id.bytes,
                       NINLIL_ID_BYTES) != 0)
                continue;
            payload_for(message, expected, run->network.manifest.payload_bytes);
            if (message->target != node->id ||
                message->source != inbound.source ||
                inbound.service != SERVICE ||
                inbound.required_evidence != NINLIL_EVIDENCE_REMOTE_STORED ||
                inbound.payload_len != run->network.manifest.payload_bytes ||
                memcmp(expected, inbound.payload, inbound.payload_len) != 0)
                return NINLIL_ERR_CORRUPT;
            if (message->offers != 0u && message->last_offer_boot == node->boot)
                return NINLIL_ERR_CORRUPT;
            message->last_offer_boot = node->boot;
            if (message->offers++ == 0u)
                message->first_offer_us = run->network.now_us;
            /* Intentionally no Application acceptance: test store-only success
             * and at-least-once re-offer after a runtime restart. */
            break;
        }
        if (index == run->count)
            return NINLIL_ERR_CORRUPT;
    }
    return NINLIL_ERR_CAPACITY;
}

static int observe_outbox(sim_run *run)
{
    size_t index;
    for (index = 0u; index < run->count; index++) {
        sim_message *message = &run->messages[index];
        ninlil_info info;
        int rc;
        if (!message->admitted)
            continue;
        rc = ninlil_query(run->network.nodes[message->source - 1u].runtime,
                          &message->id, &info);
        if (rc != NINLIL_OK)
            return rc;
        if (info.peer != message->target ||
            info.required_evidence != NINLIL_EVIDENCE_REMOTE_STORED ||
            (info.outcome != NINLIL_OUTCOME_ACTIVE &&
             info.outcome != NINLIL_OUTCOME_SATISFIED) ||
            (message->satisfied && info.outcome != NINLIL_OUTCOME_SATISFIED))
            return NINLIL_ERR_CORRUPT;
        if (info.outcome == NINLIL_OUTCOME_SATISFIED) {
            if (info.latest_evidence != NINLIL_EVIDENCE_REMOTE_STORED ||
                message->offers == 0u)
                return NINLIL_ERR_CORRUPT;
            if (!message->satisfied)
                message->satisfied_us = run->network.now_us;
            message->satisfied = 1;
        }
    }
    return NINLIL_OK;
}

static void isolation_checkpoint(sim_run *run)
{
    size_t index;
    const sim_manifest *m = &run->network.manifest;
    if (run->checkpoint_done || m->offline_node == 0u ||
        run->network.now_us != (uint64_t)m->offline_end_ms * 1000u)
        return;
    for (index = 0u; index < run->count; index++) {
        const sim_message *message = &run->messages[index];
        if (message->source == m->offline_node ||
            message->target == m->offline_node)
            run->isolated_active +=
                (uint32_t)(message->admitted && !message->satisfied);
        else
            run->healthy_satisfied += (uint32_t)message->satisfied;
    }
    run->checkpoint_done = 1;
}

int sim_run_tick(sim_run *run)
{
    const sim_manifest *m = &run->network.manifest;
    uint32_t index;
    int rc;
    isolation_checkpoint(run);
    if (m->restart_node != 0u &&
        run->network.now_us == (uint64_t)m->restart_ms * 1000u) {
        rc = sim_run_reopen(run, m->restart_node);
        if (rc != NINLIL_OK)
            return rc;
    }
    for (index = 0u; index < m->nodes; index++) {
        int offline =
            index + 1u == m->offline_node &&
            run->network.now_us >= (uint64_t)m->offline_start_ms * 1000u &&
            run->network.now_us < (uint64_t)m->offline_end_ms * 1000u;
        sim_node_online(&run->network.nodes[index], !offline);
    }
    sim_network_advance(&run->network);
    rc = admit_due(run);
    if (rc != NINLIL_OK)
        return rc;
    for (index = 0u; index < m->nodes; index++) {
        sim_node *node = &run->network.nodes[index];
        if (!node->online)
            continue;
        rc = ninlil_step(node->runtime);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_BUSY &&
            rc != NINLIL_ERR_CAPACITY)
            return rc;
        rc = observe_inbox(run, node);
        if (rc != NINLIL_OK)
            return rc;
    }
    rc = observe_outbox(run);
    run->network.now_us += SIM_TICK_US;
    return rc;
}

void sim_run_close(sim_run *run)
{
    uint32_t index;
    for (index = 0u; index < run->network.manifest.nodes; index++) {
        sim_node *node = &run->network.nodes[index];
        ninlil_close(node->runtime);
        node->runtime = NULL;
        if (node->path[0] != '\0')
            (void)unlink(node->path);
    }
    if (run->directory[0] != '\0')
        (void)rmdir(run->directory);
}

int sim_run_finish(sim_run *run, FILE *report)
{
    size_t index;
    uint32_t satisfied = 0u, on_time = 0u, offered = 0u;
    uint64_t maximum = 0u;
    isolation_checkpoint(run);
    if (sim_manifest_write(report, &run->network.manifest) != NINLIL_OK)
        return NINLIL_ERR_IO;
    fprintf(report,
            "model=static-star-v1 seed=%u nodes=%u sf=%u slot_us=%" PRIu64
            " duration_ms=%u\n",
            run->network.manifest.seed, run->network.manifest.nodes,
            run->network.manifest.sf, run->network.slot_us,
            run->network.manifest.duration_ms);
    fprintf(
        report,
        "source,target,sequence,admitted,outcome,offers,admitted_us,"
        "first_offer_us,evidence_us,latency_us,censored_age_us,message_id\n");
    for (index = 0u; index < run->count; index++) {
        const sim_message *message = &run->messages[index];
        uint64_t latency = message->satisfied
                               ? message->satisfied_us - message->admitted_us
                               : 0u;
        size_t byte;
        char first_offer[32] = "NA", evidence[32] = "NA", elapsed[32] = "NA";
        uint64_t censored = message->admitted && !message->satisfied
                                ? run->network.now_us - message->admitted_us
                                : 0u;
        if (message->offers != 0u)
            (void)snprintf(first_offer, sizeof(first_offer), "%" PRIu64,
                           message->first_offer_us);
        if (message->satisfied) {
            (void)snprintf(evidence, sizeof(evidence), "%" PRIu64,
                           message->satisfied_us);
            (void)snprintf(elapsed, sizeof(elapsed), "%" PRIu64, latency);
        }
        satisfied += (uint32_t)message->satisfied;
        offered += (uint32_t)(message->offers != 0u);
        on_time +=
            (uint32_t)(message->satisfied &&
                       latency <=
                           (uint64_t)run->network.manifest.latency_target_ms *
                               1000u);
        if (latency > maximum)
            maximum = latency;
        fprintf(report, "%u,%u,%u,%d,%s,%u,%" PRIu64 ",%s,%s,%s,%" PRIu64 ",",
                message->source, message->target, message->sequence,
                message->admitted,
                message->satisfied  ? "SATISFIED"
                : message->rejected ? "REJECTED"
                : message->admitted ? "ACTIVE"
                                    : "NOT_OFFERED",
                message->offers, message->admitted_us, first_offer, evidence,
                elapsed, censored);
        for (byte = 0u; byte < NINLIL_ID_BYTES; byte++)
            fprintf(report, "%02x", message->id.bytes[byte]);
        fputc('\n', report);
    }
    fprintf(
        report,
        "offered=%u admitted=%u rejected=%u satisfied=%u active=%u "
        "remote_offered=%u on_time=%u not_on_time=%u "
        "max_completed_latency_us=%" PRIu64
        " restarts=%u reoffers=%u isolated_active=%u healthy_satisfied=%u\n",
        run->offered, run->admitted, run->rejected, satisfied,
        run->admitted - satisfied, offered, on_time, run->admitted - on_time,
        maximum, run->restarts, run->reoffers, run->isolated_active,
        run->healthy_satisfied);
    fprintf(
        report,
        "data_tx=%u receipt_tx=%u data_lost=%u receipt_lost=%u "
        "duplicates=%u rx_overflow=%u interrupted=%u data_airtime_us=%" PRIu64
        " receipt_airtime_us=%" PRIu64 "\n",
        run->network.tx_data, run->network.tx_receipt, run->network.lost_data,
        run->network.lost_receipt, run->network.duplicates,
        run->network.rx_overflow, run->network.interrupted,
        run->network.data_airtime_us, run->network.receipt_airtime_us);
    fprintf(report, "coalesced_queued_retries=%u busy=%u\n",
            run->network.coalesced, run->network.busy);
    for (index = 0u; index < run->network.manifest.nodes; index++)
        fprintf(report, "node=%zu rx_peak=%zu tx_queue_peak=%zu\n", index + 1u,
                run->network.nodes[index].rx_peak,
                run->network.nodes[index].pending_peak);
    if (ferror(report))
        return NINLIL_ERR_IO;
    return run->offered == run->count && run->rejected == 0u &&
                   on_time == run->admitted
               ? NINLIL_OK
               : NINLIL_ERR_TIMEOUT;
}
