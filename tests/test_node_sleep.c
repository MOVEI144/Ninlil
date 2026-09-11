#define main original_node_tests
int original_node_tests(int argc, char **argv);
#include "test_node.c"
#undef main
#include "ninlil_sleep.h"
int main(void)
{
    ninlil_node *leaf;
    ninlil_submission request;
    ninlil_id id;
    ninlil_info info;
    uint8_t payload = 42u, fingerprint[16];
    uint64_t lease, until, at;
    unsigned int accepted = 0u;
    CHECK(setenv("NINLIL_TEST_ENROLLMENT", "1", 1) == 0);
    CHECK(setenv("NINLIL_TEST_BATTERY", "1", 1) == 0);
    setup();
    for (unsigned int i = 0u; i < NODES; i++)
        for (unsigned int j = 0u; j < NODES; j++)
            CHECK(ninlil_node_enroll(devices[i].node, &members[j]) ==
                  NINLIL_OK);
    wait_ms(150000u);
    leaf = devices[2].node;
    at = now - devices[2].boot_at;
    CHECK(ninlil_node_suspend(devices[0].node, now) == NINLIL_ERR_INVALID);
    CHECK(ninlil_node_suspend(devices[1].node, now) == NINLIL_ERR_INVALID);
    CHECK(ninlil_node_lease(leaf, &lease) == NINLIL_OK);
    memcpy(fingerprint,
           leaf->peers[leaf->root_index].sessions[0].material.fingerprint, 16u);
    ninlil_submission_defaults(&request);
    request.target = 3u;
    request.service = 256u;
    request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    request.payload = &payload;
    request.payload_len = 1u;
    request.idempotency_key.bytes[0] = 71u;
    CHECK(ninlil_submit(devices[0].node->core, &request, &id) == NINLIL_OK);
    CHECK(ninlil_node_suspend(leaf, at) == NINLIL_OK);
    CHECK(ninlil_node_step(leaf, at) == NINLIL_ERR_BUSY);
    CHECK(ninlil_node_receive(leaf, &payload, 1u, at) == NINLIL_ERR_BUSY);
    CHECK(ninlil_node_frame_current(leaf, &payload, 1u, at) == NINLIL_ERR_BUSY);
    CHECK(ninlil_node_resume(leaf, at - 1u) == NINLIL_ERR_STATE);
    devices[2].node =
        NULL; /* Radio and owner are asleep; other nodes continue. */
    wait_ms(90000u);
    CHECK(ninlil_query(devices[0].node->core, &id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    devices[2].node = leaf;
    CHECK(ninlil_node_resume(leaf, now - devices[2].boot_at) == NINLIL_OK);
    CHECK(ninlil_node_lease(leaf, &lease) != NINLIL_OK);
    CHECK(!memcmp(
        fingerprint,
        leaf->peers[leaf->root_index].sessions[0].material.fingerprint, 16u));
    until = now + 240000u;
    while (now < until) {
        ninlil_inbound in;
        tick();
        if (ninlil_receive(leaf->core, &in) == NINLIL_OK) {
            CHECK(!memcmp(in.message_id.bytes, id.bytes, 16u));
            CHECK(ninlil_application_accept(leaf->core, &id) == NINLIL_OK);
            accepted++;
        }
        CHECK(ninlil_query(devices[0].node->core, &id, &info) == NINLIL_OK);
        if (info.outcome == NINLIL_OUTCOME_SATISFIED)
            break;
    }
    CHECK(now < until && accepted == 1u);
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
    puts("Battery pause, retained context, route expiry and pending delivery "
         "recovery PASS");
    return 0;
}
