/* Reuse the actual owner/radio harness, with five distinct persistent devices.
 * This is a link-loss model, not physical RF or a deployment latency claim. */
#define NODES 5u
#define main ninlil_original_main
int ninlil_original_main(int argc, char **argv);
#include "test_node.c"
#undef main

static void stop_device(unsigned int index)
{
    ninlil_node_close(devices[index].node);
    devices[index].node = NULL;
    devices[index].head = devices[index].count = 0u;
}
static void commission(unsigned int index)
{
    uint8_t credential[NINLIL_ADMISSION_MAX];
    size_t length;
    CHECK(ninlil_node_authorize(devices[0].node, &members[index], credential,
                                sizeof(credential), &length) == NINLIL_OK);
    CHECK(ninlil_node_advertise(devices[index].node, credential, length) ==
          NINLIL_OK);
}
static void boot_device(unsigned int index)
{
    device *d = &devices[index];
    CHECK(!d->node);
    d->boot_at = now;
    CHECK(ninlil_node_open(&d->node, &d->config, 0u) == NINLIL_OK);
}
static void path_through(uint16_t relay_id)
{
    ninlil_node *n = devices[0].node;
    for (unsigned int i = 0u; i < NINLIL_NETWORK_FLOWS_MAX; i++) {
        const ninlil_network_path *p = &n->coordinator.flows[i].active.path;
        if (p->count == 3u && p->nodes[0] == 1u && p->nodes[2] == 3u) {
            CHECK(p->nodes[1] == relay_id);
            return;
        }
    }
    CHECK(0);
}
int main(void)
{
    ninlil_submission request;
    ninlil_info info;
    ninlil_id pending;
    uint8_t payload[64] = {42};
    uint64_t until, began;
    unsigned int accepted = 0u;
    CHECK(setenv("NINLIL_TEST_ENROLLMENT", "radio", 1) == 0);
    setup();
    stop_device(3u);
    stop_device(4u);
    commission(1u);
    commission(2u);
    wait_ms(180000u);
    CHECK(devices[2].node->joined);
    delivery(1u, 0u);
    path_through(2u);
    boot_device(3u);
    commission(3u);
    wait_ms(120000u);
    stop_device(1u); /* Sudden removal, no drain acknowledgement. */
    wait_ms(120000u);
    delivery(2u, 0u);
    path_through(4u);
    stop_device(3u);
    wait_ms(90000u);
    ninlil_submission_defaults(&request);
    request.idempotency_key.bytes[0] = 0xe1;
    request.target = 3u;
    request.service = 256u;
    request.payload = payload;
    request.payload_len = sizeof(payload);
    request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    CHECK(ninlil_submit(devices[0].node->core, &request, &pending) ==
          NINLIL_OK);
    wait_ms(60000u);
    CHECK(ninlil_query(devices[0].node->core, &pending, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    CHECK(ninlil_node_index(devices[2].node, 5u) < 0);
    boot_device(
        4u); /* Previously unknown replacement, no configuration of D. */
    began = now;
    commission(4u);
    until = now + 240000u;
    while (now < until) {
        ninlil_inbound in;
        tick();
        if (ninlil_receive(devices[2].node->core, &in) == NINLIL_OK) {
            CHECK(!memcmp(in.message_id.bytes, pending.bytes, NINLIL_ID_BYTES));
            CHECK(in.payload_len == sizeof(payload) &&
                  !memcmp(in.payload, payload, sizeof(payload)));
            CHECK(ninlil_application_accept(devices[2].node->core,
                                            &in.message_id) == NINLIL_OK);
            accepted++;
        }
        CHECK(ninlil_query(devices[0].node->core, &pending, &info) ==
              NINLIL_OK);
        if (info.outcome == NINLIL_OUTCOME_SATISFIED)
            break;
    }
    if (now >= until)
        show();
    CHECK(now < until && accepted == 1u);
    printf("Unknown replacement and pending delivery recovered in model: %llu "
           "ms\n",
           (unsigned long long)(now - began));
    delivery(3u, 0u);
    path_through(5u);
    CHECK(ninlil_node_revoke(devices[0].node, 2u, 1u) == NINLIL_OK);
    boot_device(1u);
    wait_ms(90000u);
    CHECK(!devices[1].node->joined &&
          devices[1].node->peers[devices[1].node->local_index].revoked);
    for (unsigned int i = 0u; i < NODES; i++) {
        stop_device(i);
        ninlil_identity_close(&devices[i].identity);
        ninlil_identity_file_close(&devices[i].identity_file);
        test_remove_directory(devices[i].identity_dir, "identity.bin",
                              ".identity.lock");
        test_remove_directory(devices[i].core_dir, "core", NULL);
        test_remove_directory(devices[i].control_dir, "control", NULL);
    }
    ninlil_counter_close(&eras);
    puts("Out-of-range Join, alternate Relay, partition, unknown replacement, "
         "revoked return PASS");
    return 0;
}
