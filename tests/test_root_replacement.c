#define NODES 5u
#define main original_node_tests
int original_node_tests(int argc, char **argv);
#include "test_node.c"
#undef main
static ninlil_identity issuer;
static ninlil_identity_file issuer_file;
static char issuer_dir[128];
static flash new_era_flash;
static ninlil_counter_store new_eras;
static ninlil_node_member replacement;
static void sign_member(const ninlil_node_member *m, uint8_t *credential,
                        size_t *length)
{
    CHECK(ninlil_admission_sign(&issuer, m, credential, NINLIL_ADMISSION_MAX,
                                length) == NINLIL_OK);
}
static void reuse_address(void)
{
    ninlil_node *root = devices[3].node;
    ninlil_node_member member = members[2];
    ninlil_submission pending;
    ninlil_id id;
    uint8_t bytes[NINLIL_ADMISSION_MAX], payload = 7u;
    size_t size;
    memcpy(member.public_key, members[0].public_key, 65u);
    memcpy(member.grant.identity, members[0].grant.identity, 32u);
    member.grant.membership_epoch = member.grant.binding_epoch = 2u;
    sign_member(&member, bytes, &size);
    ninlil_submission_defaults(&pending);
    pending.target = 3u;
    pending.service = 256u;
    pending.payload = &payload;
    pending.payload_len = 1u;
    pending.idempotency_key.bytes[0] = 82u;
    CHECK(ninlil_submit(root->core, &pending, &id) == NINLIL_OK);
    CHECK(ninlil_node_admit(root, bytes, size) == NINLIL_ERR_BUSY);
    CHECK(ninlil_cancel(root->core, &id) == NINLIL_OK); /* Not attempted. */
    for (unsigned int i = 0u; i < 20u; i++) {
        const ninlil_node_member *physical = &members[i % 2u ? 2u : 0u];
        memcpy(member.public_key, physical->public_key, 65u);
        memcpy(member.grant.identity, physical->grant.identity, 32u);
        member.grant.membership_epoch = member.grant.binding_epoch = i + 2u;
        sign_member(&member, bytes, &size);
        CHECK(ninlil_node_admit(root, bytes, size) == NINLIL_OK);
        CHECK(root->config.member_count == 3u);
        CHECK(ninlil_node_collect(root) == NINLIL_OK);
    }
    restart(3u);
    root = devices[3].node;
    CHECK(root->config.member_count == 3u &&
          root->members[ninlil_node_index(root, 3u)].grant.membership_epoch ==
              21u);
    sign_member(&members[2], bytes, &size);
    CHECK(ninlil_node_admit(root, bytes, size) == NINLIL_ERR_CONFLICT);
}
static void join_reused_address(void)
{
    static ninlil_node_member roster[2];
    uint8_t certificate[NINLIL_ADMISSION_MAX], payload = 91u;
    size_t size;
    ninlil_submission request;
    ninlil_id id;
    ninlil_info info;
    roster[0] = replacement;
    roster[1] = members[4];
    roster[1].grant.node = 3u;
    roster[1].grant.membership_epoch = roster[1].grant.binding_epoch = 22u;
    sign_member(&roster[1], certificate, &size);
    ninlil_node_close(devices[2].node);
    devices[2].node = NULL;
    devices[4].config.local = 3u;
    devices[4].config.root = 1u;
    devices[4].config.members = roster;
    devices[4].config.member_count = 2u;
    devices[4].config.authority_key = issuer.public_key;
    devices[4].boot_at = now;
    devices[4].head = devices[4].count = 0u;
    CHECK(ninlil_node_open(&devices[4].node, &devices[4].config, 0u) ==
          NINLIL_OK);
    CHECK(ninlil_node_advertise(devices[4].node, certificate, size) ==
          NINLIL_OK);
    wait_ms(240000u);
    CHECK(devices[4].node->joined);
    CHECK(devices[3]
              .node->members[ninlil_node_index(devices[3].node, 3u)]
              .grant.membership_epoch == 22u);
    ninlil_submission_defaults(&request);
    request.target = 1u;
    request.service = 256u;
    request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    request.payload = &payload;
    request.payload_len = 1u;
    request.idempotency_key.bytes[0] = 93u;
    CHECK(ninlil_submit(devices[4].node->core, &request, &id) == NINLIL_OK);
    uint64_t until = now + 300000u;
    unsigned int accepted = 0u;
    do {
        ninlil_inbound in;
        tick();
        if (ninlil_receive(devices[3].node->core, &in) == NINLIL_OK) {
            CHECK(!memcmp(in.message_id.bytes, id.bytes, 16u));
            CHECK(ninlil_application_accept(devices[3].node->core, &id) ==
                  NINLIL_OK);
            accepted++;
        }
        CHECK(ninlil_query(devices[4].node->core, &id, &info) == NINLIL_OK);
    } while (now < until && info.outcome != NINLIL_OUTCOME_SATISFIED);
    CHECK(now < until && accepted == 1u);
}
int main(void)
{
    ninlil_identity_io issuer_io;
    ninlil_security_io era_io = {read_flash, write_flash, erase_flash,
                                 &new_era_flash, sizeof(new_era_flash.bytes)};
    ninlil_counter_config era_config = {{8}, 0u, 1u, UINT32_MAX - 1u};
    ninlil_submission request;
    ninlil_id id;
    ninlil_info info;
    uint8_t credential[NINLIL_ADMISSION_MAX],
        root_certificate[NINLIL_ADMISSION_MAX], payload = 39u;
    size_t length, root_length;
    uint64_t until;
    unsigned int accepted = 0u;
    CHECK(setenv("NINLIL_TEST_ENROLLMENT", "1", 1) == 0);
    setup();
    ninlil_node_close(devices[3].node);
    devices[3].node = NULL;
    ninlil_node_close(devices[4].node);
    devices[4].node = NULL;
    CHECK(test_make_directory(issuer_dir, sizeof(issuer_dir)) == 0);
    CHECK(ninlil_identity_file_open(&issuer_file, issuer_dir, &issuer_io) ==
          NINLIL_OK);
    CHECK(ninlil_identity_provision(&issuer, issuer_io) == NINLIL_OK);
    for (unsigned int i = 0u; i < 3u; i++) {
        for (unsigned int j = 0u; j < 3u; j++)
            CHECK(ninlil_node_enroll(devices[i].node, &members[j]) ==
                  NINLIL_OK);
        if (!i) {
            ninlil_network_plan pending = {0};
            pending.path.count = 2u;
            pending.path.nodes[0] = 1u;
            pending.path.nodes[1] = 3u;
            pending.path.membership_epochs[0] =
                pending.path.membership_epochs[1] = 1u;
            pending.path.cost_us = 1000u;
            pending.epoch = pending.profile = 1u;
            pending.rto_ms = 1000u;
            pending.valid_until_ms = devices[i].node->clock.stamp + 60000u;
            pending.phase = NINLIL_PLAN_STAGED;
            CHECK(ninlil_node_plan_commit(devices[i].node, &pending) ==
                  NINLIL_OK);
            CHECK(ninlil_node_plan_restore(devices[i].node, &pending) ==
                  NINLIL_OK);
        }
        devices[i].config.authority_key = issuer.public_key;
        restart(i);
        if (!i) {
            CHECK(!devices[i].node->coordinator.pending.epoch);
            restart(
                i); /* Aborted old preparation precedes the new epoch fence. */
        }
        sign_member(&members[i], credential, &length);
        CHECK(ninlil_node_advertise(devices[i].node, credential, length) ==
              NINLIL_OK);
    }
    wait_ms(150000u);
    ninlil_submission_defaults(&request);
    request.target = 1u;
    request.service = 256u;
    request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    request.idempotency_key.bytes[0] = 81u;
    request.payload = &payload;
    request.payload_len = 1u;
    CHECK(ninlil_submit(devices[2].node->core, &request, &id) == NINLIL_OK);
    wait_ms(40000u);
    CHECK(ninlil_query(devices[2].node->core, &id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    ninlil_node_close(
        devices[0].node); /* Only old Root is physically removed. */
    devices[0].node = NULL;
    replacement = members[3];
    replacement.grant = members[0].grant;
    memcpy(replacement.grant.identity, devices[3].identity.identity, 32u);
    replacement.grant.membership_epoch = replacement.grant.binding_epoch = 2u;
    memset(new_era_flash.bytes, 255, sizeof(new_era_flash.bytes));
    CHECK(ninlil_counter_open(&new_eras, &era_io, NINLIL_COUNTER_CREATE_NEW,
                              &era_config) == NINLIL_OK);
    devices[3].config.local = devices[3].config.root = 1u;
    devices[3].config.members = &replacement;
    devices[3].config.member_count = 1u;
    devices[3].config.authority_key = issuer.public_key;
    devices[3].config.root_eras = &new_eras;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_SITE_GATEWAY,
                                       &devices[3].config.resources) ==
          NINLIL_OK);
    devices[3].boot_at = now;
    devices[3].head = devices[3].count = 0u;
    CHECK(ninlil_node_open(&devices[3].node, &devices[3].config, 0u) ==
          NINLIL_OK);
    sign_member(&replacement, root_certificate, &root_length);
    CHECK(ninlil_node_advertise(devices[3].node, root_certificate,
                                root_length) == NINLIL_OK);
    until = now + 300000u;
    while (now < until) {
        ninlil_inbound in;
        tick();
        if (ninlil_receive(devices[3].node->core, &in) == NINLIL_OK) {
            CHECK(!memcmp(in.message_id.bytes, id.bytes, 16u));
            CHECK(ninlil_application_accept(devices[3].node->core, &id) ==
                  NINLIL_OK);
            accepted++;
        }
        CHECK(ninlil_query(devices[2].node->core, &id, &info) == NINLIL_OK);
        if (info.outcome == NINLIL_OUTCOME_SATISFIED)
            break;
    }
    if (now >= until)
        show();
    CHECK(now < until && accepted == 1u);
    reuse_address();
    sign_member(&members[0], credential, &length);
    CHECK(ninlil_node_admit(devices[2].node, credential, length) ==
          NINLIL_ERR_CONFLICT);
    restart(2u);
    CHECK(devices[2]
              .node->members[devices[2].node->root_index]
              .grant.membership_epoch == 2u);
    devices[0].boot_at = now;
    devices[0].head = devices[0].count = 0u;
    CHECK(ninlil_node_open(&devices[0].node, &devices[0].config, 0u) ==
          NINLIL_OK);
    CHECK(ninlil_node_admit(devices[0].node, root_certificate, root_length) ==
          NINLIL_OK);
    CHECK(devices[0].node->status.fault == NINLIL_ERR_UNAUTHORIZED);
    ninlil_node_close(devices[0].node);
    devices[0].node = NULL;
    CHECK(ninlil_node_open(&devices[0].node, &devices[0].config, 0u) !=
          NINLIL_OK);
    join_reused_address();
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
    ninlil_counter_close(&new_eras);
    ninlil_identity_close(&issuer);
    ninlil_identity_file_close(&issuer_file);
    test_remove_directory(issuer_dir, "identity.bin", ".identity.lock");
    puts("Root-only replacement, remote trust switch, retained child message, "
         "replay and old Root fence PASS");
    return 0;
}
