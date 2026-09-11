/* Real Group and Core journals; only the Link is the existing deterministic
 * test transport. Reuse the binding fixture, not a replacement Core. */
#define main original_binding_tests
int original_binding_tests(void);
#include "test_delivery_binding.c"
#undef main
#include "ninlil_fanout_core.h"
#include <openssl/evp.h>

static int lose_admission;
int __real_ninlil_submit_bound(ninlil_runtime *runtime,
                               const ninlil_submission *submission,
                               const ninlil_delivery_binding *binding,
                               ninlil_id *id);
int __wrap_ninlil_submit_bound(ninlil_runtime *runtime,
                               const ninlil_submission *submission,
                               const ninlil_delivery_binding *binding,
                               ninlil_id *id);
int __wrap_ninlil_submit_bound(ninlil_runtime *runtime,
                               const ninlil_submission *submission,
                               const ninlil_delivery_binding *binding,
                               ninlil_id *id)
{
    int rc = __real_ninlil_submit_bound(runtime, submission, binding, id);
    if (rc == NINLIL_OK && lose_admission)
        _exit(95); /* Core committed; Group has not received the ID. */
    return rc;
}
static int digest(void *ctx, const uint8_t *data, size_t length,
                  uint8_t out[32])
{
    uint8_t value[32];
    unsigned int written = 0;
    (void)ctx;
    if (EVP_Digest(data, length, value, &written, EVP_sha256(), NULL) != 1 ||
        written != 32u)
        return NINLIL_ERR_IO;
    memcpy(out, value, 32u);
    return NINLIL_OK;
}
static int connect_group(fixture *f, ninlil_fanout_core *a,
                         ninlil_fanout_store **store,
                         ninlil_fanout_store_config *c,
                         ninlil_fanout_store_mode mode)
{
    c->eligible = NULL;
    c->admit_bound = NULL;
    c->query_bound = NULL;
    CHECK(ninlil_fanout_core_connect(a, f->sender, c) == 0);
    CHECK(ninlil_fanout_store_open(store, c, mode) == 0);
    return 0;
}
int main(void)
{
    fixture f;
    ninlil_fanout_core adapter;
    ninlil_fanout_store *store;
    ninlil_fanout_store_config config = {0};
    ninlil_fanout_contract contract = {0};
    ninlil_fanout_target target = {0}, saved;
    ninlil_fanout_item item;
    ninlil_fanout_status state;
    ninlil_id id;
    int status;
    pid_t child;
    char group_path[256];
    CHECK(create(&f) == 0);
    CHECK(test_make_path(group_path, sizeof(group_path), f.directory,
                         "group") == 0);
    memcpy(contract.source, f.expected.source_identity, 32u);
    memcpy(contract.authority, f.expected.authority, 16u);
    contract.authority_epoch = f.expected.authority_epoch;
    contract.payload_reference = 1u;
    contract.service = 256u;
    contract.evidence = f.request.required_evidence;
    contract.traffic = f.request.traffic_class;
    test_fill_id(&contract.operation, 188u);
    CHECK(digest(NULL, f.request.payload, f.request.payload_len,
                 contract.payload_digest) == 0);
    memcpy(target.identity, f.expected.peer_identity, 32u);
    target.address = 2u;
    target.membership_epoch = f.expected.membership_epoch;
    target.binding_epoch = f.expected.binding_epoch;
    target.idempotency_key = f.request.idempotency_key;
    config.location = group_path;
    config.maximum_bytes = 128u * 1024u;
    config.target_capacity = 1u;
    config.sha256 = digest;
    config.operation = contract.operation;
    memcpy(config.source_identity, contract.source, 32u);
    CHECK(connect_group(&f, &adapter, &store, &config,
                        NINLIL_FANOUT_STORE_INITIALIZE) == 0);
    CHECK(ninlil_fanout_store_start(store, &contract, &target, 1u,
                                    f.request.payload,
                                    f.request.payload_len) == 0);
    ninlil_fanout_store_close(store);
    ninlil_close(f.sender);
    f.sender = NULL;
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        if (open_source(&f) || connect_group(&f, &adapter, &store, &config,
                                             NINLIL_FANOUT_STORE_RESUME))
            _exit(91);
        lose_admission = 1;
        (void)ninlil_fanout_core_step(&adapter, store, 0u, 1u);
        _exit(92);
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 95);
    CHECK(open_source(&f) == 0);
    CHECK(f.sender->outbound_live == 1u);
    id = ninlil_find_idempotency(f.sender, &target.idempotency_key)->message_id;
    CHECK(connect_group(&f, &adapter, &store, &config,
                        NINLIL_FANOUT_STORE_RESUME) == 0);
    CHECK(ninlil_fanout_store_target(store, 0u, &saved, &item) == 0 &&
          item.phase == NINLIL_FANOUT_INTENT);
    CHECK(ninlil_fanout_core_step(&adapter, store, 0u, 1u) == 0);
    CHECK(ninlil_fanout_store_target(store, 0u, &saved, &item) == 0 &&
          item.phase == NINLIL_FANOUT_ADMITTED);
    CHECK(ninlil_id_equal(&item.message, &id) && f.sender->outbound_live == 1u);
    CHECK(finish_delivery(&f, &id) == 0);
    CHECK(!ninlil_find_archive_id(f.sender, &id)->binding_released);
    CHECK(ninlil_fanout_core_step(&adapter, store, 1000u, 1u) == 0);
    CHECK(ninlil_fanout_store_inspect(store, &state) == 0 &&
          state.all_satisfied);
    CHECK(ninlil_find_archive_id(f.sender, &id)->binding_released);
    /* Retiring Core history does not erase Group's authoritative result. */
    CHECK(ninlil_retire_completed(f.sender) == 0);
    ninlil_fanout_store_close(store);
    ninlil_close(f.sender);
    f.sender = NULL;
    CHECK(open_source(&f) == 0);
    CHECK(connect_group(&f, &adapter, &store, &config,
                        NINLIL_FANOUT_STORE_RESUME) == 0);
    CHECK(ninlil_fanout_core_step(&adapter, store, 0u, 1u) == 0);
    CHECK(ninlil_fanout_store_inspect(store, &state) == 0 &&
          state.all_satisfied);
    ninlil_fanout_store_close(store);
    unlink(group_path);
    close_fixture(&f);
    puts("actual Group/Core cross-store crash, same ID recovery, terminal "
         "handoff PASS");
    return 0;
}
