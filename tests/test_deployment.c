#define main original_node_tests
int original_node_tests(int argc, char **argv);
#include "test_node.c"
#undef main
#include "ninlil_journal.h"
#include "ninlil_setup.h"
static int ambiguous;
int __real_ninlil_journal_rewrite(ninlil_journal *, ninlil_journal_snapshot,
                                  void *);
int __wrap_ninlil_journal_rewrite(ninlil_journal *, ninlil_journal_snapshot,
                                  void *);
int __wrap_ninlil_journal_rewrite(ninlil_journal *j,
                                  ninlil_journal_snapshot snapshot, void *ctx)
{
    int rc = __real_ninlil_journal_rewrite(j, snapshot, ctx);
    if (ambiguous && rc == NINLIL_OK) {
        ambiguous = 0;
        return NINLIL_ERR_IO; /* Publication completed; response lost. */
    }
    return rc;
}
static size_t setup_packet(uint8_t *p, uint64_t revision,
                           const ninlil_node_member *root,
                           const ninlil_node_member *local)
{
    size_t first, second = 0u;
    ninlil_node_put(p, revision, 8u);
    p[8] = 1u;
    memcpy(p + 9, devices[1].identity.public_key, 65u);
    CHECK(ninlil_admission_sign(&devices[1].identity, root, p + 76,
                                NINLIL_ADMISSION_MAX, &first) == NINLIL_OK);
    ninlil_node_put(p + 74, first, 2u);
    if (local)
        CHECK(ninlil_admission_sign(&devices[1].identity, local, p + 76 + first,
                                    NINLIL_ADMISSION_MAX,
                                    &second) == NINLIL_OK);
    return 76u + first + second;
}
int main(void)
{
    ninlil_setup *s;
    char directory[128], location[256];
    uint8_t bytes[NINLIL_DEPLOYMENT_PACKET_MAX], payload = 1u;
    ninlil_node_config c;
    ninlil_node_member root, local;
    ninlil_submission submission;
    ninlil_id id;
    uint64_t revision;
    size_t length;
    int autorun;
    setup();
    wait_ms(150000u);
    ninlil_submission_defaults(&submission);
    submission.target = 1u;
    submission.service = 256u;
    submission.payload = &payload;
    submission.payload_len = 1u;
    submission.idempotency_key.bytes[0] = 99u;
    CHECK(ninlil_submit(devices[2].node->core, &submission, &id) == NINLIL_OK);
    for (unsigned int i = 0u; i < NODES; i++) {
        ninlil_node_close(devices[i].node);
        devices[i].node = NULL;
    }
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(location, sizeof(location), directory, "setup") == 0);
    CHECK(ninlil_setup_open(&s, location, &devices[2].identity) == NINLIL_OK);
    length = setup_packet(bytes, 0u, &members[0], &members[2]);
    CHECK(ninlil_setup_packet(s, bytes, length) == NINLIL_OK);
    CHECK(ninlil_setup_packet(s, bytes, length) == NINLIL_OK);
    bytes[length - 1u] ^= 1u;
    CHECK(ninlil_setup_packet(s, bytes, length) == NINLIL_ERR_UNAUTHORIZED);
    bytes[length - 1u] ^= 1u;
    CHECK(ninlil_identity_mark_deployed(&devices[2].identity) == NINLIL_OK);
    c = devices[2].config;
    CHECK(ninlil_setup_config(s, &c, &revision, &autorun) == NINLIL_OK);
    c.offline = 1u;
    CHECK(ninlil_node_open(&devices[2].node, &c, 0u) == NINLIL_OK);
    CHECK(ninlil_node_step(devices[2].node, 0u) == NINLIL_ERR_BUSY);
    CHECK(ninlil_node_retire_deployment(devices[2].node) == NINLIL_ERR_BUSY);
    CHECK(ninlil_cancel(devices[2].node->core, &id) == NINLIL_OK);
    ninlil_node_close(devices[2].node);
    devices[2].node = NULL;
    root = members[2];
    root.grant.role = NINLIL_ROLE_SITE_GATEWAY;
    root.grant.binding_epoch = root.grant.membership_epoch = 2u;
    memset(root.grant.authority, 9, 16u);
    for (unsigned int cycle = 0u; cycle < 3u; cycle++) {
        local = members[2];
        local.grant.role = cycle == 1u ? NINLIL_ROLE_BATTERY_LEAF
                                       : NINLIL_ROLE_POWERED_RELAY_CANDIDATE;
        local.grant.capabilities =
            3u |
            (cycle == 1u ? NINLIL_CAP_POLL_DOWNLINK : NINLIL_CAP_RELAY_CUSTODY);
        local.grant.binding_epoch = local.grant.membership_epoch = cycle + 2u;
        if (cycle)
            root = members[0];
        memcpy(local.grant.authority, root.grant.authority, 16u);
        length = setup_packet(bytes, cycle + 1u, &root, cycle ? &local : NULL);
        ambiguous = 1;
        int begin_result = ninlil_setup_transfer_begin(s, bytes, length);
        if (begin_result != NINLIL_ERR_IO)
            fprintf(stderr, "cycle %u begin %d\n", cycle, begin_result);
        CHECK(begin_result == NINLIL_ERR_IO);
        ninlil_setup_close(s);
        CHECK(ninlil_setup_open(&s, location, &devices[2].identity) ==
              NINLIL_OK);
        CHECK(ninlil_setup_transfer_pending(s));
        CHECK(ninlil_setup_transfer_begin(s, bytes, length) == NINLIL_OK);
        CHECK(ninlil_setup_config(s, &c, &revision, &autorun) ==
              NINLIL_ERR_BUSY);
        CHECK(!autorun && c.offline);
        for (unsigned int repeat = 0u; repeat < 2u; repeat++) {
            CHECK(ninlil_node_open(&devices[2].node, &c, 0u) == NINLIL_OK);
            CHECK(ninlil_node_retire_deployment(devices[2].node) == NINLIL_OK);
            ninlil_node_close(devices[2].node);
            devices[2].node = NULL;
        }
        ambiguous = 1;
        CHECK(ninlil_setup_transfer_finish(s) == NINLIL_ERR_IO);
        ninlil_setup_close(s);
        CHECK(ninlil_setup_open(&s, location, &devices[2].identity) ==
              NINLIL_OK);
        CHECK(!ninlil_setup_transfer_pending(s));
        CHECK(ninlil_setup_packet(s, bytes, length) == NINLIL_OK);
        CHECK(ninlil_setup_config(s, &c, &revision, &autorun) == NINLIL_OK);
        CHECK(revision == cycle + 2u && autorun && !c.offline);
        CHECK(ninlil_node_open(&devices[2].node, &c, 0u) == NINLIL_OK);
        CHECK(ninlil_setup_advertise(s, devices[2].node) == NINLIL_OK);
        CHECK(ninlil_node_step(devices[2].node, 0u) == NINLIL_OK);
        ninlil_node_close(devices[2].node);
        devices[2].node = NULL;
    }
    ninlil_setup_close(s);
    test_remove_directory(directory, "setup", NULL);
    for (unsigned int i = 0u; i < NODES; i++) {
        ninlil_identity_close(&devices[i].identity);
        ninlil_identity_file_close(&devices[i].identity_file);
        test_remove_directory(devices[i].identity_dir, "identity.bin",
                              ".identity.lock");
        test_remove_directory(devices[i].core_dir, "core", NULL);
        test_remove_directory(devices[i].control_dir, "control", NULL);
    }
    ninlil_counter_close(&eras);
    puts("CA setup, pending ownership, network/role transfer, interrupted "
         "retirement and lost publication PASS");
    return 0;
}
