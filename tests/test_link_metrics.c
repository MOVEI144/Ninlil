#include "ninlil_link_metrics.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)
int main(void)
{
    ninlil_link_metrics m, before;
    ninlil_link_window w;
    ninlil_link_context c = {
        .generation = 1, .profile = 1, .power_dbm = -3, .session = {1}};
    ninlil_link_metrics_open(&m);
    for (uint64_t i = 0; i < 8; i++) {
        uint64_t now = i * 4000 + 1;
        CHECK(ninlil_link_metrics_tx(&m, &c, i + 1, now, 100000,
                                     (uint32_t)i * 1000) == 0);
        before = m;
        CHECK(ninlil_link_metrics_tx(&m, &c, i + 1, now, 100000,
                                     (uint32_t)i * 1000) == 0);
        CHECK(!memcmp(&m, &before, sizeof(m)));
        CHECK(ninlil_link_metrics_read(&m, now, &w) == NINLIL_ERR_EMPTY);
        CHECK(ninlil_link_metrics_reply(&m, 99, now + 1) ==
              NINLIL_ERR_NOT_FOUND);
        CHECK(ninlil_link_metrics_reply(&m, i + 1, now + 100) == 0);
        CHECK(ninlil_link_metrics_reply(&m, i + 1, now + 100) == 0);
        CHECK(ninlil_link_metrics_tick(&m, now + 2999) == 0);
        CHECK(ninlil_link_metrics_read(&m, now + 2999, &w) == NINLIL_ERR_EMPTY);
        CHECK(ninlil_link_metrics_tick(&m, now + 3000) == 0);
    }
    CHECK(ninlil_link_metrics_read(&m, 31001, &w) == 0);
    CHECK(w.attempts == 8 && w.delivered == 8 && w.first_sequence == 1 &&
          w.last_sequence == 8);
    CHECK(w.queue_max_us == 7000 && w.airtime_sum_us == 800000);
    CHECK(ninlil_link_metrics_read(&m, 120002, &w) == NINLIL_ERR_EMPTY);
    /* Fresh setting and session cannot inherit even a perfect old window. */
    c.generation++;
    c.power_dbm = -6;
    CHECK(ninlil_link_metrics_tx(&m, &c, 10, 40000, 10000, 0) == 0);
    CHECK(ninlil_link_metrics_read(&m, 40000, &w) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_link_metrics_reply(&m, 10, 43000) == NINLIL_ERR_EXPIRED);
    CHECK(ninlil_link_metrics_tick(&m, 43000) == 0 &&
          m.consecutive_losses == 1);
    CHECK(ninlil_link_metrics_tx(&m, &c, 11, 44000, 10000, 0) == 0);
    CHECK(ninlil_link_metrics_tick(&m, 47000) == 0 &&
          m.consecutive_losses == 2);
    c.session[0] = 2;
    CHECK(ninlil_link_metrics_tx(&m, &c, 12, 48000, 10000, 0) == 0);
    CHECK(m.attempts == 0 && m.consecutive_losses == 0);
    before = m;
    CHECK(ninlil_link_metrics_tx(&m, &c, 13, 48001, 10000, 0) ==
          NINLIL_ERR_BUSY);
    CHECK(!memcmp(&m, &before, sizeof(m)));
    CHECK(ninlil_link_metrics_tick(&m, 47000) == NINLIL_ERR_INVALID);
    CHECK(!memcmp(&m, &before, sizeof(m)));
    CHECK(ninlil_link_metrics_tx(&m, &c, 13, UINT64_MAX, 10000, 0) ==
          NINLIL_ERR_INVALID);
    CHECK(!memcmp(&m, &before, sizeof(m)));
    CHECK(ninlil_link_metrics_tick(&m, 51000) == 0);
    /* Sparse trials discard a stale partial window instead of rejuvenating it.
     */
    CHECK(ninlil_link_metrics_tx(&m, &c, 14, 200000, 10000, 0) == 0);
    CHECK(ninlil_link_metrics_tick(&m, 203000) == 0 && m.attempts == 1);
    m.sequence = UINT64_MAX;
    before = m;
    CHECK(ninlil_link_metrics_tx(&m, &c, 15, 204000, 10000, 0) ==
          NINLIL_ERR_CAPACITY);
    CHECK(!memcmp(&m, &before, sizeof(m)));
    puts("closed non-overlapping probe windows/context/freshness/boundaries "
         "PASS");
    return 0;
}
