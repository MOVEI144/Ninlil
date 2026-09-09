#include "ninlil_join.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check line %d: %s\n", __LINE__, #x);              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct store {
    ninlil_join_record records[16];
    size_t count;
    int failure;
    ninlil_join_grant approved;
} store;

static int commit_record(void *ctx, const ninlil_join_record *record)
{
    store *s = ctx;
    if (s->count == 16u)
        return NINLIL_ERR_CAPACITY;
    // Deliberately allow committed-but-reported-failed writes.
    s->records[s->count++] = *record;
    return s->failure ? NINLIL_ERR_IO : NINLIL_OK;
}
static int approve(void *ctx, const uint8_t id[32], ninlil_join_grant *grant)
{
    store *s = ctx;
    *grant = s->approved;
    memcpy(grant->identity, id, 32u);
    return NINLIL_OK;
}

int main(void)
{
    ninlil_join_authority a;
    ninlil_join_peer peers[65];
    store gateway, endpoint;
    ninlil_join_record accept, ack, decoded;
    ninlil_peer_policy policy;
    uint8_t id[32] = {1}, authority[16] = {7}, session[16] = {9};
    uint8_t bytes[NINLIL_JOIN_RECORD_MAX];
    size_t n, i;
    memset(&gateway, 0, sizeof(gateway));
    memset(&endpoint, 0, sizeof(endpoint));
    memcpy(gateway.approved.authority, authority, 16u);
    gateway.approved.node = 2u;
    gateway.approved.membership_epoch = 1u;
    gateway.approved.binding_epoch = 1u;
    gateway.approved.role = NINLIL_ROLE_POWERED_ENDPOINT;
    gateway.approved.capabilities =
        NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    gateway.approved.service_count = 1u;
    gateway.approved.services[0] =
        (ninlil_service_grant){256u, 96u, 8u, NINLIL_SERVICE_BOTH, 15u};
    CHECK(ninlil_join_open(&a, peers, 65u, authority, commit_record, &gateway,
                           approve, &gateway) == NINLIL_OK);
    CHECK(ninlil_join_begin(&a, id, 0u) == NINLIL_OK);
    CHECK(ninlil_join_prepare(&a, id, 1u, &accept) == NINLIL_ERR_STATE);
    CHECK(ninlil_join_authenticated(&a, id, session, 1u) == NINLIL_OK);
    gateway.approved.services[0].maximum_payload_bytes = 0u;
    CHECK(ninlil_join_prepare(&a, id, 2u, &accept) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(gateway.count == 0u);
    gateway.approved.services[0].maximum_payload_bytes = 96u;
    gateway.approved.capabilities |= NINLIL_CAP_GATEWAY_RADIO_HEAD;
    CHECK(ninlil_join_prepare(&a, id, 2u, &accept) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(gateway.count == 0u);
    gateway.approved.capabilities &= ~NINLIL_CAP_GATEWAY_RADIO_HEAD;
    CHECK(ninlil_join_prepare(&a, id, 2u, &accept) == NINLIL_OK &&
          gateway.count == 1u);
    CHECK(ninlil_join_policy(&a, 2u, &policy) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_join_prepare(&a, id, 3u, &accept) == NINLIL_OK &&
          gateway.count == 1u);
    n = ninlil_join_encode(&accept, bytes, sizeof(bytes));
    CHECK(n == 100u);
    CHECK(ninlil_join_decode(bytes, n, &decoded) == NINLIL_OK);
    bytes[75] = 255u;
    CHECK(ninlil_join_decode(bytes, n, &decoded) == NINLIL_ERR_INVALID);
    CHECK(ninlil_join_endpoint_commit(&accept, NULL, id, authority, session,
                                      commit_record, &endpoint,
                                      &ack) == NINLIL_OK);
    CHECK(ninlil_join_confirm(&a, &ack, session, 4u) == NINLIL_OK);
    CHECK(ninlil_join_confirm(&a, &ack, session, 5u) == NINLIL_OK &&
          gateway.count == 2u);
    CHECK(ninlil_join_policy(&a, 2u, &policy) == NINLIL_OK);
    CHECK(ninlil_policy_validate(&policy, NINLIL_JOIN_SERVICES_MAX) ==
          NINLIL_OK);
    CHECK(ninlil_join_begin(&a, id, 6u) == NINLIL_ERR_BUSY);
    CHECK(ninlil_join_open(&a, peers, 65u, authority, commit_record, &gateway,
                           approve, &gateway) == NINLIL_OK);
    for (i = 0u; i < gateway.count; i++)
        CHECK(ninlil_join_restore(&a, &gateway.records[i]) == NINLIL_OK);
    CHECK(ninlil_join_policy(&a, 2u, &policy) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_join_begin(&a, id, 10u) == NINLIL_OK);
    CHECK(ninlil_join_authenticated(&a, id, session, 11u) ==
          NINLIL_ERR_UNAUTHORIZED);
    session[0]++;
    CHECK(ninlil_join_authenticated(&a, id, session, 11u) == NINLIL_OK);
    decoded = ack;
    decoded.grant.binding_epoch++;
    CHECK(ninlil_join_resume(&a, &decoded, session, 12u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_join_resume(&a, &ack, session, 12u) == NINLIL_OK);
    CHECK(ninlil_join_revoke(&a, id) == NINLIL_OK);
    CHECK(ninlil_join_policy(&a, 2u, &policy) == NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_join_begin(&a, id, 20u) == NINLIL_OK);
    session[0]++;
    CHECK(ninlil_join_authenticated(&a, id, session, 21u) == NINLIL_OK);
    CHECK(ninlil_join_resume(&a, &ack, session, 22u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_join_prepare(&a, id, 22u, &accept) == NINLIL_ERR_UNAUTHORIZED);
    gateway.approved.membership_epoch++;
    gateway.failure = 1;
    CHECK(ninlil_join_prepare(&a, id, 23u, &accept) == NINLIL_ERR_IO &&
          a.poisoned);
    CHECK(ninlil_join_policy(&a, 2u, &policy) == NINLIL_ERR_UNAUTHORIZED);
    gateway.failure = 0;
    CHECK(ninlil_join_open(&a, peers, 65u, authority, commit_record, &gateway,
                           approve, &gateway) == NINLIL_OK);
    for (i = 0u; i < 64u; i++) {
        id[0] = (uint8_t)(i + 1u);
        CHECK(ninlil_join_begin(&a, id, 0u) == NINLIL_OK);
    }
    id[0] = 65u;
    CHECK(ninlil_join_begin(&a, id, 1u) == NINLIL_ERR_CAPACITY);
    ninlil_join_expire(&a, 30001u);
    CHECK(ninlil_join_begin(&a, id, 30002u) == NINLIL_OK);
    {
        ninlil_join_endpoint e, restored;
        ninlil_join_record newer, result;
        size_t before;
        CHECK(ninlil_join_endpoint_open(&e, ack.grant.identity, authority,
                                        commit_record, &endpoint) == NINLIL_OK);
        CHECK(ninlil_join_endpoint_restore(&e, &ack) == NINLIL_OK);
        newer = ack;
        newer.state = NINLIL_JOIN_PENDING;
        before = endpoint.count;
        CHECK(ninlil_join_endpoint_accept(&e, &newer, newer.transaction,
                                          &result) == NINLIL_OK);
        CHECK(endpoint.count == before);
        newer.grant.membership_epoch++;
        newer.transaction[0]++;
        CHECK(ninlil_join_endpoint_accept(&e, &newer, newer.transaction,
                                          &result) == NINLIL_OK);
        CHECK(ninlil_join_endpoint_open(&restored, ack.grant.identity,
                                        authority, commit_record,
                                        &endpoint) == NINLIL_OK);
        CHECK(ninlil_join_endpoint_restore(&restored, &ack) == NINLIL_OK);
        CHECK(ninlil_join_endpoint_restore(&restored, &result) == NINLIL_OK);
        CHECK(ninlil_join_endpoint_restore(&restored, &ack) ==
              NINLIL_ERR_CORRUPT);
    }
    puts("join durable activation/restart/revoke/epoch/quota/failure PASS");
    return 0;
}
