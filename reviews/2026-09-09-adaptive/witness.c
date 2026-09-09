/* Baseline witnesses, not acceptance tests for a future scheduler. */
#include "ninlil_airtime.h"
#include "ninlil_radio_adapt.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void class_airtime(void)
{
    ninlil_airtime_scheduler s;
    const ninlil_airtime_job *j;
    const uint8_t frame = 1;
    uint64_t token = 1, airtime[4] = {0};
    unsigned int count[4] = {0};
    assert(ninlil_airtime_open(&s, 0, 1000000, 0) == NINLIL_OK);
    for (unsigned int cls = 0; cls < 4; cls++) {
        assert(ninlil_airtime_enqueue(
                   &s, token++, (uint16_t)(cls + 1),
                   (ninlil_traffic_class)cls, cls == 3 ? 400000 : 10000,
                   &frame, 1) == NINLIL_OK);
    }
    for (unsigned int turn = 0; turn < 16; turn++) {
        assert(ninlil_airtime_next(&s, (uint64_t)(turn + 1) * 1000000, &j) ==
               NINLIL_OK);
        unsigned int cls = (unsigned int)j->traffic;
        uint32_t cost = j->airtime_us;
        count[cls]++;
        airtime[cls] += cost;
        assert(ninlil_airtime_complete(&s, NINLIL_OK) == NINLIL_OK);
        assert(ninlil_airtime_enqueue(&s, token++, (uint16_t)(cls + 1),
                                     (ninlil_traffic_class)cls, cost, &frame,
                                     1) == NINLIL_OK);
    }
    assert(count[0] == 8 && count[1] == 4 && count[2] == 3 && count[3] == 1);
    printf("class_frames=%u,%u,%u,%u class_airtime_us=%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 "\n",
           count[0], count[1], count[2], count[3], airtime[0], airtime[1],
           airtime[2], airtime[3]);
}

static void waiting_blocks_critical(void)
{
    ninlil_airtime_scheduler s, before;
    const ninlil_airtime_job *j = NULL;
    const uint8_t frame = 1;
    assert(ninlil_airtime_open(&s, 0, 400000, 0) == NINLIL_OK);
    assert(ninlil_airtime_enqueue(&s, 1, 1, NINLIL_TRAFFIC_BULK, 400000,
                                 &frame, 1) == NINLIL_OK);
    assert(ninlil_airtime_next(&s, 100000, &j) == NINLIL_ERR_EMPTY);
    assert(ninlil_airtime_enqueue(&s, 2, 2, NINLIL_TRAFFIC_CRITICAL, 10000,
                                 &frame, 1) == NINLIL_OK);
    int rc = ninlil_airtime_next(&s, 110000, &j);
    assert(rc == NINLIL_ERR_EMPTY && s.credit_us == 44000 && s.waiting &&
           !s.busy);
    printf("waiting_rc=%d credit_us=%u critical_cost_us=10000 "
           "selected_class=%d\n",
           rc, s.credit_us, (int)s.jobs[s.active].traffic);
    before = s;
    assert(ninlil_airtime_enqueue(&s, 3, 3, (ninlil_traffic_class)-1, 10000,
                                 &frame, 1) == NINLIL_ERR_INVALID);
    assert(memcmp(&s, &before, sizeof(s)) == 0);
    assert(ninlil_airtime_next(&s, 1000000, &j) == NINLIL_OK &&
           j->traffic == NINLIL_TRAFFIC_BULK);
}

static void power_boundaries(void)
{
    ninlil_radio_adapt s;
    int8_t power = 0;
    assert(ninlil_radio_adapt_open(&s, -9, -3) == NINLIL_OK);
    assert(ninlil_radio_adapt_observe(&s, 1000, 1000, 8, 8, &power) ==
               NINLIL_OK &&
           power == -3);
    assert(ninlil_radio_adapt_observe(&s, 10000, 10000, 8, 8, &power) ==
               NINLIL_OK &&
           power == -3);
    assert(ninlil_radio_adapt_observe(&s, 30000, 30000, 8, 8, &power) ==
               NINLIL_OK &&
           power == -6);
    assert(ninlil_radio_adapt_observe(&s, 30001, 30000, 8, 8, &power) ==
               NINLIL_OK &&
           s.good == 0);
    assert(ninlil_radio_adapt_observe(&s, 40000, 40000, 8, 6, &power) ==
               NINLIL_OK &&
           power == -3);
    assert(ninlil_radio_adapt_observe(&s, 100001, 40000, 8, 8, &power) ==
               NINLIL_OK &&
           power == -3);
    puts("power_drop_dbm=-6 loss_fallback_dbm=-3 stale_fallback_dbm=-3 "
         "duplicate_not_recounted=1");
}

int main(void)
{
    class_airtime();
    waiting_blocks_critical();
    power_boundaries();
    puts("baseline_witnesses=PASS "
         "(not full CI, RF or future-profile acceptance)");
    return 0;
}
