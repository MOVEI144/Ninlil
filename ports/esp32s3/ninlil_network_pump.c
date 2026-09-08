#include "ninlil_network_pump.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "ninlil_node.h"
#include <string.h>

int ninlil_esp_network_open(ninlil_esp_network_pump *p,
                            ninlil_sx1262_radio *radio, ninlil_routed *routed,
                            uint32_t budget)
{
    int64_t now = esp_timer_get_time();
    if (!p || !radio || !routed || now < 0)
        return NINLIL_ERR_INVALID;
    memset(p, 0, sizeof(*p));
    p->radio = radio;
    p->routed = routed;
    return ninlil_airtime_open(&p->scheduler, (uint64_t)now, budget, 0u);
}

int ninlil_esp_network_emit(void *ctx, uint16_t next,
                            ninlil_traffic_class traffic, const uint8_t *frame,
                            size_t length)
{
    ninlil_esp_network_pump *p = ctx;
    uint32_t airtime;
    int rc;
    if (!p || !p->radio || p->token == UINT64_MAX || length > 240u)
        return NINLIL_ERR_INVALID;
    rc = ninlil_sx1262_radio_airtime(p->radio, (uint16_t)length, &airtime);
    if (rc != NINLIL_OK)
        return rc;
    return ninlil_airtime_enqueue(&p->scheduler, ++p->token, next, traffic,
                                  airtime, frame, length);
}

int ninlil_esp_node_open(ninlil_esp_network_pump *p, ninlil_sx1262_radio *radio,
                         ninlil_node *node, uint32_t budget)
{
    int64_t now = esp_timer_get_time();
    if (!p || !radio || !node || now < 0)
        return NINLIL_ERR_INVALID;
    memset(p, 0, sizeof(*p));
    p->radio = radio;
    p->node = node;
    return ninlil_airtime_open(&p->scheduler, (uint64_t)now, budget, 0u);
}

static int control_frame(const uint8_t *frame, size_t length)
{
    return length >= NINLIL_SECURE_OVERHEAD &&
           memcmp(frame, "NS\001", 3u) == 0 && frame[31] == 1u;
}

static int receive(ninlil_esp_network_pump *p)
{
    unsigned int work;
    for (work = 0u; work < 4u; work++) {
        uint8_t frame[240];
        uint16_t length = 0u;
        ninlil_sx1262_rx_info info;
        int64_t now;
        int rc = ninlil_sx1262_radio_receive(p->radio, frame, sizeof(frame),
                                             &length, &info, 0u);
        if (rc == NINLIL_ERR_EMPTY || rc == NINLIL_ERR_TIMEOUT)
            return NINLIL_OK;
        if (rc == NINLIL_ERR_INVALID)
            continue;
        if (rc != NINLIL_OK)
            return rc;
        if (p->received != UINT32_MAX)
            p->received++;
        if (p->accept_rx && !p->accept_rx(p->rx_ctx, frame, length))
            continue;
        now = esp_timer_get_time();
        if (now < 0)
            return NINLIL_ERR_IO;
        if (p->node)
            rc = ninlil_node_receive(p->node, frame, length,
                                     (uint64_t)now / 1000u);
        else if (control_frame(frame, length) && p->control_receive)
            rc = p->control_receive(p->control_ctx, frame, length,
                                    (uint64_t)now / 1000u);
        else
            rc = ninlil_routed_receive(p->routed, frame, length,
                                       (uint64_t)now / 1000u);
        p->last_receive_result = rc;
        if (rc == NINLIL_ERR_IO || rc == NINLIL_ERR_CORRUPT ||
            rc == NINLIL_ERR_FAULT)
            return rc;
    }
    return NINLIL_OK;
}

int ninlil_esp_network_step(ninlil_esp_network_pump *p)
{
    const ninlil_airtime_job *job;
    int64_t now;
    int rc, completion;
    if (!p || !p->radio || (!p->routed && !p->node))
        return NINLIL_ERR_INVALID;
    rc = receive(p);
    if (rc != NINLIL_OK)
        return rc;
    now = esp_timer_get_time();
    if (now < 0)
        return NINLIL_ERR_IO;
    rc = p->node ? ninlil_node_step(p->node, (uint64_t)now / 1000u)
                 : ninlil_routed_poll(p->routed, (uint64_t)now / 1000u);
    if (rc != NINLIL_OK && rc != NINLIL_ERR_BUSY && rc != NINLIL_ERR_CAPACITY)
        return rc;
    if (!p->node && p->routed->core) {
        rc = ninlil_step(p->routed->core);
        if (rc != NINLIL_OK && rc != NINLIL_ERR_BUSY &&
            rc != NINLIL_ERR_CAPACITY && rc != NINLIL_ERR_UNAUTHORIZED &&
            rc != NINLIL_ERR_CONFLICT && rc != NINLIL_ERR_EXPIRED)
            return rc;
    }
    if (!p->scheduler.busy) {
        rc = ninlil_airtime_next(&p->scheduler, (uint64_t)now, &job);
        if (rc == NINLIL_ERR_EMPTY || rc == NINLIL_ERR_BUSY)
            return NINLIL_OK;
        if (rc != NINLIL_OK)
            return rc;
        /* Defer independently before CCA while continuing reception and

         * ownership work, including when a reply and flooded copy compete. */
        p->tx_at_us = (uint64_t)now + esp_random() % 100001u;
    }
    if ((uint64_t)now < p->tx_at_us)
        return NINLIL_OK;
    job = &p->scheduler.jobs[p->scheduler.active];
    now = esp_timer_get_time();
    if (now < 0 || (!p->node && (uint64_t)now / 1000u < p->routed->now_ms)) {
        (void)ninlil_airtime_discard_stale(&p->scheduler);
        return NINLIL_ERR_IO;
    }
    if (p->node)
        rc = ninlil_node_frame_current(p->node, job->frame, job->length,
                                       (uint64_t)now / 1000u);
    else {
        p->routed->now_ms = (uint64_t)now / 1000u;
        rc = control_frame(job->frame, job->length)
                 ? p->control_current
                       ? p->control_current(p->control_ctx, job->frame,
                                            job->length, (uint64_t)now / 1000u)
                       : NINLIL_ERR_UNAUTHORIZED
                 : ninlil_routed_frame_current(p->routed, job->frame,
                                               job->length);
    }
    if (rc != NINLIL_OK) {
        if (p->discarded != UINT32_MAX)
            p->discarded++;
        (void)ninlil_airtime_discard_stale(&p->scheduler);
        return rc;
    }
    /* This driver returns OK only after TX_DONE and restoring reception. */
    rc = ninlil_sx1262_radio_send(p->radio, job->frame, job->length);
    if (rc == NINLIL_OK && p->transmitted != UINT32_MAX)
        p->transmitted++;
    if (p->node) {
        int64_t completed = esp_timer_get_time();
        if (completed >= now)
            ninlil_node_transmitted(p->node, job->frame, job->length, rc,
                                    job->airtime_us,
                                    (uint64_t)completed / 1000u);
    }
    completion = ninlil_airtime_complete(
        &p->scheduler, rc == NINLIL_OK         ? NINLIL_OK
                       : rc == NINLIL_ERR_BUSY ? rc
                                               : NINLIL_ERR_IO);
    return completion == NINLIL_OK ? rc : completion;
}
