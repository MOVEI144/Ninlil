#include "ninlil_airtime.h"
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
    ninlil_airtime_scheduler s, before;
    const ninlil_airtime_job *job;
    uint8_t frame[240] = {1u};
    CHECK(ninlil_airtime_open(&s, 0u, 800000u, 0u) == 0);
    CHECK(ninlil_airtime_enqueue_at(&s, 1u, 2u, NINLIL_TRAFFIC_NORMAL, 10000u,
                                    frame, sizeof(frame), 0u) == 0);
    before = s;
    CHECK(ninlil_airtime_enqueue_at(&s, 1u, 2u, NINLIL_TRAFFIC_NORMAL, 10000u,
                                    frame, sizeof(frame), 100000u) == 0);
    CHECK(!memcmp(&s, &before, sizeof(s)));
    CHECK(ninlil_airtime_next(&s, 500000u, &job) == 0);
    CHECK(job->queued_time_known && job->queued_at_us == 0u);
    CHECK(ninlil_airtime_complete(&s, NINLIL_ERR_BUSY) == 0);
    CHECK(ninlil_airtime_next(&s, 600000u, &job) == 0);
    CHECK(job->queued_at_us == 0u && job->queued_time_known);
    CHECK(ninlil_airtime_discard_stale(&s) == 0);
    CHECK(ninlil_airtime_enqueue(&s, 2u, 2u, NINLIL_TRAFFIC_NORMAL, 10000u,
                                 frame, sizeof(frame)) == 0);
    before = s;
    CHECK(ninlil_airtime_enqueue_at(&s, 2u, 2u, NINLIL_TRAFFIC_NORMAL, 10000u,
                                    frame, sizeof(frame), 600000u) == 0);
    CHECK(!memcmp(&s, &before, sizeof(s))); /* Unknown origin stays unknown. */
    CHECK(ninlil_airtime_enqueue_at(&s, 3u, 2u, NINLIL_TRAFFIC_NORMAL, 10000u,
                                    frame, sizeof(frame),
                                    0u) == NINLIL_ERR_INVALID);
    CHECK(!memcmp(&s, &before, sizeof(s)));
    CHECK(ninlil_airtime_next(&s, 700000u, &job) == 0 &&
          !job->queued_time_known);
    puts("queue first-admission timestamp / BUSY / coalescing / unknown origin "
         "PASS");
    return 0;
}
