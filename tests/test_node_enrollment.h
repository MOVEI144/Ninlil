static void wait_ms(uint64_t duration);
static void delivery(unsigned int sequence, unsigned int restart_mode);
static void restart(unsigned int index);
static void show(void);
#include "admission_vector.h"
#include "ninlil_setup.h"
#include <fcntl.h>
/* Included by the actual multi-node owner harness, using both journal ports. */
static ninlil_node_member enrollment_initial[NODES][2];
static int reject_control(void *ctx, uint16_t peer, ninlil_traffic_class cls,
                          const uint8_t *bytes, size_t length)
{
    CHECK(peer == 3u && cls == NINLIL_TRAFFIC_CONTROL && bytes && length);
    (*(unsigned int *)ctx)++;
    return NINLIL_ERR_CAPACITY;
}
static void enrollment_control_pressure(void)
{
    ninlil_node *n = devices[0].node;
    ninlil_route_emit_fn saved = n->config.emit;
    void *context = n->config.emit_ctx;
    unsigned int calls = 0u;
    n->peers[ninlil_node_index(n, 3u)].control_emit_at = 0u;
    n->config.emit = reject_control;
    n->config.emit_ctx = &calls;
    for (unsigned int i = 0u; i < 100u; i++) {
        int rc = ninlil_node_control_send(n, 3u, NODE_JOIN_READY, NULL, 0u);
        CHECK(rc == NINLIL_ERR_CAPACITY || rc == NINLIL_ERR_BUSY);
    }
    CHECK(calls == 1u);
    n->config.emit = saved;
    n->config.emit_ctx = context;
    wait_ms(NODE_CONTROL_MS);
}
static void enrollment_signed(ninlil_node *root, ninlil_node *receiver,
                              const ninlil_node_member *member)
{
    uint8_t credential[NINLIL_ADMISSION_MAX], bad[NINLIL_ADMISSION_MAX];
    size_t length = 0u;
    ninlil_node_member decoded;
    CHECK(ninlil_admission_verify(admission_vector_key, admission_vector,
                                  sizeof(admission_vector),
                                  &decoded) == NINLIL_OK &&
          decoded.grant.node == 3u);
    CHECK(ninlil_node_authorize(root, member, credential, sizeof(credential),
                                &length) == NINLIL_OK);
    CHECK(ninlil_admission_verify(members[0].public_key, credential, length,
                                  &decoded) == NINLIL_OK);
    CHECK(ninlil_admission_verify(members[1].public_key, credential, length,
                                  &decoded) != NINLIL_OK);
    for (size_t i = 0u; i < length; i++)
        CHECK(ninlil_admission_verify(members[0].public_key, credential, i,
                                      &decoded) != NINLIL_OK);
    for (size_t i = 0u; i < length; i++) {
        memcpy(bad, credential, length);
        bad[i] ^= 1u;
        CHECK(ninlil_node_admit(receiver, bad, length) != NINLIL_OK);
    }
    CHECK(ninlil_node_admit(receiver, credential, length) == NINLIL_OK);
    CHECK(ninlil_node_admit(receiver, credential, length) == NINLIL_OK);
}
static void enrollment_config(ninlil_node_config *c, unsigned int index)
{
    if (!getenv("NINLIL_TEST_ENROLLMENT"))
        return;
    if (index >= 3u) {
        members[index].grant.role = NINLIL_ROLE_POWERED_RELAY_CANDIDATE;
        members[index].grant.capabilities |= NINLIL_CAP_RELAY_CUSTODY;
    }
    enrollment_initial[index][0] = members[0];
    enrollment_initial[index][1] = members[index];
    c->members = enrollment_initial[index];
    c->member_count = index ? 2u : 1u;
    c->dynamic_enrollment = 1u;
}

static void enrollment_check(ninlil_node *n)
{
    ninlil_node_member m = members[2], out;
    uint8_t bytes[NINLIL_MEMBER_RECORD_MAX];
    size_t length = ninlil_member_encode(&m, bytes, sizeof(bytes));
    CHECK(length && ninlil_member_decode(bytes, length, &out) == NINLIL_OK);
    CHECK(ninlil_member_decode(bytes, length - 1u, &out) == NINLIL_ERR_INVALID);
    bytes[0] ^= 1u;
    CHECK(ninlil_member_decode(bytes, length, &out) == NINLIL_ERR_INVALID);
    m.grant.authority[0] ^= 1u;
    CHECK(ninlil_node_enroll(n, &m) != NINLIL_OK);
    m = members[2];
    m.public_key[0] = 0u;
    CHECK(ninlil_node_enroll(n, &m) == NINLIL_ERR_INVALID);
    m = members[2];
    m.grant.identity[0] ^= 1u;
    CHECK(ninlil_node_enroll(n, &m) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_node_member_inspect(n, 3u, &out) == NINLIL_OK);
    CHECK(out.grant.membership_epoch == 1u);
    CHECK(ninlil_node_enroll(n, &members[2]) == NINLIL_OK);
    m = members[2];
    m.grant.membership_epoch++;
    CHECK(ninlil_node_enroll(n, &m) == NINLIL_OK);
    CHECK(!n->peers[ninlil_node_index(n, 3u)].member_active);
    CHECK(ninlil_node_enroll(n, &members[2]) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_node_collect(n) == NINLIL_OK);
}

static void enrollment_setup(void)
{
    char directory[128], path[256];
    ninlil_setup *s = NULL;
    ninlil_node_config c = {0};
    ninlil_identity reopened = {0};
    uint8_t credential[NINLIL_ADMISSION_MAX];
    size_t length;
    uint64_t revision = 9u;
    int autorun = -1;
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "setup") == 0);
    CHECK(ninlil_setup_open(&s, path, &devices[2].identity) == NINLIL_OK);
    CHECK(ninlil_setup_config(s, &c, &revision, &autorun) == NINLIL_ERR_EMPTY);
    CHECK(revision == 0u && autorun == 0);
    CHECK(ninlil_node_authorize(devices[0].node, &members[2], credential,
                                sizeof(credential), &length) == NINLIL_OK);
    CHECK(ninlil_setup_update(s, 0u, &members[0], NULL, 0u, 1) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_setup_update(s, 0u, &members[0], credential, length, 1) ==
          NINLIL_OK);
    CHECK(ninlil_setup_update(s, 0u, &members[0], credential, length, 1) ==
          NINLIL_OK);
    CHECK(ninlil_setup_update(s, 0u, &members[0], credential, length, 0) ==
          NINLIL_ERR_CONFLICT);
    ninlil_setup_close(s);
    s = NULL;
    CHECK(ninlil_setup_open(&s, path, &devices[2].identity) == NINLIL_OK);
    CHECK(ninlil_setup_config(s, &c, &revision, &autorun) == NINLIL_OK);
    CHECK(revision == 1u && autorun == 1 && c.local == 3u && c.root == 1u &&
          c.member_count == 2u && c.dynamic_enrollment);
    CHECK(ninlil_setup_autorun(s, 0) == NINLIL_OK);
    CHECK(ninlil_setup_autorun(s, 0) == NINLIL_OK);
    CHECK(ninlil_setup_config(s, &c, &revision, &autorun) == NINLIL_OK);
    CHECK(revision == 2u && !autorun);
    CHECK(ninlil_identity_mark_deployed(&devices[2].identity) == NINLIL_OK);
    CHECK(ninlil_identity_open(&reopened, devices[2].identity_io) == NINLIL_OK);
    CHECK(reopened.initialized == 3u &&
          !memcmp(reopened.identity, devices[2].identity.identity, 32u));
    ninlil_identity_close(&reopened);
    {
        /* A duplicate reply must not confirm a setting corrupted on disk. */
        uint8_t *bytes = malloc(0x18000u);
        int fd = open(path, O_RDWR);
        ssize_t got;
        size_t found = 0u;
        CHECK(bytes && fd >= 0);
        got = pread(fd, bytes, 0x18000u, 0);
        CHECK(got > 45);
        for (size_t i = 0u; i + 45u <= (size_t)got; i++)
            if (!memcmp(bytes + i, "NCF\001", 4u) && bytes[i + 11u] == 2u)
                found = i;
        CHECK(found);
        bytes[found + 12u] ^= 1u;
        CHECK(pwrite(fd, bytes + found + 12u, 1u, (off_t)(found + 12u)) == 1);
        CHECK(fsync(fd) == 0 && close(fd) == 0);
        free(bytes);
        CHECK(ninlil_setup_update(s, 1u, &members[0], credential, length, 0) ==
              NINLIL_ERR_CORRUPT);
    }
    ninlil_setup_close(s);
    s = NULL;
    CHECK(unlink(path) == 0);
    CHECK(ninlil_setup_open(&s, path, &devices[2].identity) ==
          NINLIL_ERR_CORRUPT);
    CHECK(!s);
    test_remove_directory(directory, "setup", NULL);
}
static void enrollment_prepare_expiry(void)
{
    ninlil_node *copy = malloc(sizeof(*copy));
    ninlil_network_plan p = {0}, changed;
    uint8_t proof[48];
    uint64_t lease;
    CHECK(copy);
    *copy = *devices[0].node;
    CHECK(ninlil_node_lease(copy, &lease) == NINLIL_OK);
    memset(copy->local_plans, 0, sizeof(copy->local_plans));
    memset(copy->local_ready, 0, sizeof(copy->local_ready));
    memset(copy->retired, 0, sizeof(copy->retired));
    p.path.count = 3u;
    p.path.cost_us = 1000u;
    for (unsigned int i = 0u; i < 3u; i++) {
        p.path.nodes[i] = (uint16_t)(i + 1u);
        p.path.membership_epochs[i] = 1u;
    }
    p.epoch = copy->local_plan_epoch + 1u;
    p.profile = 1u;
    p.rto_ms = 1000u;
    p.prepare_until_ms = lease - 1u;
    p.valid_until_ms = lease + 50000u;
    p.prepared = 7u;
    p.phase = NINLIL_PLAN_COMMITTED;
    copy->prepared = p;
    copy->prepared.phase = NINLIL_PLAN_STAGED;
    copy->prepared.valid_until_ms = p.prepare_until_ms;
    CHECK(!ninlil_node_plan_in_progress(copy, 1u, 3u, lease));
    CHECK(ninlil_node_apply(copy, &p, proof) == NINLIL_ERR_EXPIRED);
    p.prepare_until_ms = copy->prepared.prepare_until_ms = lease + 10000u;
    copy->prepared.valid_until_ms = p.prepare_until_ms;
    CHECK(ninlil_node_plan_in_progress(copy, 1u, 3u, lease));
    CHECK(ninlil_node_apply(copy, &p, proof) == NINLIL_OK);
    CHECK(ninlil_node_plan_in_progress(copy, 1u, 3u, lease));
    CHECK(!ninlil_node_plan_in_progress(copy, 1u, 3u, p.valid_until_ms));
    changed = p;
    changed.valid_until_ms++;
    CHECK(!ninlil_node_same_plan(&p, &changed));
    {
        uint64_t token = copy->request_sequence;
        memset(copy->wanted, 0, sizeof(copy->wanted));
        memset(copy->wanted_force, 0, sizeof(copy->wanted_force));
        ninlil_node_plan_rejected(copy, &changed);
        CHECK(copy->request_sequence == token);
        ninlil_node_plan_rejected(copy, &p);
        CHECK(copy->request_sequence > token);
        token = copy->request_sequence;
        ninlil_node_plan_rejected(copy, &p);
        CHECK(copy->request_sequence == token); /* Duplicate: same request. */
        copy->now_ms += 60000u;
        memset(copy->wanted, 0, sizeof(copy->wanted));
        memset(copy->wanted_force, 0, sizeof(copy->wanted_force));
        ninlil_node_plan_rejected(copy, &p);
        CHECK(copy->request_sequence == token);
        copy->now_ms -= 60000u;
        copy->clock = devices[0].node->clock;
    }
    {
        uint8_t bytes[99] = {NODE_PREPARE};
        ninlil_network_plan staged = p;
        staged.phase = NINLIL_PLAN_STAGED;
        staged.valid_until_ms = staged.prepare_until_ms;
        staged.prepared = staged.applied = 0u;
        CHECK(!ninlil_node_prepare_window(&staged, lease));
        CHECK(ninlil_node_prepare_window(&staged, lease - 20000u));
        CHECK(
            ninlil_node_prepare_window(&p, lease)); /* COMMITTED stays fixed. */
        staged.prepare_until_ms = 0u;
        CHECK(ninlil_node_prepare_window(&staged, lease)); /* Legacy NP1. */
        staged.prepare_until_ms = p.prepare_until_ms;
        copy->coordinator.pending = staged;
        CHECK(ninlil_network_plan_encode(&staged, bytes + 1, 98u) == 98u);
        CHECK(ninlil_node_plan_frame_current(copy, 3u, bytes, sizeof(bytes)) ==
              NINLIL_OK);
        copy->coordinator.prepared_live = 4u;
        CHECK(ninlil_node_plan_frame_current(copy, 3u, bytes, sizeof(bytes)) ==
              NINLIL_ERR_STATE);
        copy->coordinator.prepared_live = 0u;
        copy->coordinator.pending = p;
        CHECK(ninlil_node_plan_frame_current(copy, 3u, bytes, sizeof(bytes)) ==
              NINLIL_ERR_STATE);
        bytes[0] = NODE_APPLY;
        CHECK(ninlil_node_plan_frame_current(copy, 3u, bytes, sizeof(bytes)) ==
              NINLIL_ERR_STATE);
        CHECK(ninlil_network_plan_encode(&p, bytes + 1, 98u) == 98u);
        CHECK(ninlil_node_plan_frame_current(copy, 3u, bytes, sizeof(bytes)) ==
              NINLIL_OK);
        copy->coordinator.pending.phase = NINLIL_PLAN_ABORTED;
        CHECK(ninlil_node_plan_frame_current(copy, 3u, bytes, sizeof(bytes)) ==
              NINLIL_ERR_STATE);
    }
    free(copy); /* Borrowed identity/session/storage handles remain owned by
                  device 0. */
}
static void enrollment_recovery_probe(void)
{
    ninlil_node *n = devices[0].node;
    int peer = ninlil_node_index(n, 3u);
    uint8_t request[19] = {0, 3};
    memcpy(request + 2, n->peers[peer].sessions[0].material.fingerprint, 16u);
    CHECK(ninlil_node_recovery_receive(n, 2u, NODE_RECOVER_REQUEST, request,
                                       sizeof(request)) == NINLIL_ERR_BUSY);
    CHECK(n->peers[peer].sessions[0].ready &&
          !memcmp(request + 2, n->peers[peer].sessions[0].material.fingerprint,
                  16u));
    request[18] = 2u;
    CHECK(ninlil_node_recovery_receive(n, 2u, NODE_RECOVER_REQUEST, request,
                                       sizeof(request)) == NINLIL_ERR_INVALID);
}
static void enrollment_restart(unsigned int mode, const ninlil_id *id)
{
    ninlil_node_status status;
    ninlil_info info;
    restart(0u);
    if (mode != 4u)
        return;
    restart(1u);
    restart(2u);
    CHECK(ninlil_query(devices[0].node->core, id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(ninlil_node_inspect(devices[1].node, &status) == NINLIL_OK &&
          status.relay_owned > 0u);
    for (unsigned int i = 0u; i < 3u; i++)
        CHECK(!devices[i].node->prepared.epoch);
    wait_ms(40000u);
    {
        uint64_t lease;
        ninlil_node *root = devices[0].node;
        CHECK(ninlil_node_lease(root, &lease) == NINLIL_ERR_STATE);
        CHECK(root->peers[ninlil_node_index(root, 2u)].attempts > 0u);
        CHECK(ninlil_query(root->core, id, &info) == NINLIL_OK &&
              info.outcome == NINLIL_OUTCOME_ACTIVE);
    }
}
static int enrollment_forward_emit(void *ctx, uint16_t peer,
                                   ninlil_traffic_class traffic,
                                   const uint8_t *frame, size_t length)
{
    int *admit = ctx;
    CHECK(peer == 1u && traffic == NINLIL_TRAFFIC_NORMAL && length == 17u &&
          frame[3] == ((NINLIL_NETWORK_HOPS_MAX - 1u) << 5 | 1u));
    return *admit ? NINLIL_OK : NINLIL_ERR_CAPACITY;
}
static void enrollment_forward_pressure(void)
{
    ninlil_node *n = calloc(1u, sizeof(*n));
    uint8_t frame[17] = {'N', 'B', 1, (NINLIL_NETWORK_HOPS_MAX << 5) | 1u};
    int admit = 0;
    CHECK(n);
    n->members[0] = members[1];
    n->config.local = 2u;
    n->config.emit = enrollment_forward_emit;
    n->config.emit_ctx = &admit;
    frame[5] = 3u;
    frame[7] = frame[15] = 1u;
    CHECK(ninlil_node_forward(n, NULL, sizeof(frame)) == NINLIL_ERR_INVALID);
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_OK &&
          n->forward_length == sizeof(frame));
    CHECK(ninlil_node_forward_current(n, frame, sizeof(frame)) != NINLIL_OK);
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_OK);
    frame[15]++;
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_ERR_CAPACITY);
    frame[15]--;
    n->now_ms = 100u;
    admit = 1;
    CHECK(ninlil_node_forward_step(n) == NINLIL_OK && !n->forward_length);
    CHECK(ninlil_node_forward_current(n, frame, sizeof(frame)) == NINLIL_OK);
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_OK &&
          !n->forward_length);
    frame[15]++;
    admit = 0;
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_OK);
    n->now_ms += 2000u;
    CHECK(ninlil_node_forward_step(n) == NINLIL_OK && !n->forward_length);
    CHECK(ninlil_node_forward_current(n, frame, sizeof(frame)) != NINLIL_OK);
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_OK);
    n->peers[0].revoked = 1u;
    admit = 1;
    CHECK(ninlil_node_forward_step(n) == NINLIL_OK && !n->forward_length);
    CHECK(ninlil_node_forward(n, frame, sizeof(frame)) == NINLIL_OK &&
          !n->forward_length);
    free(n);
}
static void enrollment_corrupt_issuer(void)
{
    ninlil_node *n = devices[0].node;
    uint8_t credential[NINLIL_ADMISSION_MAX], *bytes = malloc(0x220000u);
    size_t length = 1u;
    unsigned int changed = 0u;
    int fd;
    ssize_t got;
    CHECK(ninlil_node_collect(n) == NINLIL_OK);
    fd = open(devices[0].control_path, O_RDWR);
    CHECK(bytes && fd >= 0);
    got = pread(fd, bytes, 0x220000u, 0);
    CHECK(got > 224);
    /* Damage trust records in both possible NOR banks, including the active
     * one. No application data or device identity is modified by this model. */
    for (size_t i = 0u; i + 5u < (size_t)got; i++)
        if (!memcmp(bytes + i, "NM\001\004", 4u)) {
            bytes[i + 4u] ^= 1u;
            CHECK(pwrite(fd, bytes + i + 4u, 1u, (off_t)(i + 4u)) == 1);
            changed++;
        }
    CHECK(changed && fsync(fd) == 0 && close(fd) == 0);
    free(bytes);
    CHECK(ninlil_node_authorize(n, &members[1], credential, sizeof(credential),
                                &length) == NINLIL_ERR_CORRUPT &&
          !length);
}
static void enrollment_run(void)
{
    enrollment_forward_pressure();
    wait_ms(70000u);
    CHECK(devices[0].node->joined &&
          devices[0].node->config.member_count == 1u);
    if (!strcmp(getenv("NINLIL_TEST_ENROLLMENT"), "radio")) {
        for (unsigned int i = 1u; i < NODES; i++) {
            uint8_t credential[NINLIL_ADMISSION_MAX];
            size_t length;
            CHECK(ninlil_node_authorize(devices[0].node, &members[i],
                                        credential, sizeof(credential),
                                        &length) == NINLIL_OK);
            CHECK(ninlil_node_advertise(devices[i].node, credential, length) ==
                  NINLIL_OK);
        }
        wait_ms(180000u);
        show();
        for (unsigned int i = 0u; i < NODES; i++)
            CHECK(devices[i].node->joined &&
                  devices[i].node->config.member_count == NODES);
    }
    for (unsigned int i = 0u; i < NODES; i++) {
        for (unsigned int j = 0u; j < NODES; j++)
            enrollment_signed(devices[0].node, devices[i].node, &members[j]);
        CHECK(ninlil_node_collect(devices[i].node) == NINLIL_OK);
        restart(i);
        CHECK(devices[i].node->config.member_count == NODES);
    }
    wait_ms(120000u);
    enrollment_control_pressure();
    delivery(1u, 0u);
    CHECK(!drop_core_receipt);
    CHECK(ninlil_node_local_plan(devices[0].node, 3u, 1u, 0) < 0);
    {
        uint8_t receipt[26] = {'N', 'L', 2, 2, 0, 3, 0, 1};
        for (unsigned int i = 0u; i < 8u; i++) {
            receipt[i] ^= 1u;
            CHECK(ninlil_node_core_receipt(devices[0].node, 3u, receipt,
                                           sizeof(receipt)) ==
                  NINLIL_ERR_UNAUTHORIZED);
            receipt[i] ^= 1u;
        }
    }
    enrollment_prepare_expiry();
    enrollment_recovery_probe();
    delivery(2u, 4u);
    enrollment_setup();
    enrollment_check(devices[0].node);
    restart(0u);
    CHECK(devices[0]
              .node->members[ninlil_node_index(devices[0].node, 3u)]
              .grant.membership_epoch == 2u);
    enrollment_corrupt_issuer();
    for (unsigned int i = 0u; i < NODES; i++) {
        ninlil_node_close(devices[i].node);
        ninlil_identity_close(&devices[i].identity);
        ninlil_identity_file_close(&devices[i].identity_file);
        test_remove_directory(devices[i].identity_dir, "identity.bin",
                              ".identity.lock");
        test_remove_directory(devices[i].core_dir, "core", NULL);
        test_remove_directory(devices[i].control_dir, "control", NULL);
    }
    ninlil_counter_close(&eras);
    puts("Root alone, durable live enrollment, encrypted Relay delivery, "
         "epoch fence PASS");
}
