#define NODES 7u
#define NINLIL_TEST_BOUND_FANOUT 1
#define main original_node_tests
int original_node_tests(int argc, char **argv);
#include "test_node.c"
#undef main
#include "ninlil_fanout_core.h"

static int sha256(void *ctx, const uint8_t *data, size_t length,
                  uint8_t out[32])
{
    uint8_t digest[32];
    size_t written = 0u;
    (void)ctx;
    if (psa_hash_compute(PSA_ALG_SHA_256, data, length, digest, sizeof(digest),
                         &written) != PSA_SUCCESS ||
        written != 32u)
        return NINLIL_ERR_IO;
    memcpy(out, digest, 32u);
    return NINLIL_OK;
}
static int compare_target(const void *a, const void *b)
{
    const ninlil_fanout_target *left = a, *right = b;
    return memcmp(left->identity, right->identity, 32u);
}
static void connect_store(ninlil_fanout_core *a, ninlil_fanout_store **s,
                          ninlil_fanout_store_config *c,
                          ninlil_fanout_store_mode mode)
{
    c->eligible = NULL;
    c->admit_bound = NULL;
    c->query_bound = NULL;
    CHECK(ninlil_fanout_core_connect(a, ninlil_node_core(devices[0].node), c) ==
          NINLIL_OK);
    CHECK(ninlil_fanout_store_open(s, c, mode) == NINLIL_OK);
}
int main(void)
{
    ninlil_fanout_core adapter;
    ninlil_fanout_store *store;
    ninlil_fanout_store_config config = {0};
    ninlil_fanout_contract contract = {0};
    ninlil_fanout_target targets[NODES - 1u];
    ninlil_fanout_status status;
    char directory[128], path[256];
    uint8_t payload[4] = {9u, 1u, 7u, 2u};
    unsigned int accepted[NODES] = {0};
    int restarted = 0;
    memset(targets, 0, sizeof(targets));
    weak_direct = 0; /* Shared broadcast fixture; no physical range claim. */
    setup();
    wait_ms(350000u);
    for (uint16_t i = 1u; i < NODES; i++) {
        ninlil_delivery_binding b;
        CHECK(ninlil_node_peer_binding(devices[0].node, (uint16_t)(i + 1u),
                                       &b) == NINLIL_OK);
        memcpy(targets[i - 1u].identity, b.peer_identity, 32u);
        targets[i - 1u].address = (uint16_t)(i + 1u);
        targets[i - 1u].membership_epoch = b.membership_epoch;
        targets[i - 1u].binding_epoch = b.binding_epoch;
        test_fill_id(&targets[i - 1u].idempotency_key, (uint8_t)(i + 201u));
        memcpy(contract.source, b.source_identity, 32u);
        memcpy(contract.authority, b.authority, 16u);
        contract.authority_epoch = b.authority_epoch;
    }
    qsort(targets, NODES - 1u, sizeof(targets[0]), compare_target);
    test_fill_id(&contract.operation, 151u);
    contract.payload_reference = 1u;
    contract.service = 256u;
    contract.traffic = NINLIL_TRAFFIC_NORMAL;
    contract.evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    CHECK(sha256(NULL, payload, sizeof(payload), contract.payload_digest) ==
          NINLIL_OK);
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "group") == 0);
    config.location = path;
    config.maximum_bytes = 1024u * 1024u;
    config.target_capacity = NODES - 1u;
    config.operation = contract.operation;
    config.sha256 = sha256;
    memcpy(config.source_identity, contract.source, 32u);
    connect_store(&adapter, &store, &config, NINLIL_FANOUT_STORE_INITIALIZE);
    CHECK(ninlil_fanout_store_start(store, &contract, targets, NODES - 1u,
                                    payload, sizeof(payload)) == 0);
    CHECK(ninlil_fanout_core_step(&adapter, store, now, 4u) == 0);
    /* Group and Core both survive; restart before the newly accepted DATA is
     * sent. */
    ninlil_fanout_store_close(store);
    restart(0u);
    connect_store(&adapter, &store, &config, NINLIL_FANOUT_STORE_RESUME);
    restarted = 1;
    for (unsigned int step = 0u; step < 100000u; step++) {
        CHECK(ninlil_fanout_core_step(&adapter, store, now, 4u) == NINLIL_OK);
        tick();
        for (unsigned int i = 1u; i < NODES; i++) {
            ninlil_inbound in;
            int rc = ninlil_receive(ninlil_node_core(devices[i].node), &in);
            CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_EMPTY);
            if (rc == NINLIL_OK) {
                CHECK(in.payload_len == sizeof(payload) &&
                      !memcmp(in.payload, payload, sizeof(payload)));
                CHECK(
                    ninlil_application_accept(ninlil_node_core(devices[i].node),
                                              &in.message_id) == 0);
                accepted[i]++;
            }
        }
        CHECK(ninlil_fanout_store_inspect(store, &status) == NINLIL_OK);
        if (status.all_satisfied)
            break;
    }
    if (!status.all_satisfied)
        show();
    CHECK(restarted && status.all_satisfied && status.total == 6u);
    for (unsigned int i = 1u; i < NODES; i++)
        CHECK(accepted[i] == 1u);
    ninlil_fanout_store_close(store);
    connect_store(&adapter, &store, &config, NINLIL_FANOUT_STORE_RESUME);
    CHECK(ninlil_fanout_store_inspect(store, &status) == 0 &&
          status.all_satisfied);
    ninlil_fanout_store_close(store);
    test_remove_directory(directory, path, NULL);
    for (unsigned int i = 0u; i < NODES; i++) {
        device *d = &devices[i];
        ninlil_node_close(d->node);
        ninlil_identity_close(&d->identity);
        ninlil_identity_file_close(&d->identity_file);
        test_remove_directory(d->identity_dir, "identity.bin",
                              ".identity.lock");
        test_remove_directory(d->core_dir, "core", NULL);
        test_remove_directory(d->control_dir, "control", NULL);
    }
    ninlil_counter_close(&eras);
    printf("seven real node/Core/EDHOC owners + durable bound Group + Root "
           "restart PASS: %u simulated RF frames\n",
           transmissions);
    return 0;
}
