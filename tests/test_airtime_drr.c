#include "ninlil_airtime.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static const uint8_t payload[240] = {1};
static int enqueue(ninlil_airtime_scheduler *s, uint64_t id, uint16_t peer,
                    unsigned int cls, uint32_t airtime)
{
    return ninlil_airtime_enqueue(s,id,peer,(ninlil_traffic_class)cls,
                                  airtime,payload,sizeof(payload));
}
static int class_fairness(void)
{
    ninlil_airtime_scheduler s;
    const ninlil_airtime_job *j = NULL;
    uint64_t id=4, service[4]={0}, frames[4]={0}, now=0;
    const uint32_t costs[4]={10000,10000,10000,400000};
    const unsigned int weight[4]={8,4,3,1};
    CHECK(ninlil_airtime_open(&s,0,800000,0)==0);
    CHECK(ninlil_airtime_enable_drr(&s,10000,0)==0);
    for(unsigned int c=0;c<4;c++) CHECK(enqueue(&s,c+1,(uint16_t)(c+2),c,costs[c])==0);
    for(unsigned int i=0;i<12020;i++) {
        unsigned int c;
        now+=1000000;
        CHECK(ninlil_airtime_next(&s,now,&j)==0);
        c=(unsigned int)j->traffic; service[c]+=j->airtime_us; frames[c]++;
        CHECK(ninlil_airtime_complete(&s,0)==0);
        CHECK(enqueue(&s,++id,(uint16_t)(c+2),c,costs[c])==0);
    }
    for(unsigned int c=0;c<4;c++) {
        uint64_t normalized=service[c]/weight[c];
        uint64_t delta=normalized>service[3]?normalized-service[3]:service[3]-normalized;
        CHECK(delta<=400000);
    }
    printf("DRR airtime=%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64" frames=%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n",
           service[0],service[1],service[2],service[3],frames[0],frames[1],frames[2],frames[3]);
    return 0;
}
static int reservation(void)
{
    ninlil_airtime_scheduler s, before;
    const ninlil_airtime_job *j=NULL;
    uint64_t id=2;
    unsigned int bulk=0, urgent=0;
    CHECK(ninlil_airtime_open(&s,0,400000,0)==0);
    before=s;
    CHECK(ninlil_airtime_enable_drr(&s,999,0)==NINLIL_ERR_INVALID);
    CHECK(!memcmp(&s,&before,sizeof(s)));
    CHECK(ninlil_airtime_enable_drr(&s,10000,50000)==0);
    CHECK(enqueue(&s,1,2,3,400000)==0);
    for(unsigned int i=0;i<3;i++) CHECK(ninlil_airtime_next(&s,0,&j)==NINLIL_ERR_EMPTY);
    CHECK(s.waiting);
    CHECK(enqueue(&s,id,3,0,10000)==0);
    CHECK(ninlil_airtime_next(&s,110000,&j)==0 && j->token==id);
    CHECK(s.waiting && s.bypass_left_us==40000);
    CHECK(ninlil_airtime_complete(&s,NINLIL_ERR_IO)==0);
    CHECK(s.jobs[s.active].used);
    for(uint64_t t=120000;t<=1200000;t+=10000) {
        int rc=ninlil_airtime_next(&s,t,&j);
        CHECK(rc==0 || rc==NINLIL_ERR_BUSY || rc==NINLIL_ERR_EMPTY);
        if(rc==0) {
            unsigned int c=(unsigned int)j->traffic;
            CHECK(ninlil_airtime_complete(&s,0)==0);
            if(c==3) {bulk++; break;}
            urgent++;
            CHECK(enqueue(&s,++id,3,0,10000)==0);
        }
    }
    CHECK(bulk==1 && urgent<=4);
    CHECK(ninlil_airtime_enable_drr(&s,10000,0)==NINLIL_ERR_STATE);
    return 0;
}
static int peer_fairness(void)
{
    ninlil_airtime_scheduler s;
    const ninlil_airtime_job *j=NULL;
    uint64_t id=2, a=0,b=0,now=0;
    CHECK(ninlil_airtime_open(&s,0,800000,0)==0);
    CHECK(ninlil_airtime_enable_drr(&s,10000,0)==0);
    CHECK(enqueue(&s,1,2,2,400000)==0);
    CHECK(enqueue(&s,2,3,2,10000)==0);
    for(unsigned int i=0;i<410;i++) {
        uint16_t peer; uint32_t cost;
        int rc;
        do {now+=1000000; rc=ninlil_airtime_next(&s,now,&j);} while(rc==NINLIL_ERR_EMPTY);
        CHECK(rc==0);
        peer=j->peer; cost=j->airtime_us;
        if(peer==2) a+=cost; else b+=cost;
        CHECK(ninlil_airtime_complete(&s,0)==0);
        CHECK(enqueue(&s,++id,peer,2,cost)==0);
    }
    CHECK(a>0 && b>0 && (a>b?a-b:b-a)<=400000);
    printf("peer airtime=%"PRIu64",%"PRIu64"\n",a,b);
    return 0;
}
static int bounds(void)
{
    ninlil_airtime_scheduler s,before;
    const ninlil_airtime_job *j=NULL;
    CHECK(ninlil_airtime_open(&s,0,800000,0)==0);
    CHECK(ninlil_airtime_enable_drr(&s,1000,0)==0);
    CHECK(enqueue(&s,1,2,2,400000)==0);
    before=s;
    CHECK(enqueue(&s,1,2,2,400000)==0 && !memcmp(&s,&before,sizeof(s)));
    CHECK(enqueue(&s,1,2,2,10)==NINLIL_ERR_CONFLICT);
    CHECK(enqueue(&s,2,2,99,10)==NINLIL_ERR_INVALID);
    s.next_sequence=UINT64_MAX;
    before=s;
    CHECK(enqueue(&s,2,3,2,10)==NINLIL_ERR_CAPACITY);
    CHECK(!memcmp(&s,&before,sizeof(s)));
    for(unsigned int i=0;i<10;i++) {
        int rc=ninlil_airtime_next(&s,1000000,&j);
        CHECK(rc==0 || rc==NINLIL_ERR_EMPTY);
        if(rc==0) {CHECK(ninlil_airtime_discard_stale(&s)==0); break;}
    }
    CHECK(!s.jobs[31].used);
    return 0;
}
int main(void)
{
    CHECK(class_fairness()==0); CHECK(reservation()==0);
    CHECK(peer_fairness()==0); CHECK(bounds()==0);
    puts("DRR regression PASS"); return 0;
}
