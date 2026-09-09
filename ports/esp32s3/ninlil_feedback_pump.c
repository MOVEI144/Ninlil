#include "ninlil_feedback_pump.h"
#include "ninlil_node.h"
#include <string.h>

static void reply(void *ctx, uint16_t peer, uint64_t token,
                   const uint8_t session[16], uint64_t now)
{
    ninlil_esp_network_pump *p = ctx;
    p->feedback_reply_result =
        ninlil_radio_feedback_reply(&p->feedback, peer, session, token, now);
}

static int suspend(void *ctx, uint64_t now)
{
    ninlil_esp_network_pump *p = ctx;
    return ninlil_radio_feedback_invalidate(&p->feedback, 0u, now);
}

int ninlil_esp_node_feedback_open(ninlil_esp_network_pump *p, int8_t minimum)
{
    ninlil_node_radio_observer observer = {reply, suspend, p};
    int rc;
    if (!p || !p->node || !p->radio || p->adaptive_power ||
        p->scheduler.busy || p->scheduler.waiting)
        return NINLIL_ERR_STATE;
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++)
        if (p->scheduler.jobs[i].used)
            return NINLIL_ERR_STATE;
    rc = ninlil_radio_feedback_open(&p->feedback, minimum,
                                     p->radio->profile.tx_power_dbm);
    if (rc == NINLIL_OK)
        rc = ninlil_node_observe_radio(p->node, &observer);
    if (rc == NINLIL_OK)
        p->adaptive_power = 2u;
    return rc;
}

void ninlil_esp_feedback_queued(ninlil_esp_network_pump *p, uint64_t token,
                                int64_t now)
{
    for (unsigned int i = 0u; i < NINLIL_AIRTIME_QUEUE_MAX; i++) {
        if (!p->scheduler.jobs[i].used || p->scheduler.jobs[i].token != token)
            continue;
        p->queued_at_us[i] = now >= 0 ? (uint64_t)now : 0u;
        p->queued_token[i] = now >= 0 ? token : 0u;
        return;
    }
}

int ninlil_esp_feedback_prepare(ninlil_esp_network_pump *p,
                                 const ninlil_airtime_job *job, uint64_t now)
{
    ninlil_node_radio_tx tx;
    ninlil_link_context proposed;
    unsigned int slot = p->scheduler.active;
    int rc = ninlil_node_radio_tx_context(p->node, job->frame, job->length, &tx);
    p->feedback_probe_token = 0u;
    p->feedback_queue_us = UINT32_MAX;
    if (rc == NINLIL_ERR_EMPTY) {
        /* NB destination is an END target, not necessarily a physical neighbor.
         * Never reduce the power of a bootstrap/recovery flood using that link. */
        return ninlil_sx1262_radio_power(p->radio,
                                          p->radio->profile.tx_power_dbm);
    }
    if (rc != NINLIL_OK)
        return rc;
    if (tx.peer != job->peer)
        return NINLIL_ERR_UNAUTHORIZED;
    rc = ninlil_radio_feedback_begin(&p->feedback, tx.peer, tx.profile,
                                       tx.session, now / 1000u, &proposed);
    if (rc != NINLIL_OK)
        return rc;
    p->feedback_probe_token = tx.probe_token;
    if (p->queued_token[slot] == job->token &&
        now >= p->queued_at_us[slot] && now - p->queued_at_us[slot] <= 30000000u)
        p->feedback_queue_us = (uint32_t)(now - p->queued_at_us[slot]);
    rc = ninlil_sx1262_radio_power(p->radio, proposed.power_dbm);
    if (rc != NINLIL_OK) {
        /* Even output staging failure is not a successful measurement. */
        int observed = ninlil_radio_feedback_finish(
            &p->feedback, rc == NINLIL_ERR_BUSY ? rc : NINLIL_ERR_IO,
            p->radio->applied_power_dbm, now / 1000u, 0u, 0u, 0u);
        return observed == NINLIL_OK ? rc : observed;
    }
    return NINLIL_OK;
}

int ninlil_esp_feedback_complete(ninlil_esp_network_pump *p,
                                  const ninlil_airtime_job *job, int result,
                                  uint64_t now)
{
    if (!p->feedback.pending)
        return NINLIL_OK; /* Unadapted bootstrap or recovery frame. */
    return ninlil_radio_feedback_finish(
        &p->feedback, result == NINLIL_OK || result == NINLIL_ERR_BUSY ||
                             result == NINLIL_ERR_TIMEOUT
                         ? result : NINLIL_ERR_IO,
        p->radio->applied_power_dbm, now, p->feedback_probe_token,
        job->airtime_us, p->feedback_queue_us);
}
