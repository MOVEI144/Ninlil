#include "ninlil_airtime.h"
#include <stdio.h>
int main(void)
{
    ninlil_airtime_scheduler scheduler;
    if (ninlil_airtime_open(&scheduler,0u,800000u,0u)!=NINLIL_OK ||
        !scheduler.drr_enabled || scheduler.quantum_us!=10000u ||
        scheduler.bypass_limit_us!=50000u)
        return 1;
    puts("explicit firmware DRR build selection PASS (native compiler, not ESP-IDF)");
    return 0;
}
