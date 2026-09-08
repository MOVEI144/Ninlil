#define _POSIX_C_SOURCE 200809L
#include "ninlil_control_log.h"
#include "test_support.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    ninlil_config config = {0};
    ninlil_runtime *core = NULL;
    ninlil_control_log *control = NULL;
    ninlil_control_replay replay = {0};
    ninlil_submission request;
    ninlil_id id;
    ninlil_info info;
    test_policy policy;
    test_link transport;
    uint32_t random = 1u;
    uint8_t identity[32] = {1u}, different[32] = {2u};
    char directory[128], path[256], control_path[256];
    CHECK(test_make_directory(directory, sizeof(directory)) == 0);
    CHECK(test_make_path(path, sizeof(path), directory, "core") == 0);
    CHECK(test_make_path(control_path, sizeof(control_path), directory,
                         "control") == 0);
    test_policy_init(&policy, 0x100u, 32u);
    test_link_init(&transport, 320u);
    test_link_bind(&transport, 0u, &config.link);
    config.node_id = 1u;
    config.journal_location = path;
    config.random = (ninlil_random){test_rng_fill, &random};
    config.policy_lookup = test_policy_lookup;
    config.policy_ctx = &policy;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &config.profile) == NINLIL_OK);
    CHECK(ninlil_open(&core, &config) == NINLIL_OK);
    CHECK(ninlil_bind_storage(core, identity, 0) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_bind_storage(core, identity, 1) == NINLIL_OK);
    CHECK(ninlil_bind_storage(core, different, 1) == NINLIL_ERR_CONFLICT);
    ninlil_close(core);
    CHECK(ninlil_open(&core, &config) == NINLIL_OK);
    CHECK(ninlil_bind_storage(core, identity, 0) == NINLIL_OK);
    CHECK(ninlil_bind_storage(core, different, 0) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_health(core) == NINLIL_OK);
    ninlil_close(core);
    CHECK(unlink(path) == 0);
    CHECK(ninlil_open(&core, &config) == NINLIL_OK);
    CHECK(ninlil_bind_storage(core, identity, 0) == NINLIL_ERR_CORRUPT);
    ninlil_submission_defaults(&request);
    test_fill_id(&request.idempotency_key, 1u);
    request.target = 2u;
    request.service = 0x100u;
    CHECK(ninlil_submit(core, &request, &id) == NINLIL_OK);
    CHECK(ninlil_bind_storage(core, identity, 1) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_query(core, &id, &info) == NINLIL_OK &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    ninlil_close(core);
    CHECK(ninlil_control_log_open(&control, control_path, 128u * 1024u,
                                  replay) == NINLIL_OK);
    CHECK(ninlil_control_log_bind(control, identity, 0) == NINLIL_ERR_CORRUPT);
    CHECK(ninlil_control_log_bind(control, identity, 1) == NINLIL_OK);
    ninlil_control_log_close(control);
    CHECK(ninlil_control_log_open(&control, control_path, 128u * 1024u,
                                  replay) == NINLIL_OK);
    CHECK(ninlil_control_log_bind(control, identity, 0) == NINLIL_OK);
    CHECK(ninlil_control_log_bind(control, different, 1) ==
          NINLIL_ERR_CONFLICT);
    ninlil_control_log_close(control);
    CHECK(unlink(control_path) == 0);
    CHECK(ninlil_control_log_open(&control, control_path, 128u * 1024u,
                                  replay) == NINLIL_OK);
    CHECK(ninlil_control_log_bind(control, identity, 0) == NINLIL_ERR_CORRUPT);
    ninlil_control_log_close(control);
    test_remove_directory(directory, path, control_path);
    puts("explicit storage provisioning/reopen/missing/wrong identity/nonempty "
         "preservation PASS");
    return 0;
}
