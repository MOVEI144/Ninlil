#include "ninlil_phy_plan.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while(0)
/* Callback contract fixture, not a cryptographic implementation or claim. */
static int verify_fixture(void *ctx,const uint8_t *data,size_t length,const uint8_t digest[32])
{
    unsigned int *calls=ctx;(*calls)++;
    if(length!=1408 || digest[0]!=1) return NINLIL_ERR_CORRUPT;
    for(size_t i=0;i<length;i++) if(data[i]!=(uint8_t)(i/88)) return NINLIL_ERR_CORRUPT;
    return 0;
}
int main(void)
{
    ninlil_phy_profile profiles[2]={{.id=1,.version=1,.max_airtime_us=400000,.post_tx_pause_us=50000,.retune_us=1000,.mtu=240,.approved=1},
      {.id=2,.version=1,.max_airtime_us=400000,.post_tx_pause_us=50000,.retune_us=1000,.mtu=240,.approved=1}};
    ninlil_phy_slot slots[3]={{.start_us=0,.duration_us=460000,.guard_us=5000,.profile=1,.recovery=1},
      {.start_us=460000,.duration_us=170000,.airtime_us=100000,.guard_us=5000,.commit_us=10000,.profile=2,.tx=1,.rx=2},
      {.start_us=630000,.duration_us=170000,.airtime_us=100000,.guard_us=5000,.commit_us=10000,.profile=2,.tx=2,.rx=3}};
    ninlil_phy_fragment f={.operation={1},.digest={1},.authority_epoch=1,.plan_epoch=2,.activate_ms=200,.expires_ms=100000,.profile=1,.profile_version=1,.count=16,.length=88},decoded,before;
    ninlil_phy_reassembly s;
    uint8_t frame[184];size_t n=0;unsigned int calls=0;
    CHECK(ninlil_phy_schedule_validate(profiles,2,slots,3,1000000,1000,1)==0);
    slots[0].conflict_domain=1;slots[1].conflict_domain=2;
    slots[1].start_us=10000;
    CHECK(ninlil_phy_schedule_validate(profiles,2,slots,3,1000000,1000,1)==NINLIL_ERR_CONFLICT);
    slots[0].conflict_domain=slots[1].conflict_domain=0;
    slots[1].start_us=460000;
    slots[2].start_us=620000;
    CHECK(ninlil_phy_schedule_validate(profiles,2,slots,3,1000000,1000,1)==NINLIL_ERR_CONFLICT);
    slots[2].start_us=630000;
    CHECK(ninlil_phy_schedule_validate(profiles,2,slots,3,1000000,5000,1)==NINLIL_ERR_STATE);
    slots[2].duration_us--;
    CHECK(ninlil_phy_schedule_validate(profiles,2,slots,3,1000000,1000,1)==NINLIL_ERR_CAPACITY);
    ninlil_phy_reassembly_open(&s);
    for(int i=15;i>=0;i--) {
        int rc;f.index=(uint8_t)i;memset(f.payload,i,sizeof(f.payload));
        CHECK(ninlil_phy_fragment_encode(&f,frame,sizeof(frame),&n)==0 && n+40+16==240);
        CHECK(ninlil_phy_fragment_decode(frame,n,&decoded)==0 && decoded.index==i);
        before=decoded;frame[2]=1;
        CHECK(ninlil_phy_fragment_decode(frame,n,&decoded)==NINLIL_ERR_INVALID && !memcmp(&before,&decoded,sizeof(before)));
        frame[2]=0;
        rc=ninlil_phy_reassembly_push(&s,2,frame,n,100+(uint64_t)(15-i)*2u,NINLIL_TIME_RESTART_SAFE,verify_fixture,&calls);
        CHECK(rc==(i==0?0:NINLIL_ERR_BUSY));
        CHECK(s.deadline_ms==60100);
        CHECK(ninlil_phy_reassembly_push(&s,2,frame,n,101+(uint64_t)(15-i)*2u,NINLIL_TIME_RESTART_SAFE,verify_fixture,&calls)==rc);
    }
    CHECK(s.ready && s.length==1408 && calls==1);
    frame[96]^=1;
    CHECK(ninlil_phy_reassembly_push(&s,2,frame,n,132,NINLIL_TIME_RESTART_SAFE,verify_fixture,&calls)==NINLIL_ERR_CONFLICT);
    CHECK(s.poisoned && !s.ready);
    ninlil_phy_reassembly_open(&s);frame[96]^=1;
    CHECK(ninlil_phy_reassembly_push(&s,2,frame,n,100,NINLIL_TIME_RESTART_SAFE,verify_fixture,&calls)==NINLIL_ERR_BUSY);
    CHECK(ninlil_phy_reassembly_push(&s,2,frame,n,60100,NINLIL_TIME_RESTART_SAFE,verify_fixture,&calls)==NINLIL_ERR_EXPIRED);
    puts("PHY guard/half-duplex/capacity/240-byte codec/reassembly PASS");return 0;
}
