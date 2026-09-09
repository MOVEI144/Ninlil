#include "ninlil_radio_feedback.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static const uint8_t session[16] = {1u};
static int trial(ninlil_radio_feedback *o, uint64_t token, uint64_t now,
                  int reply, int8_t expected)
{
    ninlil_link_context c;
    CHECK(ninlil_radio_feedback_begin(o,2u,1u,session,now,&c)==0);
    CHECK(c.power_dbm==expected);
    CHECK(ninlil_radio_feedback_finish(o,0,c.power_dbm,now+1u,token,100000u,40000u)==0);
    if(reply) {
        CHECK(ninlil_radio_feedback_reply(o,2u,session,token,now+101u)==0);
        CHECK(ninlil_radio_feedback_reply(o,2u,session,token,now+101u)==0);
    }
    return 0;
}
int main(void)
{
    ninlil_radio_feedback o, before;
    ninlil_link_context c, old;
    ninlil_link_window w;
    uint8_t other[16]={2u};
    CHECK(ninlil_radio_feedback_open(&o,-9,-3)==0);
    CHECK(ninlil_radio_feedback_begin(&o,2,1,session,0,&c)==0);
    CHECK(!o.peers[o.slot].confirmed && !o.peers[o.slot].policy.opened);
    CHECK(ninlil_radio_feedback_finish(&o,NINLIL_ERR_BUSY,-3,10,1,100000,0)==0);
    CHECK(!o.peers[o.slot].metrics.sequence && !o.peers[o.slot].confirmed);
    for(uint64_t i=1;i<=24;i++) CHECK(trial(&o,i,i*4000u,1,-3)==0);
    /* Window still open: data may not force a power change mid-probe. */
    CHECK(ninlil_radio_feedback_begin(&o,2,1,session,96200,&c)==0 && c.power_dbm==-3);
    CHECK(ninlil_radio_feedback_finish(&o,0,-3,96201,0,100000,0)==0);
    CHECK(ninlil_radio_feedback_read(&o,2,96201,&w)==0 && w.last_sequence==16);
    CHECK(ninlil_radio_feedback_begin(&o,2,1,session,100000,&c)==0 && c.power_dbm==-6);
    old=c;
    CHECK(o.peers[o.slot].policy.context.power_dbm==-3); /* staging is not apply */
    CHECK(ninlil_radio_feedback_finish(&o,NINLIL_ERR_BUSY,-3,100001,0,100000,0)==0);
    CHECK(ninlil_radio_feedback_begin(&o,2,1,session,101000,&c)==0 && c.generation==old.generation);
    CHECK(ninlil_radio_feedback_finish(&o,0,-6,101001,25,100000,0)==0);
    CHECK(ninlil_radio_feedback_read(&o,2,101001,&w)==NINLIL_ERR_EMPTY);
    CHECK(ninlil_radio_feedback_reply(&o,2,other,25,101002)==NINLIL_ERR_CONFLICT);
    CHECK(ninlil_radio_feedback_reply(&o,2,session,25,104001)==NINLIL_ERR_EXPIRED);
    /* Sleeping cancels the provisional failure even with retained session keys. */
    CHECK(ninlil_radio_feedback_invalidate(&o,0,104002)==0);
    CHECK(!o.peers[o.slot].metrics.attempts && !o.peers[o.slot].metrics.pending);
    CHECK(ninlil_radio_feedback_begin(&o,2,1,session,105000,&c)==0 && c.power_dbm==-3 && c.generation>old.generation);
    CHECK(ninlil_radio_feedback_finish(&o,0,-3,105001,26,100000,UINT32_MAX)==0);
    CHECK(!o.peers[o.slot].metrics.pending); /* unknown queue is not zero */
    CHECK(ninlil_radio_feedback_begin(&o,2,1,other,106000,&c)==0 && c.power_dbm==-3);
    CHECK(ninlil_radio_feedback_finish(&o,0,-3,106001,27,100000,30000000u)==0);
    CHECK(ninlil_radio_feedback_reply(&o,2,session,27,106002)==NINLIL_ERR_CONFLICT);
    CHECK(ninlil_radio_feedback_reply(&o,2,other,27,106003)==0);
    before=o;
    CHECK(ninlil_radio_feedback_begin(&o,2,1,other,0,&c)==NINLIL_ERR_STATE);
    CHECK(!memcmp(&o,&before,sizeof(o)));
    CHECK(ninlil_radio_feedback_begin(&o,2,1,other,110000,&c)==0);
    CHECK(ninlil_radio_feedback_finish(&o,0,-6,110001,28,100000,0)==NINLIL_ERR_IO);
    CHECK(o.fault==NINLIL_ERR_IO);
    CHECK(ninlil_radio_feedback_begin(&o,2,1,other,111000,&c)==NINLIL_ERR_STATE);
    CHECK(ninlil_radio_feedback_open(&o,-9,-3)==0);
    for(uint16_t peer=1;peer<=16;peer++) {
        CHECK(ninlil_radio_feedback_begin(&o,peer,1,session,(uint64_t)peer*10u,&c)==0);
        CHECK(ninlil_radio_feedback_finish(&o,0,-3,(uint64_t)peer*10u+1u,0,100000,0)==0);
    }
    CHECK(ninlil_radio_feedback_begin(&o,17,1,session,1000,&c)==NINLIL_ERR_CAPACITY);
    puts("feedback actual-confirmation/disjoint windows/sleep/rekey/queue/bounds PASS");
    return 0;
}
