#include "ninlil_airtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check %d: %s\n", __LINE__, #x);                   \
            return 1;                                                          \
        }                                                                      \
    } while (0)
int main(void)
{
    ninlil_airtime_scheduler s;
    const ninlil_airtime_job *job;
    uint8_t bytes[240] = {1};
    unsigned int i, counts[4] = {0};
    uint64_t now = 1000000u;
    CHECK(ninlil_airtime_open(&s, 0u, 800000u, 50000u) == NINLIL_OK);
    for (i = 0u; i < 4u; i++)
        CHECK(ninlil_airtime_enqueue(&s, i + 1u, 2u, NINLIL_TRAFFIC_NORMAL,
                                     100000u, bytes,
                                     sizeof(bytes)) == NINLIL_OK);
    CHECK(ninlil_airtime_enqueue(&s, 99u, 2u, NINLIL_TRAFFIC_NORMAL, 100000u,
                                 bytes, sizeof(bytes)) == NINLIL_ERR_CAPACITY);
    CHECK(ninlil_airtime_enqueue(&s, 10u, 2u, NINLIL_TRAFFIC_CRITICAL, 100000u,
                                 bytes, sizeof(bytes)) == NINLIL_OK);
    CHECK(ninlil_airtime_enqueue(&s, 11u, 2u, NINLIL_TRAFFIC_CONTROL, 100000u,
                                 bytes, sizeof(bytes)) == NINLIL_OK);
    CHECK(ninlil_airtime_enqueue(&s, 12u, 3u, NINLIL_TRAFFIC_BULK, 100000u,
                                 bytes, sizeof(bytes)) == NINLIL_OK);
    CHECK(ninlil_airtime_next(&s, 0u, &job) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_airtime_next(&s, now, &job) == NINLIL_OK &&
          job->traffic == NINLIL_TRAFFIC_CRITICAL);
    CHECK(ninlil_airtime_next(&s, now, &job) == NINLIL_ERR_BUSY);
    CHECK(ninlil_airtime_complete(&s, NINLIL_ERR_TIMEOUT) == NINLIL_OK);
    for (i = 0u; i < 64u; i++) {
        int rc;
        now += 200000u;
        rc = ninlil_airtime_next(&s, now, &job);
        if (rc == NINLIL_ERR_EMPTY)
            break;
        CHECK(rc == NINLIL_OK);
        counts[(unsigned int)job->traffic]++;
        CHECK(ninlil_airtime_complete(&s, NINLIL_OK) == NINLIL_OK);
    }
    CHECK(counts[0] == 1u && counts[1] == 1u && counts[2] == 4u &&
          counts[3] == 1u);
    CHECK(ninlil_airtime_next(&s, 1u, &job) == NINLIL_ERR_STATE);
    CHECK(ninlil_airtime_open(&s, 0u, 1u, 0u) == NINLIL_OK);
    CHECK(ninlil_airtime_enqueue(&s, 1u, 2u, NINLIL_TRAFFIC_NORMAL, 1u, bytes,
                                 sizeof(bytes)) == NINLIL_OK);
    for (i = 1u; i < 1000u; i++)
        CHECK(ninlil_airtime_next(&s, (uint64_t)i * 1000u, &job) ==
              NINLIL_ERR_EMPTY);
    CHECK(ninlil_airtime_next(&s, 1000000u, &job) == NINLIL_OK);
    CHECK(ninlil_airtime_complete(&s, NINLIL_OK) == NINLIL_OK);
    puts("one-radio airtime budget/reserves/fairness/ambiguous TX retention "
         "PASS");
    return 0;
}
