#include "ninlil_airtime.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static int run(int drr)
{
    ninlil_airtime_scheduler s, before;
    const ninlil_airtime_job *job;
    const uint8_t frame[8] = {1};
    uint32_t remaining;
    CHECK(ninlil_airtime_open(&s, 0u, 400000u, 50000u) == 0);
    if (drr)
        CHECK(ninlil_airtime_enable_drr(&s, 10000u, 50000u) == 0);
    CHECK(ninlil_airtime_enqueue(&s, 1u, 2u, NINLIL_TRAFFIC_CONTROL, 200000u,
                                 frame, sizeof(frame)) == 0);
    CHECK(ninlil_airtime_next(&s, 1000000u, &job) == 0);
    CHECK(s.credit_us == 200000u);
    before = s;
    CHECK(ninlil_airtime_not_sent(&s, 999999u) == NINLIL_ERR_STATE);
    CHECK(!memcmp(&s, &before, sizeof(s)));
    CHECK(ninlil_airtime_not_sent(&s, 1001000u) == 0);
    CHECK(s.credit_us == 400000u && !s.busy && s.jobs[s.active].used);
    CHECK(ninlil_airtime_not_sent(&s, 1001000u) == NINLIL_ERR_STATE);
    CHECK(ninlil_airtime_next(&s, 1000000u, &job) == NINLIL_ERR_BUSY);
    CHECK(ninlil_airtime_next(&s, 1001000u, &job) == 0);
    remaining = s.credit_us;
    CHECK(ninlil_airtime_complete_at(&s, NINLIL_ERR_TIMEOUT, 1301000u) == 0);
    CHECK(s.credit_us == remaining && s.jobs[s.active].used);
    CHECK(s.not_before_us == 1351000u);
    CHECK(ninlil_airtime_next(&s, 1350999u, &job) == NINLIL_ERR_BUSY);
    CHECK(ninlil_airtime_next(&s, 1351000u, &job) == 0);
    remaining = s.credit_us;
    CHECK(ninlil_airtime_discard_stale(&s) == 0);
    CHECK(s.credit_us == remaining + 200000u && !s.jobs[s.active].used);
    /* Only the last proven-unsent reservation was refunded. */
    CHECK(s.credit_us < 400000u);
    return 0;
}
static int bypass(void)
{
    ninlil_airtime_scheduler s;
    const ninlil_airtime_job *job;
    const uint8_t frame = 1u;
    CHECK(ninlil_airtime_open(&s, 0u, 400000u, 0u) == 0);
    CHECK(ninlil_airtime_enable_drr(&s, 10000u, 50000u) == 0);
    CHECK(ninlil_airtime_enqueue(&s, 1u, 2u, NINLIL_TRAFFIC_BULK, 400000u,
                                 &frame, 1u) == 0);
    for (unsigned int i = 0u; i < 3u; i++)
        CHECK(ninlil_airtime_next(&s, 0u, &job) == NINLIL_ERR_EMPTY);
    CHECK(s.waiting);
    CHECK(ninlil_airtime_enqueue(&s, 2u, 3u, NINLIL_TRAFFIC_CRITICAL, 10000u,
                                 &frame, 1u) == 0);
    CHECK(ninlil_airtime_next(&s, 100000u, &job) == 0 && job->token == 2u);
    CHECK(s.bypass_left_us == 40000u && s.waiting);
    CHECK(ninlil_airtime_not_sent(&s, 100000u) == 0);
    CHECK(s.bypass_left_us == 50000u && s.waiting && s.credit_us == 40000u);
    CHECK(ninlil_airtime_next(&s, 100000u, &job) == 0 && job->token == 2u);
    CHECK(ninlil_airtime_complete_at(&s, 0, 110000u) == 0);
    CHECK(s.bypass_left_us == 40000u);
    return 0;
}
int main(void)
{
    CHECK(run(0) == 0 && run(1) == 0 && bypass() == 0);
    puts("proven-unsent refund / ambiguous charge / DRR / actual completion "
         "PASS");
    return 0;
}
