#include "ninlil_airtime.h"
#include "ninlil_join.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
static void airtime_starvation(void)
{
    ninlil_airtime_scheduler scheduler;
    const ninlil_airtime_job *job;
    uint8_t critical[200] = {1u}, normal[20] = {2u};
    unsigned int normals = 0u;
    REQUIRE(ninlil_airtime_open(&scheduler, 0u, 200000u, 0u) == NINLIL_OK);
    REQUIRE(ninlil_airtime_enqueue(&scheduler, 1u, 2u, NINLIL_TRAFFIC_CRITICAL,
                                   200000u, critical,
                                   sizeof(critical)) == NINLIL_OK);
    for (uint64_t i = 1u; i <= 2000u; i++) {
        REQUIRE(ninlil_airtime_enqueue(&scheduler, i + 1u, 3u,
                                       NINLIL_TRAFFIC_NORMAL, 10000u, normal,
                                       sizeof(normal)) == NINLIL_OK);
        REQUIRE(ninlil_airtime_next(&scheduler, i * 50000u, &job) == NINLIL_OK);
        REQUIRE(job->traffic == NINLIL_TRAFFIC_NORMAL);
        normals++;
        REQUIRE(ninlil_airtime_complete(&scheduler, NINLIL_OK) == NINLIL_OK);
    }
    {
        unsigned int pending = 0u;
        for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
            if (scheduler.jobs[i].used && scheduler.jobs[i].token == 1u)
                pending++;
        REQUIRE(pending == 1u);
    }
    printf("REPRODUCED airtime: %u normal packets sent over 100 seconds; "
           "continuously waiting critical packet never selected\n",
           normals);
}
typedef struct fixture {
    ninlil_join_grant grant;
    unsigned int commits;
} fixture;
static int approve(void *ctx, const uint8_t id[32], ninlil_join_grant *grant)
{
    fixture *f = ctx;
    REQUIRE(memcmp(id, f->grant.identity, 32u) == 0);
    *grant = f->grant;
    return NINLIL_OK;
}
static int commit(void *ctx, const ninlil_join_record *record)
{
    fixture *f = ctx;
    REQUIRE(record->state == NINLIL_JOIN_PENDING ||
            record->state == NINLIL_JOIN_ACTIVE);
    f->commits++;
    return NINLIL_OK;
}
static void join_invalid_policy(unsigned int variant)
{
    fixture f = {0};
    ninlil_join_authority authority;
    ninlil_join_peer peers[2];
    ninlil_join_record accept, ack;
    ninlil_peer_policy policy;
    uint8_t context[16] = {3u};
    memset(f.grant.identity, 1, sizeof(f.grant.identity));
    memset(f.grant.authority, 2, sizeof(f.grant.authority));
    f.grant.node = 2u;
    f.grant.membership_epoch = f.grant.binding_epoch = 1u;
    f.grant.role = NINLIL_ROLE_POWERED_ENDPOINT;
    f.grant.capabilities = NINLIL_CAP_APP_SEND | NINLIL_CAP_APP_RECEIVE;
    f.grant.service_count = 1u;
    f.grant.services[0] =
        (ninlil_service_grant){0x100u, 32u, 1u, NINLIL_SERVICE_BOTH, 15u};
    if (variant == 0u)
        f.grant.services[0].maximum_payload_bytes = 0u;
    else
        f.grant.capabilities |= NINLIL_CAP_GATEWAY_RADIO_HEAD;
    REQUIRE(ninlil_join_open(&authority, peers, 2u, f.grant.authority, commit,
                             &f, approve, &f) == NINLIL_OK);
    REQUIRE(ninlil_join_begin(&authority, f.grant.identity, 0u) == NINLIL_OK);
    REQUIRE(ninlil_join_authenticated(&authority, f.grant.identity, context,
                                      1u) == NINLIL_OK);
    REQUIRE(ninlil_join_prepare(&authority, f.grant.identity, 2u, &accept) ==
            NINLIL_OK);
    REQUIRE(ninlil_join_endpoint_commit(&accept, NULL, f.grant.identity,
                                        f.grant.authority, context, commit, &f,
                                        &ack) == NINLIL_OK);
    REQUIRE(ninlil_join_confirm(&authority, &ack, context, 3u) == NINLIL_OK);
    REQUIRE(ninlil_join_policy(&authority, 2u, &policy) == NINLIL_OK);
    REQUIRE(ninlil_policy_validate(&policy, 8u) == NINLIL_ERR_INVALID);
    printf("REPRODUCED Join policy: %s accepted through %u commits and "
           "session-ready, then Core policy validation rejects it\n",
           variant == 0u ? "zero payload maximum"
                         : "gateway capability on endpoint",
           f.commits);
}
int main(void)
{
    airtime_starvation();
    join_invalid_policy(0u);
    join_invalid_policy(1u);
    return 0;
}
