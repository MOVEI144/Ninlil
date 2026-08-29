#include "ninlil_custody.h"
#include "ninlil_group.h"
#include "ninlil_leaf.h"
#include "ninlil_topology.h"
#include "test_support.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,    \
                    #expression);                                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct custody_log {
    ninlil_custody_entry entries[16];
    ninlil_custody_entry records[32];
    ninlil_custody_record_type record_types[32];
    unsigned int commits;
    unsigned int record_count;
    int fail_next;
} custody_log;

typedef struct topology_log {
    ninlil_route_lease lease;
    ninlil_reception_observation observations[4];
    unsigned int commits;
    unsigned int observation_count;
    int fail_next;
} topology_log;

typedef struct group_log {
    unsigned int starts;
    unsigned int admits;
    unsigned int outcomes;
    unsigned int forgets;
    int fail_next;
} group_log;

static int same_id(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

static int custody_commit(void *ctx, ninlil_custody_record_type type,
                          const ninlil_custody_entry *entry)
{
    custody_log *log = ctx;
    unsigned int index;
    ninlil_custody_entry *slot = NULL;

    log->commits++;
    if (log->fail_next) {
        log->fail_next = 0;
        return NINLIL_ERR_IO;
    }
    if (log->record_count >= 32u)
        return NINLIL_ERR_CAPACITY;
    log->record_types[log->record_count] = type;
    log->records[log->record_count++] = *entry;
    for (index = 0u; index < 16u; index++) {
        if (log->entries[index].used &&
            same_id(&log->entries[index].message_id, &entry->message_id)) {
            slot = &log->entries[index];
            break;
        }
        if (!slot && !log->entries[index].used)
            slot = &log->entries[index];
    }
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    if (type == NINLIL_CUSTODY_RECORD_FORGET)
        memset(slot, 0, sizeof(*slot));
    else
        *slot = *entry;
    return NINLIL_OK;
}

static int topology_commit(void *ctx, const ninlil_route_lease *lease)
{
    topology_log *log = ctx;

    log->commits++;
    if (log->fail_next) {
        log->fail_next = 0;
        return NINLIL_ERR_IO;
    }
    log->lease = *lease;
    return NINLIL_OK;
}

static void observe_reception(void *ctx,
                              const ninlil_reception_observation *observation)
{
    topology_log *log = ctx;

    if (log->observation_count < 4u)
        log->observations[log->observation_count++] = *observation;
}

static int group_commit(void *ctx, ninlil_group_record_type type,
                        const ninlil_group_record *record)
{
    group_log *log = ctx;

    (void)record;
    if (log->fail_next) {
        log->fail_next = 0;
        return NINLIL_ERR_IO;
    }
    if (type == NINLIL_GROUP_RECORD_START)
        log->starts++;
    else if (type == NINLIL_GROUP_RECORD_ADMIT)
        log->admits++;
    else if (type == NINLIL_GROUP_RECORD_OUTCOME)
        log->outcomes++;
    else if (type == NINLIL_GROUP_RECORD_FORGET)
        log->forgets++;
    return NINLIL_OK;
}

static int test_profiles_and_authorization_contract(void)
{
    ninlil_role_profile profile;
    ninlil_service_grant grant;
    ninlil_peer_policy older;
    ninlil_peer_policy newer;

    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_BATTERY_LEAF, &profile) ==
          NINLIL_OK);
    CHECK(profile.max_outbound == 8u && profile.max_inbound == 2u);
    CHECK(profile.critical_reserve == 2u && profile.control_reserve == 1u);
    CHECK(profile.bulk_maximum == 0u);
    CHECK(ninlil_role_profile_validate(&profile) == NINLIL_OK);
    profile.shared_slots++;
    CHECK(ninlil_role_profile_validate(&profile) == NINLIL_ERR_INVALID);

    memset(&grant, 0, sizeof(grant));
    grant.service_id = UINT16_C(0x0100);
    grant.directions = NINLIL_SERVICE_BOTH;
    grant.maximum_payload_bytes = 32u;
    grant.maximum_live_messages = 2u;
    grant.traffic_class_mask = NINLIL_TRAFFIC_MASK(NINLIL_TRAFFIC_NORMAL);
    memset(&older, 0, sizeof(older));
    older.role = NINLIL_ROLE_BATTERY_LEAF;
    older.capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    older.membership_epoch = 4u;
    older.session_membership_epoch = 4u;
    older.grants = &grant;
    older.grant_count = 1u;
    CHECK(ninlil_policy_validate(&older, 8u) == NINLIL_OK);
    newer = older;
    newer.capabilities |= UINT32_C(0x80000000);
    CHECK(ninlil_policy_validate(&newer, 8u) == NINLIL_ERR_INVALID);
    newer = older;
    newer.capabilities |= NINLIL_CAP_RELAY_CUSTODY;
    CHECK(ninlil_policy_validate(&newer, 8u) == NINLIL_ERR_INVALID);
    newer = older;
    newer.capabilities |= NINLIL_CAP_POLL_DOWNLINK;
    CHECK(ninlil_policy_update_validate(&older, &newer, 8u) ==
          NINLIL_ERR_CONFLICT);
    newer.membership_epoch = 5u;
    CHECK(ninlil_policy_update_validate(&older, &newer, 8u) ==
          NINLIL_ERR_STATE);
    newer.session_membership_epoch = 5u;
    CHECK(ninlil_policy_update_validate(&older, &newer, 8u) == NINLIL_OK);
    grant.service_id = UINT16_C(0x00FF);
    CHECK(ninlil_policy_validate(&older, 8u) == NINLIL_ERR_INVALID);
    return 0;
}

static int test_custody_reconnect_and_capacity(void)
{
    ninlil_custody_entry entries[10];
    ninlil_custody_entry restored[10];
    ninlil_custody_spool spool;
    ninlil_custody_spool reopened;
    custody_log log;
    ninlil_id first;
    ninlil_id second;
    ninlil_id empty;
    ninlil_id extra;
    ninlil_custody_entry replayed;
    uint16_t cursor;
    uint32_t delay_ms;
    unsigned int index;

    CHECK(ninlil_custody_reconnect_delay(NINLIL_CUSTODY_RECONNECT_HOST, 0u,
                                         &delay_ms) == NINLIL_OK);
    CHECK(delay_ms == NINLIL_HOST_RECONNECT_INITIAL_MS);
    CHECK(ninlil_custody_reconnect_delay(NINLIL_CUSTODY_RECONNECT_GATEWAY,
                                         UINT8_MAX, &delay_ms) == NINLIL_OK);
    CHECK(delay_ms == NINLIL_RECONNECT_MAX_MS);
    memset(&log, 0, sizeof(log));
    test_fill_id(&first, UINT8_C(0x11));
    test_fill_id(&second, UINT8_C(0x22));
    test_fill_id(&empty, UINT8_C(0x23));
    CHECK(ninlil_custody_open(&spool, entries, 10u, 1024u, 8u, custody_commit,
                              &log) == NINLIL_OK);
    CHECK(ninlil_custody_admit(&spool, &first, 7u, 100u, 1u, 1u,
                               NINLIL_EVIDENCE_HOST_ADAPTER_STORED) ==
          NINLIL_OK);
    CHECK(ninlil_custody_admit(&spool, &second, 7u, 200u, 2u, 1u,
                               NINLIL_EVIDENCE_HOST_ADAPTER_STORED) ==
          NINLIL_OK);
    CHECK(ninlil_custody_admit(&spool, &empty, 7u, 0u, 3u, 1u,
                               NINLIL_EVIDENCE_HOST_ADAPTER_STORED) ==
          NINLIL_OK);
    CHECK(spool.live_bytes == 300u && spool.live == 3u);
    test_fill_id(&extra, UINT8_C(0x24));
    CHECK(ninlil_custody_admit(&spool, &extra, 7u, 0u, 0u, 1u,
                               NINLIL_EVIDENCE_HOST_ADAPTER_STORED) ==
          NINLIL_ERR_INVALID);
    CHECK(spool.live_bytes == 300u && spool.live == 3u);
    CHECK(ninlil_custody_note_evidence(
              &spool, &empty, NINLIL_EVIDENCE_GATEWAY_CUSTODY) == NINLIL_OK);
    CHECK(ninlil_custody_note_evidence(
              &spool, &empty, NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(spool.live_bytes == 300u && spool.live == 2u);
    CHECK(ninlil_custody_note_evidence(
              &spool, &first, NINLIL_EVIDENCE_GATEWAY_CUSTODY) == NINLIL_OK);
    CHECK(spool.live_bytes == 300u);
    cursor = 0u;
    CHECK(ninlil_custody_replay_next(&spool, &cursor, &replayed) == NINLIL_OK);
    CHECK(same_id(&replayed.message_id, &first));
    CHECK(ninlil_custody_note_evidence(
              &spool, &first, NINLIL_EVIDENCE_REMOTE_STORED) == NINLIL_OK);
    CHECK(spool.live_bytes == 200u && spool.live == 1u);

    CHECK(ninlil_custody_open(&reopened, restored, 10u, 1024u, 8u,
                              custody_commit, &log) == NINLIL_OK);
    for (index = 0u; index < log.record_count; index++) {
        CHECK(ninlil_custody_restore(&reopened, log.record_types[index],
                                     &log.records[index]) == NINLIL_OK);
    }
    CHECK(reopened.live == 1u && reopened.live_bytes == 200u);
    cursor = 0u;
    CHECK(ninlil_custody_replay_next(&reopened, &cursor, &replayed) ==
          NINLIL_OK);
    CHECK(same_id(&replayed.message_id, &second));
    CHECK(ninlil_custody_replay_next(&reopened, &cursor, &replayed) ==
          NINLIL_ERR_EMPTY);
    CHECK(ninlil_custody_forget(&reopened, &first) == NINLIL_OK);

    for (index = 0u; index < 8u; index++) {
        test_fill_id(&extra, (uint8_t)(0x30u + index));
        CHECK(ninlil_custody_admit(&reopened, &extra, 9u, 1u, 100u + index, 1u,
                                   NINLIL_EVIDENCE_HOST_ADAPTER_STORED) ==
              NINLIL_OK);
    }
    test_fill_id(&extra, UINT8_C(0x49));
    CHECK(ninlil_custody_admit(&reopened, &extra, 9u, 1u, 200u, 1u,
                               NINLIL_EVIDENCE_HOST_ADAPTER_STORED) ==
          NINLIL_ERR_CAPACITY);
    log.fail_next = 1;
    CHECK(ninlil_custody_update_route(&reopened, &second, 2u) == NINLIL_ERR_IO);
    CHECK(reopened.poisoned == 1u);
    return 0;
}

static int test_sleeping_leaf_opportunities(void)
{
    ninlil_leaf_window_profile profile;
    ninlil_leaf_opportunity opportunity;
    ninlil_id first;
    ninlil_id second;
    ninlil_id taken;
    uint64_t start;
    uint32_t duration;

    CHECK(ninlil_leaf_window_profile_lab(&profile) == NINLIL_OK);
    CHECK(ninlil_leaf_window_profile_validate(&profile) == NINLIL_OK);
    CHECK(ninlil_leaf_opportunity_begin(&opportunity, &profile, 1000u) ==
          NINLIL_OK);
    CHECK(ninlil_leaf_opportunity_next(&opportunity, &start, &duration) ==
          NINLIL_OK);
    CHECK(start == 1200u && duration == 800u);
    CHECK(ninlil_leaf_opportunity_next(&opportunity, &start, &duration) ==
          NINLIL_OK);
    CHECK(start == 3200u && duration == 1200u);
    CHECK(ninlil_leaf_opportunity_next(&opportunity, &start, &duration) ==
          NINLIL_ERR_EMPTY);
    test_fill_id(&first, UINT8_C(0x51));
    test_fill_id(&second, UINT8_C(0x52));
    CHECK(ninlil_leaf_stage_downlink(&opportunity, &first) == NINLIL_OK);
    CHECK(ninlil_leaf_stage_downlink(&opportunity, &second) ==
          NINLIL_ERR_CAPACITY);
    CHECK(ninlil_leaf_take_downlink(&opportunity, &taken) == NINLIL_OK);
    CHECK(same_id(&taken, &first));
    CHECK(ninlil_leaf_take_downlink(&opportunity, &taken) == NINLIL_ERR_EMPTY);
    ninlil_leaf_opportunity_sleep(&opportunity);
    CHECK(ninlil_leaf_opportunity_next(&opportunity, &start, &duration) ==
          NINLIL_ERR_INVALID);
    CHECK(ninlil_leaf_opportunity_begin(&opportunity, &profile, 5000u) ==
          NINLIL_OK);
    CHECK(ninlil_leaf_stage_downlink(&opportunity, &second) == NINLIL_OK);
    return 0;
}

static int test_multi_gateway_dedupe_and_route_epoch(void)
{
    ninlil_route_lease routes[4];
    ninlil_uplink_dedupe dedupe[8];
    ninlil_topology topology;
    topology_log log;
    ninlil_reception_observation observation;
    uint64_t backups[1] = {2u};
    int is_new;
    uint64_t gateway;

    memset(&log, 0, sizeof(log));
    CHECK(ninlil_topology_open(&topology, routes, 4u, dedupe, 8u,
                               topology_commit, &log, observe_reception,
                               &log) == NINLIL_OK);
    for (gateway = 1u; gateway <= NINLIL_DOMAIN_GATEWAY_MAX; gateway++)
        CHECK(ninlil_topology_add_gateway(&topology, gateway) == NINLIL_OK);
    CHECK(ninlil_topology_add_gateway(&topology, 9u) == NINLIL_ERR_CAPACITY);
    memset(&observation, 0, sizeof(observation));
    test_fill_id(&observation.message_id, UINT8_C(0x61));
    observation.gateway_uid = 1u;
    observation.rssi_dbm = -80;
    observation.snr_db = 5;
    CHECK(ninlil_topology_note_uplink(&topology, &observation, &is_new) ==
          NINLIL_OK);
    CHECK(is_new == 1);
    observation.gateway_uid = 2u;
    observation.rssi_dbm = -70;
    CHECK(ninlil_topology_note_uplink(&topology, &observation, &is_new) ==
          NINLIL_OK);
    CHECK(is_new == 0 && log.observation_count == 2u);
    CHECK(log.observations[0].rssi_dbm == -80 &&
          log.observations[1].rssi_dbm == -70);

    CHECK(ninlil_topology_assign_route(&topology, 10u, 1u, backups, 1u, 1u,
                                       2000u,
                                       1000u) == NINLIL_ERR_UNAUTHORIZED);
    ninlil_topology_set_authority(&topology, 1);
    CHECK(ninlil_topology_assign_route(&topology, 11u, 1u, NULL, 0u, 1u,
                                       1000u + NINLIL_ROUTE_LEASE_MAX_MS + 1u,
                                       1000u) == NINLIL_ERR_INVALID);
    CHECK(ninlil_topology_assign_route(&topology, 10u, 1u, backups, 1u, 1u,
                                       2000u, 1000u) == NINLIL_OK);
    CHECK(ninlil_topology_check_downlink(&topology, 10u, 1u, 1u, 1100u) ==
          NINLIL_OK);
    CHECK(ninlil_topology_assign_route(&topology, 10u, 2u, NULL, 0u, 2u, 3000u,
                                       1200u) == NINLIL_ERR_BUSY);
    CHECK(ninlil_topology_release_route(&topology, 10u, 1u, 1u) == NINLIL_OK);
    CHECK(ninlil_topology_assign_route(&topology, 10u, 2u, NULL, 0u, 2u, 3000u,
                                       1200u) == NINLIL_OK);
    CHECK(ninlil_topology_check_downlink(&topology, 10u, 1u, 1u, 1300u) ==
          NINLIL_ERR_CONFLICT);
    CHECK(ninlil_topology_check_downlink(&topology, 10u, 2u, 2u, 1300u) ==
          NINLIL_OK);
    ninlil_topology_set_authority(&topology, 0);
    CHECK(ninlil_topology_check_downlink(&topology, 10u, 2u, 2u, 1300u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_topology_release_route(&topology, 10u, 2u, 2u) ==
          NINLIL_ERR_UNAUTHORIZED);
    {
        ninlil_route_lease restored_routes[1];
        ninlil_route_lease lease;
        ninlil_uplink_dedupe restored_dedupe[1];
        ninlil_topology restored;

        CHECK(ninlil_topology_open(&restored, restored_routes, 1u,
                                   restored_dedupe, 1u, topology_commit, &log,
                                   NULL, NULL) == NINLIL_OK);
        CHECK(ninlil_topology_add_gateway(&restored, 1u) == NINLIL_OK);
        CHECK(ninlil_topology_add_gateway(&restored, 2u) == NINLIL_OK);
        memset(&lease, 0, sizeof(lease));
        lease.used = 1u;
        lease.peer = 10u;
        lease.active_gateway_uid = 1u;
        lease.backup_gateway_uids[0] = 2u;
        lease.backup_count = 1u;
        lease.route_epoch = 1u;
        lease.lease_from_ms = 1000u;
        lease.lease_until_ms = 2000u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) == NINLIL_OK);
        lease.active_gateway_uid = 2u;
        lease.backup_gateway_uids[0] = 1u;
        lease.route_epoch = 2u;
        lease.lease_from_ms = 1200u;
        lease.lease_until_ms = 2500u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) ==
              NINLIL_ERR_CORRUPT);
        lease.active_gateway_uid = 1u;
        lease.backup_gateway_uids[0] = 2u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) == NINLIL_OK);
        lease.released = 1u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) == NINLIL_OK);
        lease.released = 0u;
        lease.active_gateway_uid = 2u;
        lease.backup_gateway_uids[0] = 1u;
        lease.route_epoch = 3u;
        lease.lease_from_ms = 1300u;
        lease.lease_until_ms = 2600u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) == NINLIL_OK);
        ninlil_topology_set_authority(&restored, 1);
        CHECK(ninlil_topology_check_downlink(&restored, 10u, 2u, 3u, 1400u) ==
              NINLIL_OK);
    }
    {
        ninlil_route_lease restored_routes[1];
        ninlil_route_lease lease;
        ninlil_uplink_dedupe restored_dedupe[1];
        ninlil_topology restored;

        CHECK(ninlil_topology_open(&restored, restored_routes, 1u,
                                   restored_dedupe, 1u, topology_commit, &log,
                                   NULL, NULL) == NINLIL_OK);
        CHECK(ninlil_topology_add_gateway(&restored, 1u) == NINLIL_OK);
        CHECK(ninlil_topology_add_gateway(&restored, 2u) == NINLIL_OK);
        memset(&lease, 0, sizeof(lease));
        lease.used = 1u;
        lease.peer = 11u;
        lease.active_gateway_uid = 1u;
        lease.route_epoch = 1u;
        lease.lease_from_ms = 1000u;
        lease.lease_until_ms = 2000u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) == NINLIL_OK);
        lease.active_gateway_uid = 2u;
        lease.route_epoch = 2u;
        lease.lease_from_ms = 2000u;
        lease.lease_until_ms = 3000u;
        CHECK(ninlil_topology_restore_route(&restored, &lease) == NINLIL_OK);
    }
    return 0;
}

static int test_group_wave_bound(void)
{
    ninlil_group_engine engine;
    ninlil_group_engine restored;
    ninlil_group_engine maximum;
    ninlil_group_record record;
    uint16_t target_workspace[2u * 40u];
    uint16_t restored_targets[2u * 40u];
    uint16_t maximum_targets[NINLIL_GROUP_TARGET_MAX];
    uint16_t maximum_workspace[NINLIL_GROUP_TARGET_MAX];
    uint8_t state_workspace[2u * 40u];
    uint8_t restored_states[2u * 40u];
    uint8_t maximum_states[NINLIL_GROUP_TARGET_MAX];
    uint16_t targets[40];
    group_log log;
    ninlil_id operation_id;
    ninlil_id selected_operation;
    uint16_t target;
    unsigned int index;

    memset(&log, 0, sizeof(log));
    test_fill_id(&operation_id, UINT8_C(0x71));
    for (index = 0u; index < 40u; index++)
        targets[index] = (uint16_t)(index + 1u);
    CHECK(ninlil_group_open(&engine, target_workspace, state_workspace, 2u, 40u,
                            group_commit, &log) == NINLIL_OK);
    CHECK(ninlil_group_start(&engine, &operation_id, targets, 40u) ==
          NINLIL_OK);
    for (index = 0u; index < NINLIL_GROUP_GATEWAY_WAVE_MAX; index++) {
        CHECK(ninlil_group_peek(&engine, &selected_operation, &target) ==
              NINLIL_OK);
        CHECK(same_id(&selected_operation, &operation_id));
        CHECK(ninlil_group_mark_admitted(&engine, &operation_id, target) ==
              NINLIL_OK);
    }
    CHECK(ninlil_group_peek(&engine, &selected_operation, &target) ==
          NINLIL_ERR_CAPACITY);
    CHECK(engine.inflight == NINLIL_GROUP_GATEWAY_WAVE_MAX);
    CHECK(ninlil_group_mark_terminal(&engine, &operation_id, 1u,
                                     NINLIL_OUTCOME_SATISFIED) == NINLIL_OK);
    CHECK(ninlil_group_peek(&engine, &selected_operation, &target) ==
          NINLIL_OK);
    CHECK(target == 33u);
    CHECK(log.starts == 1u && log.admits == 32u && log.outcomes == 1u);

    CHECK(ninlil_group_open(&restored, restored_targets, restored_states, 2u,
                            40u, group_commit, &log) == NINLIL_OK);
    memset(&record, 0, sizeof(record));
    record.operation_id = operation_id;
    record.targets = targets;
    record.target_count = 40u;
    CHECK(ninlil_group_restore(&restored, NINLIL_GROUP_RECORD_START, &record) ==
          NINLIL_OK);
    record.targets = NULL;
    record.target_count = 0u;
    for (index = 0u; index < NINLIL_GROUP_GATEWAY_WAVE_MAX; index++) {
        record.target = (uint16_t)(index + 1u);
        CHECK(ninlil_group_restore(&restored, NINLIL_GROUP_RECORD_ADMIT,
                                   &record) == NINLIL_OK);
    }
    record.target = 1u;
    record.outcome = NINLIL_OUTCOME_SATISFIED;
    CHECK(ninlil_group_restore(&restored, NINLIL_GROUP_RECORD_OUTCOME,
                               &record) == NINLIL_OK);
    CHECK(restored.inflight == NINLIL_GROUP_GATEWAY_WAVE_MAX - 1u);
    CHECK(ninlil_group_peek(&restored, &selected_operation, &target) ==
          NINLIL_OK);
    CHECK(target == 33u);

    for (index = 2u; index <= NINLIL_GROUP_GATEWAY_WAVE_MAX; index++) {
        CHECK(
            ninlil_group_mark_terminal(&engine, &operation_id, (uint16_t)index,
                                       NINLIL_OUTCOME_SATISFIED) == NINLIL_OK);
    }
    for (index = NINLIL_GROUP_GATEWAY_WAVE_MAX + 1u; index <= 40u; index++) {
        CHECK(ninlil_group_mark_admitted(&engine, &operation_id,
                                         (uint16_t)index) == NINLIL_OK);
        CHECK(
            ninlil_group_mark_terminal(&engine, &operation_id, (uint16_t)index,
                                       NINLIL_OUTCOME_SATISFIED) == NINLIL_OK);
    }
    CHECK(ninlil_group_start(&engine, &operation_id, targets, 40u) ==
          NINLIL_OK);
    CHECK(log.starts == 1u);
    CHECK(ninlil_group_forget(&engine, &operation_id) == NINLIL_OK);
    CHECK(log.forgets == 1u);

    for (index = 0u; index < NINLIL_GROUP_TARGET_MAX; index++)
        maximum_targets[index] = (uint16_t)(index + 1u);
    test_fill_id(&selected_operation, UINT8_C(0x72));
    CHECK(ninlil_group_open(&maximum, maximum_workspace, maximum_states, 1u,
                            NINLIL_GROUP_TARGET_MAX, group_commit,
                            &log) == NINLIL_OK);
    CHECK(ninlil_group_start(&maximum, &selected_operation, maximum_targets,
                             NINLIL_GROUP_TARGET_MAX) == NINLIL_OK);
    CHECK(ninlil_group_peek(&maximum, &operation_id, &target) == NINLIL_OK);
    CHECK(target == 1u);
    return 0;
}

static int (*const tests[])(void) = {
    test_profiles_and_authorization_contract,
    test_custody_reconnect_and_capacity,
    test_sleeping_leaf_opportunities,
    test_multi_gateway_dedupe_and_route_epoch,
    test_group_wave_bound,
};

int main(void)
{
    size_t index;

    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++) {
        int rc = tests[index]();

        printf("operations_%02zu %s\n", index + 1u, rc == 0 ? "PASS" : "FAIL");
        if (rc != 0)
            return rc;
    }
    return 0;
}
