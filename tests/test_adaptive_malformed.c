#include "ninlil_link_metrics.h"
#include "ninlil_phy_plan.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                    \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static uint32_t random32(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}
int main(void)
{
    uint32_t seed = 20260909u;
    uint8_t frame[256], base[184], roundtrip[184];
    ninlil_phy_fragment output, before, good = {0};
    ninlil_link_metrics metrics;
    ninlil_link_context context = {1u, 1u, -3, {1u}};
    size_t base_size = 0u;
    unsigned int accepted = 0u, rejected = 0u;
    good.operation[0] = 1u;
    good.digest[0] = 1u;
    good.authority_epoch = good.plan_epoch = 1u;
    good.profile = good.profile_version = 1u;
    good.activate_ms = 1000u;
    good.expires_ms = 60000u;
    good.count = 1u;
    good.length = 88u;
    CHECK(ninlil_phy_fragment_encode(&good, base, sizeof(base), &base_size) ==
              0 &&
          base_size == 184u);
    for (unsigned int iteration = 0u; iteration < 20000u; iteration++) {
        size_t size;
        int rc;
        if (iteration % 2u) {
            size = base_size;
            memcpy(frame, base, base_size);
            frame[random32(&seed) % base_size] ^=
                (uint8_t)(1u + random32(&seed) % 255u);
        } else {
            size = random32(&seed) % 257u;
            for (size_t i = 0u; i < sizeof(frame); i++)
                frame[i] = (uint8_t)(random32(&seed) >> 24);
        }
        memset(&output, 0xa5, sizeof(output));
        before = output;
        rc = ninlil_phy_fragment_decode(frame, size, &output);
        if (rc == NINLIL_OK) {
            size_t encoded = 0u;
            accepted++;
            CHECK(ninlil_phy_fragment_encode(&output, roundtrip,
                                             sizeof(roundtrip), &encoded) == 0);
            CHECK(encoded == size && !memcmp(roundtrip, frame, size));
        } else {
            rejected++;
            CHECK(!memcmp(&output, &before, sizeof(output)));
        }
    }
    CHECK(accepted > 1000u && rejected > 1000u);
    ninlil_link_metrics_open(&metrics);
    CHECK(ninlil_link_metrics_tx(&metrics, &context, 1u, 0u, 1000u, 0u) == 0);
    CHECK(ninlil_link_metrics_invalidate(&metrics, 1u) == 0);
    CHECK(metrics.sequence == 1u && !metrics.pending &&
          !metrics.consecutive_losses);
    CHECK(ninlil_link_metrics_tick(&metrics, 10000u) == 0 && !metrics.attempts);
    printf("seed=20260909 mutated decoder accepted=%u rejected=%u / sleep "
           "invalidation PASS\n",
           accepted, rejected);
    return 0;
}
