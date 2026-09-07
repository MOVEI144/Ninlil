#include "lab.h"
#include <psa/crypto.h>
#include <string.h>

static int deliver(lab *l, unsigned int sequence, uint16_t target)
{
    ninlil_submission request;
    ninlil_id id;
    ninlil_info info;
    uint8_t payload[64];
    unsigned int tick, count = 0u;
    int recovery = sequence == 19u && l->adaptive, switched = 0;
    uint8_t old_context[16];
    memcpy(old_context, l->nodes[0].sessions[3][0].session.material.fingerprint,
           16u);
    if (recovery)
        l->offline[3] = 1u;
    memset(payload, (int)sequence, sizeof(payload));
    ninlil_submission_defaults(&request);
    request.idempotency_key.bytes[0] = (uint8_t)sequence;
    request.idempotency_key.bytes[1] = (uint8_t)target;
    request.target = target;
    request.service = 256u;
    request.payload = payload;
    request.payload_len = sizeof(payload);
    REQUIRE(ninlil_routed_apply_rto(&l->nodes[0].routed, target, 10u));
    REQUIRE(ninlil_submit(l->nodes[0].core, &request, &id));
    for (tick = 0u; tick < 1000u; tick++) {
        ninlil_inbound inbound;
        REQUIRE(lab_tick(l));
        if (recovery && !switched) {
            unsigned int slot;
            for (slot = 0u; slot < 16u; slot++)
                if (l->nodes[1].slots[slot].used)
                    break;
            if (slot < 16u) {
                ninlil_network_plan old;
                REQUIRE(ninlil_query(l->nodes[0].core, &id, &info));
                ASSERT(info.outcome == NINLIL_OUTCOME_ACTIVE &&
                       info.latest_evidence == NINLIL_EVIDENCE_NONE);
                REQUIRE(ninlil_coordinator_route(&l->coordinator, 1u, 4u,
                                                 l->now_ms, &old));
                l->offline[1] = 1u;
                l->offline[3] = 0u;
                /* An absent Relay cannot confirm release: wait out its lease.
                 * Virtual restart-safe time advances; no wall-clock sleeping.
                 */
                l->now_ms = old.valid_until_ms + 1u;
                REQUIRE(lab_refresh(l));
                REQUIRE(lab_plan(l, 1u, 4u, 2u));
                REQUIRE(lab_plan(l, 4u, 1u, 2u));
                REQUIRE(lab_rekey(l, 4u));
                switched = 1;
            }
        }
        if (ninlil_receive(l->nodes[target - 1u].core, &inbound) == NINLIL_OK) {
            ASSERT(inbound.payload_len == sizeof(payload) &&
                   memcmp(inbound.payload, payload, sizeof(payload)) == 0);
            REQUIRE(ninlil_application_accept(l->nodes[target - 1u].core,
                                              &inbound.message_id));
            count++;
        }
        REQUIRE(ninlil_query(l->nodes[0].core, &id, &info));
        if (info.outcome == NINLIL_OUTCOME_SATISFIED && count)
            break;
    }
    if (tick >= 1000u || count != 1u)
        fprintf(stderr,
                "delivery seq=%u target=%u count=%u outcome=%d evidence=%d "
                "rejects=%u,%u,%u,%u attempts=%llu lost=%llu\n",
                sequence, target, count, (int)info.outcome,
                (int)info.latest_evidence, l->nodes[0].routed.rejected,
                l->nodes[1].routed.rejected, l->nodes[2].routed.rejected,
                l->nodes[3].routed.rejected,
                (unsigned long long)l->physical_attempts,
                (unsigned long long)l->dropped);
    ASSERT(tick < 1000u && count == 1u);
    ASSERT(info.latest_evidence >= NINLIL_EVIDENCE_REMOTE_STORED);
    if (recovery) {
        const uint8_t *fresh =
            l->nodes[0].sessions[3][0].session.material.fingerprint;
        int rc;
        ASSERT(switched);
        l->offline[1] = 0u;
        ASSERT(ninlil_relay_return_to_source(&l->nodes[1].relay, 1u, 4u,
                                             old_context, old_context) ==
               NINLIL_ERR_UNAUTHORIZED);
        /* Authenticated source recovery confirmation: its original Core
         * journal remained open and now holds the actual terminal evidence. */
        for (unsigned int work = 0u; work < 16u; work++) {
            rc = ninlil_relay_return_to_source(&l->nodes[1].relay, 1u, 4u,
                                               old_context, fresh);
            if (rc == NINLIL_ERR_EMPTY)
                break;
            REQUIRE(rc);
        }
        puts("Relay unavailable -> lease fence -> alternate route -> fresh "
             "session -> original message completion PASS");
    }
    return 0;
}

static int scenario(lab *l, int adaptive, uint64_t *ticks, uint64_t *attempts)
{
    unsigned int seq;
    REQUIRE(lab_open(l, adaptive));
    l->drop_ack = 1;
    for (seq = 1u; seq <= 18u; seq++)
        REQUIRE(deliver(l, seq, (uint16_t)(2u + seq % 3u)));
    *ticks = l->ticks;
    *attempts = l->physical_attempts;
    if (adaptive) {
        ninlil_remove_status status;
        ninlil_peer_policy policy;
        unsigned int tick;
        REQUIRE(deliver(l, 19u, 4u));
        /* Empty custody plus removal of every dependent route is required. */
        REQUIRE(ninlil_relay_drain(&l->nodes[1].relay, 1));
        for (tick = 0u;
             tick < 200u && !ninlil_relay_ready_remove(&l->nodes[1].relay);
             tick++)
            REQUIRE(lab_tick(l));
        ASSERT(ninlil_relay_ready_remove(&l->nodes[1].relay));
        REQUIRE(lab_plan(l, 1u, 4u, 2u));
        REQUIRE(lab_plan(l, 4u, 1u, 2u));
        REQUIRE(ninlil_coordinator_remove_status(&l->coordinator, 2u, l->now_ms,
                                                 1, &status));
        /* Flows whose endpoint is node 2 still need explicit retirement. */
        ASSERT(status.dependent_flows > 0u && !status.ready);
        for (unsigned int flow_index = 0u;
             flow_index < NINLIL_NETWORK_FLOWS_MAX; flow_index++) {
            const ninlil_network_plan *plan =
                &l->coordinator.flows[flow_index].active;
            uint16_t source, target_node;
            if (!plan->epoch)
                continue;
            source = plan->path.nodes[0];
            target_node = plan->path.nodes[plan->path.count - 1u];
            if (source == 2u || target_node == 2u)
                REQUIRE(ninlil_coordinator_retire(
                    &l->coordinator, source, target_node, plan->epoch,
                    l->now_ms, NINLIL_TIME_RESTART_SAFE, 1));
        }
        REQUIRE(ninlil_coordinator_remove_status(&l->coordinator, 2u, l->now_ms,
                                                 1, &status));
        ASSERT(status.ready && status.dependent_flows == 0u);
        REQUIRE(
            ninlil_join_revoke(&l->authority, l->nodes[1].identity.identity));
        ASSERT(ninlil_join_policy(&l->authority, 2u, &policy) ==
               NINLIL_ERR_UNAUTHORIZED);
        REQUIRE(deliver(l, 20u, 4u));
        ninlil_coordinator_enable(&l->coordinator, 0);
        REQUIRE(deliver(l, 21u, 4u));
    }
    printf("%s: 18 durable deliveries, ticks=%llu physical_attempts=%llu "
           "lost=%llu PASS\n",
           adaptive ? "adaptive" : "fixed", (unsigned long long)*ticks,
           (unsigned long long)*attempts, (unsigned long long)l->dropped);
    lab_close(l);
    return 0;
}

int main(int argc, char **argv)
{
    static lab l;
    uint64_t fixed_ticks = 0u, adaptive_ticks = 0u, fixed_attempts = 0u,
             adaptive_attempts = 0u;
    ASSERT(psa_crypto_init() == PSA_SUCCESS);
    if (argc == 2 && strcmp(argv[1], "star") == 0) {
        REQUIRE(scenario(&l, 0, &fixed_ticks, &fixed_attempts));
        return 0;
    }
    REQUIRE(scenario(&l, 0, &fixed_ticks, &fixed_attempts));
    REQUIRE(scenario(&l, 1, &adaptive_ticks, &adaptive_attempts));
    ASSERT(adaptive_ticks < fixed_ticks && adaptive_attempts < fixed_attempts);
    puts("Generated distinct device credentials -> EDHOC -> durable Join -> "
         "E2E/hop AEAD -> "
         "durable Core/Relay -> airtime scheduler, fixed/adaptive comparison "
         "PASS");
    puts("Host deterministic link model only; no RF transmission or physical "
         "acceptance.");
    return 0;
}
