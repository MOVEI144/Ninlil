#include "ninlil_power_policy.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct driver { int calls, fail; int8_t power; } driver;
static int apply(void *ctx, int8_t power)
{
    driver *d = ctx;
    d->calls++;
    if (d->fail) return NINLIL_ERR_IO;
    d->power = power;
    return NINLIL_OK;
}
int main(void)
{
    ninlil_link_context c = {1u, 1u, -3, {1u}};
    ninlil_power_policy p, saved;
    driver d = {0,0,-3};
    ninlil_link_window w = {0};
    CHECK(ninlil_power_policy_open(&p,&c,-9,-3,0u)==NINLIL_OK);
    for (uint64_t i=0; i<3u; i++) {
        w.context=p.context; w.first_sequence=1u+i*8u; w.last_sequence=8u+i*8u;
        w.first_tx_ms=i*40000u; w.closed_ms=(i+1u)*40000u;
        w.attempts=w.delivered=8u; w.airtime_sum_us=80000u;
        CHECK(ninlil_power_policy_step(&p,&w,w.closed_ms,apply,&d)==NINLIL_OK);
        if (i<2u) {
            uint8_t good=p.good;
            CHECK(ninlil_power_policy_step(&p,&w,w.closed_ms,apply,&d)==NINLIL_OK && p.good==good);
        }
    }
    CHECK(p.context.power_dbm==-6 && p.context.generation==2u && d.calls==1);
    saved=p;
    CHECK(ninlil_power_policy_step(&p,&w,120001u,apply,&d)==NINLIL_ERR_CONFLICT);
    CHECK(!memcmp(&p,&saved,sizeof(p)));
    CHECK(ninlil_power_policy_step(&p,NULL,200000u,apply,&d)==NINLIL_OK && d.calls==1);
    w.context=p.context; w.first_sequence=25u;w.last_sequence=32u;
    w.first_tx_ms=120000u;w.closed_ms=200000u;w.delivered=6u;
    CHECK(ninlil_power_policy_step(&p,&w,200000u,apply,&d)==NINLIL_OK);
    CHECK(p.context.power_dbm==-3 && p.context.generation==3u && d.calls==2);
    p.context.power_dbm=-6;d.power=-6;d.fail=1;
    CHECK(ninlil_power_policy_step(&p,NULL,320000u,apply,&d)==NINLIL_ERR_IO && p.poisoned);
    CHECK(ninlil_power_policy_step(&p,NULL,320001u,apply,&d)==NINLIL_ERR_STATE);
    puts("generation-bound power policy/disjoint windows/duplicate/loss/driver fault PASS");
    return 0;
}
