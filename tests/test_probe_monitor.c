#include "ninlil_probe_monitor.h"
#include "ninlil_airtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int window(ninlil_probe_monitor *m, uint16_t peer, const uint8_t session[16],
                   uint64_t start, uint64_t token, int8_t power, unsigned int losses)
{
    for (unsigned int i = 0u; i < 8u; i++) {
        uint64_t now = start + i * 4000u;
        int rc = ninlil_probe_monitor_tx(m, peer, session, power, token + i,
                                         now, 10000u + i, 5000u + i * 100u);
        if (rc != NINLIL_OK)
            return rc;
        if (i >= losses) {
            rc = ninlil_probe_monitor_reply(m, peer, session, token + i,
                                            now + 2500u);
            if (rc != NINLIL_OK)
                return rc;
        }
    }
    return NINLIL_OK;
}

int main(void)
{
    ninlil_probe_monitor m, before;
    ninlil_link_window w, old;
    const uint8_t session[16] = {1u}, other[16] = {2u};
    CHECK(ninlil_probe_monitor_open(&m, 1u) == NINLIL_OK);
    CHECK(window(&m, 2u, session, 1u, 1u, -3, 2u) == NINLIL_OK);
    memset(&w, 0xa5, sizeof(w)); old = w;
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 31000u, 1, &w) == NINLIL_ERR_EMPTY);
    CHECK(!memcmp(&w, &old, sizeof(w)));
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 31001u, 1, &w) == NINLIL_OK);
    CHECK(w.attempts == 8u && w.delivered == 6u && w.success_bits == 63u);
    CHECK(w.last_token == 8u && w.airtime_sum_us == 80028u && w.queue_max_us == 5700u);
    old = w;
    /* A new pending probe must not change the previous report's ACK identity. */
    CHECK(ninlil_probe_monitor_tx(&m, 2u, session, -3, 9u, 32001u, 10000u, 9000u) == 0);
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 33001u, 1, &w) == 0);
    CHECK(w.last_token == 8u && w.success_bits == old.success_bits);
    CHECK(ninlil_probe_monitor_ack(&m, 2u, 9u, 63u) == NINLIL_ERR_STATE);
    CHECK(ninlil_probe_monitor_ack(&m, 2u, 8u, 63u) == 0);
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 33001u, 1, &w) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 33001u, 0, &w) == 0);
    CHECK(ninlil_probe_monitor_reply(&m, 2u, other, 9u, 34000u) == NINLIL_ERR_STATE);
    CHECK(ninlil_probe_monitor_reply(&m, 2u, session, 9u, 35001u) == NINLIL_ERR_EXPIRED);
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 35001u, 1, &w) == 0);
    CHECK(w.first_sequence == 2u && w.last_sequence == 9u && w.last_token == 9u);
    CHECK(w.success_bits == 126u && w.delivered == 6u);
    /* Power/session changes invalidate the prior series, even mid-reply window. */
    CHECK(ninlil_probe_monitor_tx(&m, 2u, other, -6, 10u, 35002u, 10000u, 0u) == 0);
    CHECK(ninlil_probe_monitor_read(&m, 2u, session, 35002u, 0, &w) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_probe_monitor_read(&m, 2u, other, 35002u, 0, &w) == NINLIL_ERR_EMPTY);
    CHECK(m.peers[0].generation == 2u);
    CHECK(ninlil_probe_monitor_pause(&m, 36000u) == 0);
    CHECK(ninlil_probe_monitor_read(&m, 2u, other, 90000u, 0, &w) == NINLIL_ERR_EMPTY);
    CHECK(!m.peers[0].metrics.pending && !m.peers[0].metrics.consecutive_losses);
    CHECK(window(&m, 2u, other, 100000u, 20u, -6, 0u) == 0);
    CHECK(ninlil_probe_monitor_read(&m, 2u, other, 131000u, 1, &w) == 0);
    CHECK(w.first_sequence == 11u && w.last_sequence == 18u && w.context.generation == 3u);
    CHECK(ninlil_probe_monitor_read(&m, 2u, other, 220001u, 0, &w) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_probe_monitor_tx(&m, 2u, other, -6, 40u, 230000u, 10000u, UINT64_MAX) == NINLIL_ERR_EXPIRED);
    CHECK(m.skipped_measurements == 1u);
    before = m;
    CHECK(ninlil_probe_monitor_pause(&m, 1u) == NINLIL_ERR_INVALID);
    CHECK(!memcmp(&before, &m, sizeof(m)));
    CHECK(ninlil_probe_monitor_open(&m, 1u) == 0);
    for (uint16_t i = 1u; i <= 16u; i++)
        CHECK(ninlil_probe_monitor_tx(&m, i, session, -3, i, 1u, 10000u, 0u) == 0);
    CHECK(ninlil_probe_monitor_tx(&m, 17u, session, -3, 17u, 1u, 10000u, 0u) == NINLIL_ERR_CAPACITY);
    CHECK(ninlil_probe_monitor_tx(&m, 17u, session, -3, 17u, 200000u, 10000u, 0u) == 0);
    printf("closed-report monitor ACK/session/power/sleep/capacity PASS; bytes=%zu\n", sizeof(m));
    return 0;
}
