#include "ninlil_radio_adapt.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
int main(void)
{
    ninlil_radio_adapt s;
    int8_t p;
    CHECK(ninlil_radio_adapt_open(&s, -10, -3) == NINLIL_ERR_INVALID);
    CHECK(ninlil_radio_adapt_open(&s, -9, -2) == NINLIL_ERR_INVALID);
    CHECK(ninlil_radio_adapt_open(&s, -9, -3) == NINLIL_OK);
    CHECK(ninlil_radio_adapt_observe(&s, 10000u, 1000u, 8u, 8u, &p) ==
              NINLIL_OK &&
          p == -3);
    for (unsigned int i = 0u; i < 50u; i++)
        CHECK(ninlil_radio_adapt_observe(&s, 10000u, 1000u, 8u, 8u, &p) ==
                  NINLIL_OK &&
              p == -3);
    CHECK(ninlil_radio_adapt_observe(&s, 20000u, 17000u, 8u, 8u, &p) ==
              NINLIL_OK &&
          p == -3);
    CHECK(ninlil_radio_adapt_observe(&s, 30000u, 27000u, 8u, 8u, &p) ==
              NINLIL_OK &&
          p == -6);
    for (uint64_t t = 40000u; t <= 60000u; t += 10000u)
        CHECK(ninlil_radio_adapt_observe(&s, t, t - 3000u, 8u, 8u, &p) ==
              NINLIL_OK);
    CHECK(p == -9);
    CHECK(ninlil_radio_adapt_observe(&s, 61000u, 58000u, 8u, 6u, &p) ==
              NINLIL_OK &&
          p == -3);
    CHECK(ninlil_radio_adapt_observe(&s, 62000u, 58000u, 8u, 8u, &p) ==
              NINLIL_OK &&
          p == -3);
    CHECK(ninlil_radio_adapt_observe(&s, 62000u, 63000u, 8u, 8u, &p) ==
          NINLIL_ERR_INVALID);
    CHECK(ninlil_radio_adapt_observe(&s, 61000u, 58000u, 8u, 8u, &p) ==
          NINLIL_ERR_INVALID);
    for (uint64_t t = 72000u; t <= 112000u; t += 10000u)
        CHECK(ninlil_radio_adapt_observe(&s, t, t - 3000u, 8u, 8u, &p) ==
              NINLIL_OK);
    CHECK(p == -6);
    CHECK(ninlil_radio_adapt_observe(&s, 180000u, 109000u, 8u, 8u, &p) ==
              NINLIL_OK &&
          p == -3);
    puts("power bounds, duplicate/stale observations, hysteresis, recovery and "
         "clock regression PASS");
    return 0;
}
