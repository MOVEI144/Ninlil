#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int enqueue(test_endpoint *endpoint, const uint8_t *data, size_t length)
{
    if (length > TEST_PACKET_MAX)
        return NINLIL_ERR_TOO_LARGE;
    if (endpoint->count >= TEST_QUEUE_SLOTS)
        return NINLIL_ERR_CAPACITY;
    memcpy(endpoint->packets[endpoint->count], data, length);
    endpoint->lengths[endpoint->count] = (uint16_t)length;
    endpoint->count++;
    return NINLIL_OK;
}

static int test_send(void *ctx, const uint8_t *data, size_t length)
{
    test_binding *binding = ctx;
    test_endpoint *source = &binding->link->endpoint[binding->side];
    test_endpoint *target = &binding->link->endpoint[binding->side ^ 1u];
    int rc;

    if (length > binding->link->mtu)
        return NINLIL_ERR_TOO_LARGE;
    if (source->drop_next > 0u) {
        source->drop_next--;
        return NINLIL_OK;
    }
    rc = enqueue(target, data, length);
    if (rc != NINLIL_OK)
        return rc;
    if (source->duplicate_next > 0u) {
        source->duplicate_next--;
        return enqueue(target, data, length);
    }
    return NINLIL_OK;
}

static int test_recv(void *ctx, uint8_t *buffer, size_t capacity,
                     size_t *length)
{
    test_binding *binding = ctx;
    test_endpoint *endpoint = &binding->link->endpoint[binding->side];
    size_t packet_length;
    uint8_t index;

    if (endpoint->count == 0u)
        return 0;
    packet_length = endpoint->lengths[0];
    if (packet_length > capacity)
        return NINLIL_ERR_TOO_LARGE;
    memcpy(buffer, endpoint->packets[0], packet_length);
    *length = packet_length;
    for (index = 1u; index < endpoint->count; index++) {
        memcpy(endpoint->packets[index - 1u], endpoint->packets[index],
               endpoint->lengths[index]);
        endpoint->lengths[index - 1u] = endpoint->lengths[index];
    }
    endpoint->count--;
    return 1;
}

void test_link_init(test_link *link, size_t mtu)
{
    memset(link, 0, sizeof(*link));
    link->mtu = mtu;
}

void test_link_bind(test_link *link, uint8_t side, ninlil_link *out)
{
    link->binding[side].link = link;
    link->binding[side].side = side;
    out->send = test_send;
    out->recv = test_recv;
    out->ctx = &link->binding[side];
    out->max_packet_size = link->mtu;
}

void test_link_drop_next(test_link *link, uint8_t side, uint8_t count)
{
    link->endpoint[side].drop_next = count;
}

void test_link_duplicate_next(test_link *link, uint8_t side, uint8_t count)
{
    link->endpoint[side].duplicate_next = count;
}

void test_policy_init(test_policy *policy, uint16_t service,
                      uint16_t maximum_live_messages)
{
    memset(policy, 0, sizeof(*policy));
    policy->role = NINLIL_ROLE_POWERED_ENDPOINT;
    policy->capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE |
                           NINLIL_CAP_POLL_DOWNLINK;
    policy->membership_epoch = 1u;
    policy->session_membership_epoch = 1u;
    policy->grant_count = 1u;
    policy->grants[0].service_id = service;
    policy->grants[0].directions = NINLIL_SERVICE_BOTH;
    policy->grants[0].maximum_payload_bytes = NINLIL_MAX_PAYLOAD;
    policy->grants[0].traffic_class_mask = UINT8_C(0x0F);
    policy->grants[0].maximum_live_messages = maximum_live_messages;
}

int test_policy_lookup(void *ctx, uint16_t peer, ninlil_peer_policy *policy)
{
    test_policy *configured = ctx;

    if (!configured || !policy || peer == 0u || peer == UINT16_MAX)
        return NINLIL_ERR_NOT_FOUND;
    memset(policy, 0, sizeof(*policy));
    policy->role = configured->role;
    policy->capabilities = configured->capabilities;
    policy->membership_epoch = configured->membership_epoch;
    policy->session_membership_epoch = configured->session_membership_epoch;
    policy->grants = configured->grants;
    policy->grant_count = configured->grant_count;
    return NINLIL_OK;
}

int test_rng_fill(void *ctx, uint8_t *buffer, size_t length)
{
    uint32_t *state = ctx;
    size_t index;

    for (index = 0u; index < length; index++) {
        *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
        buffer[index] = (uint8_t)(*state >> 24);
    }
    return 0;
}

void test_fill_id(ninlil_id *id, uint8_t value)
{
    memset(id->bytes, value, sizeof(id->bytes));
}

int test_make_directory(char *directory, size_t capacity)
{
    static const char template_value[] = "/tmp/ninlil-m1-XXXXXX";

    if (capacity < sizeof(template_value))
        return -1;
    memcpy(directory, template_value, sizeof(template_value));
    return mkdtemp(directory) ? 0 : -1;
}

int test_make_path(char *path, size_t capacity, const char *directory,
                   const char *name)
{
    int result = snprintf(path, capacity, "%s/%s", directory, name);
    return result >= 0 && (size_t)result < capacity ? 0 : -1;
}

void test_remove_directory(const char *directory, const char *first,
                           const char *second)
{
    if (first)
        (void)unlink(first);
    if (second)
        (void)unlink(second);
    (void)rmdir(directory);
}
