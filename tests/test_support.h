#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "ninlil.h"

#include <stddef.h>
#include <stdint.h>

#define TEST_QUEUE_SLOTS 32u
#define TEST_PACKET_MAX 320u

typedef struct test_policy {
    ninlil_service_grant grants[4];
    uint16_t grant_count;
    ninlil_role role;
    uint32_t capabilities;
    uint64_t membership_epoch;
    uint64_t session_membership_epoch;
} test_policy;

typedef struct test_endpoint {
    uint8_t packets[TEST_QUEUE_SLOTS][TEST_PACKET_MAX];
    uint16_t lengths[TEST_QUEUE_SLOTS];
    uint8_t count;
    uint8_t drop_next;
    uint8_t duplicate_next;
} test_endpoint;

typedef struct test_link test_link;

typedef struct test_binding {
    test_link *link;
    uint8_t side;
} test_binding;

struct test_link {
    test_endpoint endpoint[2];
    test_binding binding[2];
    size_t mtu;
};

void test_link_init(test_link *link, size_t mtu);
void test_link_bind(test_link *link, uint8_t side, ninlil_link *out);
void test_link_drop_next(test_link *link, uint8_t side, uint8_t count);
void test_link_duplicate_next(test_link *link, uint8_t side, uint8_t count);
void test_policy_init(test_policy *policy, uint16_t service,
                      uint16_t maximum_live_messages);
int test_policy_lookup(void *ctx, uint16_t peer, ninlil_peer_policy *policy);
int test_rng_fill(void *ctx, uint8_t *buffer, size_t length);
void test_fill_id(ninlil_id *id, uint8_t value);
int test_make_directory(char *directory, size_t capacity);
int test_make_path(char *path, size_t capacity, const char *directory,
                   const char *name);
void test_remove_directory(const char *directory, const char *first,
                           const char *second);

#endif
