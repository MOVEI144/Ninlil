#define _POSIX_C_SOURCE 200809L
#include "ninlil_binding.h"
#include "ninlil_internal.h"
#include "ninlil_maintenance.h"
#include "test_support.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct fixture {
    ninlil_runtime *sender, *receiver;
    ninlil_config source_config, target_config;
    ninlil_submission request;
    ninlil_delivery_binding expected, current;
    test_policy policy;
    test_link link;
    uint32_t source_rng, target_rng;
    int lookup_result;
    char directory[128], source[256], target[256];
} fixture;
static uint8_t crash_after_type;
int __real_ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                                 const uint8_t *data, uint16_t length,
                                 ninlil_journal_ref *reference);
int __wrap_ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                                 const uint8_t *data, uint16_t length,
                                 ninlil_journal_ref *reference);
int __wrap_ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                                 const uint8_t *data, uint16_t length,
                                 ninlil_journal_ref *reference)
{
    int rc =
        __real_ninlil_journal_append(journal, type, data, length, reference);
    if (rc == NINLIL_OK && crash_after_type && type == crash_after_type)
        _exit(80 + type);
    return rc;
}
static int lookup(void *ctx, uint16_t peer, ninlil_delivery_binding *out)
{
    fixture *f = ctx;
    if (f->lookup_result)
        return f->lookup_result;
    if (peer != 2u)
        return NINLIL_ERR_NOT_FOUND;
    *out = f->current;
    return NINLIL_OK;
}
static int open_source(fixture *f)
{
    CHECK(ninlil_open(&f->sender, &f->source_config) == NINLIL_OK);
    CHECK(ninlil_bind_storage(f->sender, f->expected.source_identity, 0) ==
          NINLIL_OK);
    return 0;
}
static int create(fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->source_rng = 11u;
    f->target_rng = 23u;
    f->expected.source_identity[0] = 1u;
    f->expected.peer_identity[0] = 2u;
    f->expected.authority[0] = 3u;
    f->expected.authority_epoch = 7u;
    f->expected.membership_epoch = f->expected.binding_epoch = 1u;
    f->current = f->expected;
    CHECK(test_make_directory(f->directory, sizeof(f->directory)) == 0);
    CHECK(test_make_path(f->source, sizeof(f->source), f->directory,
                         "source") == 0);
    CHECK(test_make_path(f->target, sizeof(f->target), f->directory,
                         "target") == 0);
    test_policy_init(&f->policy, 256u, 32u);
    test_link_init(&f->link, 320u);
    test_link_bind(&f->link, 0u, &f->source_config.link);
    test_link_bind(&f->link, 1u, &f->target_config.link);
    f->source_config.node_id = 1u;
    f->target_config.node_id = 2u;
    f->source_config.journal_location = f->source;
    f->target_config.journal_location = f->target;
    f->source_config.random = (ninlil_random){test_rng_fill, &f->source_rng};
    f->target_config.random = (ninlil_random){test_rng_fill, &f->target_rng};
    f->source_config.policy_lookup = f->target_config.policy_lookup =
        test_policy_lookup;
    f->source_config.policy_ctx = f->target_config.policy_ctx = &f->policy;
    f->source_config.binding_lookup = lookup;
    f->source_config.binding_ctx = f;
    CHECK(ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT,
                                       &f->source_config.profile) == 0);
    f->target_config.profile = f->source_config.profile;
    CHECK(ninlil_open(&f->sender, &f->source_config) == 0);
    CHECK(ninlil_bind_storage(f->sender, f->expected.source_identity, 1) == 0);
    CHECK(ninlil_open(&f->receiver, &f->target_config) == 0);
    ninlil_submission_defaults(&f->request);
    test_fill_id(&f->request.idempotency_key, 9u);
    f->request.target = 2u;
    f->request.service = 256u;
    f->request.required_evidence = NINLIL_EVIDENCE_APPLICATION_ACCEPTED;
    f->request.payload = (const uint8_t *)"bound";
    f->request.payload_len = 5u;
    return 0;
}
static void close_fixture(fixture *f)
{
    ninlil_close(f->sender);
    ninlil_close(f->receiver);
    test_remove_directory(f->directory, f->source, f->target);
}
static int sent(fixture *f)
{
    for (unsigned int i = 0u; i < 20u && !f->link.endpoint[1].count; i++)
        CHECK(ninlil_step(f->sender) == NINLIL_OK);
    CHECK(f->link.endpoint[1].count > 0u);
    return 0;
}
static int finish_delivery(fixture *f, const ninlil_id *id)
{
    ninlil_info info;
    for (unsigned int i = 0u; i < 100u; i++) {
        ninlil_inbound in;
        int rc;
        CHECK(ninlil_step(f->sender) == NINLIL_OK);
        CHECK(ninlil_step(f->receiver) == NINLIL_OK);
        rc = ninlil_receive(f->receiver, &in);
        CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_EMPTY);
        if (rc == NINLIL_OK)
            CHECK(ninlil_application_accept(f->receiver, &in.message_id) == 0);
        CHECK(ninlil_query(f->sender, id, &info) == NINLIL_OK);
        if (info.outcome == NINLIL_OUTCOME_SATISFIED)
            return 0;
    }
    return 1;
}
static int identity_fences(void)
{
    fixture f;
    ninlil_id id, same, untouched;
    ninlil_info info;
    ninlil_delivery_binding saved, wrong;
    uint8_t packet[320], receipt[26];
    size_t length;
    CHECK(create(&f) == 0);
    CHECK(ninlil_submit_bound(f.sender, &f.request, &f.expected, &id) == 0);
    CHECK(ninlil_query_binding(f.sender, &id, &saved) == 0);
    CHECK(ninlil_delivery_binding_equal(&saved, &f.expected));
    CHECK(ninlil_submit_bound(f.sender, &f.request, &f.expected, &same) == 0 &&
          ninlil_id_equal(&id, &same));
    test_fill_id(&untouched, 77u);
    same = untouched;
    CHECK(ninlil_submit(f.sender, &f.request, &same) == NINLIL_ERR_CONFLICT);
    CHECK(ninlil_id_equal(&same, &untouched));
    wrong = f.expected;
    wrong.binding_epoch++;
    CHECK(ninlil_submit_bound(f.sender, &f.request, &wrong, &same) ==
          NINLIL_ERR_CONFLICT);
    f.current.peer_identity[0] = 99u;
    for (unsigned int i = 0u; i < 6u; i++) {
        int rc = ninlil_step(f.sender);
        CHECK(rc == NINLIL_OK || rc == NINLIL_ERR_UNAUTHORIZED);
    }
    CHECK(!f.link.endpoint[1].count);
    CHECK(ninlil_query(f.sender, &id, &info) == 0 &&
          !info.remote_boundary_may_have_been_reached);
    f.current = f.expected;
    CHECK(sent(&f) == 0);
    length = f.link.endpoint[1].lengths[0];
    memcpy(packet, f.link.endpoint[1].packets[0], length);
    CHECK(ninlil_transmit_check(f.sender, packet, length) == 0);
    f.current.membership_epoch++;
    CHECK(ninlil_transmit_check(f.sender, packet, length) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_wire_encode_receipt(
              receipt, 2u, 1u, &id, NINLIL_RECEIPT_EVIDENCE,
              NINLIL_EVIDENCE_APPLICATION_ACCEPTED) == sizeof(receipt));
    CHECK(ninlil_ingest(f.sender, receipt, sizeof(receipt)) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_query(f.sender, &id, &info) == 0 &&
          info.outcome == NINLIL_OUTCOME_ACTIVE);
    ninlil_close(f.sender);
    f.sender = NULL;
    f.source_config.binding_lookup = NULL;
    CHECK(open_source(&f) == 0);
    CHECK(ninlil_query_binding(f.sender, &id, &saved) == 0);
    CHECK(ninlil_transmit_check(f.sender, packet, length) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_submit_bound(f.sender, &f.request, &f.expected, &same) == 0 &&
          ninlil_id_equal(&id, &same));
    ninlil_close(f.sender);
    f.sender = NULL;
    f.source_config.binding_lookup = lookup;
    f.current = f.expected;
    CHECK(open_source(&f) == 0);
    CHECK(ninlil_collect(f.sender) == 0);
    CHECK(ninlil_query_binding(f.sender, &id, &saved) == 0 &&
          ninlil_delivery_binding_equal(&saved, &f.expected));
    CHECK(finish_delivery(&f, &id) == 0);
    CHECK(ninlil_retire_completed(f.sender) == NINLIL_ERR_BUSY);
    CHECK(ninlil_collect(f.sender) == 0);
    ninlil_close(f.sender);
    f.sender = NULL;
    CHECK(open_source(&f) == 0);
    CHECK(ninlil_query_binding(f.sender, &id, &saved) == 0);
    for (uint8_t key = 10u; key < 142u; key++) {
        ninlil_id legacy;
        test_fill_id(&f.request.idempotency_key, key);
        CHECK(ninlil_submit(f.sender, &f.request, &legacy) == 0);
        CHECK(finish_delivery(&f, &legacy) == 0);
        CHECK(ninlil_query_binding(f.sender, &id, &saved) ==
              0); /* Pinned, not evicted. */
    }
    CHECK(ninlil_release_bound(f.sender, &id) == 0);
    CHECK(ninlil_release_bound(f.sender, &id) == 0);
    CHECK(ninlil_collect(f.sender) == 0);
    ninlil_close(f.sender);
    f.sender = NULL;
    CHECK(open_source(&f) == 0);
    CHECK(ninlil_find_archive_id(f.sender, &id)->binding_released);
    CHECK(ninlil_retire_completed(f.sender) == 0);
    close_fixture(&f);
    return 0;
}
static int crash_boundaries(void)
{
    for (unsigned int point = 0u; point < 2u; point++) {
        fixture f;
        ninlil_id id, again;
        ninlil_delivery_binding saved;
        pid_t child;
        int status;
        CHECK(create(&f) == 0);
        ninlil_close(f.sender);
        f.sender = NULL;
        child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            if (open_source(&f) != 0)
                _exit(71);
            crash_after_type =
                point == 0u ? NINLIL_JRN_OUT_BINDING : NINLIL_JRN_OUT_CREATE;
            (void)ninlil_submit_bound(f.sender, &f.request, &f.expected, &id);
            _exit(72);
        }
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status));
        CHECK((unsigned int)WEXITSTATUS(status) ==
              80u + (point == 0u ? NINLIL_JRN_OUT_BINDING
                                 : NINLIL_JRN_OUT_CREATE));
        CHECK(open_source(&f) == 0);
        CHECK(f.sender->outbound_live == (point == 0u ? 0u : 1u));
        CHECK(ninlil_submit_bound(f.sender, &f.request, &f.expected, &id) == 0);
        CHECK(ninlil_query_binding(f.sender, &id, &saved) == 0);
        ninlil_close(f.sender);
        f.sender = NULL;
        CHECK(open_source(&f) == 0);
        CHECK(ninlil_submit_bound(f.sender, &f.request, &f.expected, &again) ==
                  0 &&
              ninlil_id_equal(&id, &again));
        CHECK(finish_delivery(&f, &id) == 0);
        close_fixture(&f);
    }
    return 0;
}
static int corrupt_binding(void)
{
    fixture f;
    ninlil_id id;
    ninlil_info info, before;
    struct stat st;
    uint8_t *bytes;
    int fd;
    size_t found = 0u;
    CHECK(create(&f) == 0);
    CHECK(ninlil_submit_bound(f.sender, &f.request, &f.expected, &id) == 0);
    fd = open(f.source, O_RDWR);
    CHECK(fd >= 0 && fstat(fd, &st) == 0);
    CHECK(st.st_size > 0 && st.st_size < 1024 * 1024);
    bytes = malloc((size_t)st.st_size);
    CHECK(bytes);
    CHECK(pread(fd, bytes, (size_t)st.st_size, 0) == st.st_size);
    for (size_t i = 0u; i + NINLIL_JRN_BINDING_BYTES <= (size_t)st.st_size; i++)
        if (!memcmp(bytes + i, "NDB\001", 4u) &&
            !memcmp(bytes + i + 4u, id.bytes, 16u)) {
            found = i;
            break;
        }
    CHECK(found);
    bytes[found + 52u] ^= 1u;
    CHECK(pwrite(fd, bytes + found + 52u, 1u, (off_t)(found + 52u)) == 1);
    CHECK(fdatasync(fd) == 0 && close(fd) == 0);
    free(bytes);
    memset(&info, 0xa5, sizeof(info));
    before = info;
    CHECK(ninlil_query(f.sender, &id, &info) == NINLIL_ERR_CORRUPT);
    CHECK(!memcmp(&info, &before, sizeof(info)));
    CHECK(ninlil_step(f.sender) == NINLIL_ERR_CORRUPT &&
          !f.link.endpoint[1].count);
    ninlil_close(f.sender);
    f.sender = NULL;
    CHECK(ninlil_open(&f.sender, &f.source_config) == NINLIL_ERR_CORRUPT &&
          !f.sender);
    close_fixture(&f);
    return 0;
}
int main(void)
{
    CHECK(identity_fences() == 0);
    CHECK(crash_boundaries() == 0);
    CHECK(corrupt_binding() == 0);
    puts("real Core: "
         "binding/retry/final-TX/receipt/replay/collection/retention/crash/"
         "corruption PASS");
    return 0;
}
