/* Native port/observer contract test. Real changed node radio/link/sleep and
 * pump sources; the rest of node/Core, crypto, clocks and physical radio are
 * explicit doubles. This is not EDHOC/RF/persistence or full-runtime evidence. */
#include "ninlil_node_internal.h"
#include "ninlil_feedback_pump.h"
#include "ninlil_sleep.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static int64_t clock_us;
static int send_result, stage_result, wrong_applied, reject_current, bad_auth;
static unsigned int physical_sends;
static uint8_t wire[240];
static uint16_t wire_size;
static ninlil_node node;
static ninlil_esp_network_pump pump;
static ninlil_sx1262_radio radio;

int64_t esp_timer_get_time(void) { return clock_us; }
uint32_t esp_random(void) { return 0u; }
uint64_t ninlil_node_get(const uint8_t *p,size_t n) { uint64_t v=0;for(size_t i=0;i<n;i++)v=(v<<8)|p[i];return v; }
void ninlil_node_put(uint8_t *p,uint64_t v,size_t n) { while(n){p[--n]=(uint8_t)v;v>>=8;} }
int ninlil_node_index(const ninlil_node *n,uint16_t peer) { for(unsigned int i=0;i<n->config.member_count;i++)if(n->members[i].grant.node==peer)return (int)i;return -1; }
int ninlil_node_policy(void *ctx,uint16_t peer,ninlil_peer_policy *p) { (void)ctx;(void)peer;memset(p,0,sizeof(*p));p->membership_epoch=p->session_membership_epoch=1u;return 0; }
int ninlil_node_control_send(ninlil_node *n,uint16_t p,node_control_kind k,const uint8_t*d,size_t l){(void)n;(void)p;(void)k;(void)d;(void)l;return 0;}
int ninlil_node_lease(ninlil_node *n,uint64_t *t){*t=n->now_ms+100000u;return 0;}
int ninlil_node_recovery_step(ninlil_node*n){(void)n;return 0;}
int ninlil_node_recovery_receive(ninlil_node*n,uint16_t p,node_control_kind k,const uint8_t*d,size_t l){(void)n;(void)p;(void)k;(void)d;(void)l;return NINLIL_ERR_EMPTY;}
int ninlil_coordinator_observe(ninlil_coordinator*c,uint16_t p,const ninlil_network_edge*e,uint64_t t){(void)c;(void)p;(void)e;(void)t;return 0;}
int ninlil_ingest(ninlil_runtime*r,const uint8_t*d,size_t l){(void)r;(void)d;(void)l;return 0;}
int ninlil_step(ninlil_runtime*r){(void)r;return 0;}
int ninlil_health(const ninlil_runtime*r){(void)r;return 0;}
int ninlil_verify_retained(ninlil_runtime*r){(void)r;return 0;}
void ninlil_edhoc_close(ninlil_edhoc*h){(void)h;}
void ninlil_lease_invalidate(ninlil_lease_clock*c){(void)c;}
void ninlil_secret_clear(void*d,size_t n){memset(d,0,n);}
int ninlil_secure_inspect_tx(const ninlil_secure_session*s,const uint8_t*f,size_t n,uint8_t*out,size_t cap,size_t*written)
{
    if(reject_current || !s->ready || n<40u || n-40u>cap || memcmp(f+8,s->material.fingerprint,16u) || f[n-1]!=0xa5u)
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(out,f+32,n-40u);*written=n-40u;return 0;
}
int ninlil_secure_seal_neighbor(ninlil_secure_session*s,const uint8_t*d,size_t n,uint8_t*out,size_t cap,size_t*written)
{ (void)s;(void)d;(void)n;(void)out;(void)cap;(void)written;return NINLIL_ERR_BUSY; }
int ninlil_node_collection_step(ninlil_node*n){(void)n;return 0;}
int ninlil_node_discovery_step(ninlil_node*n){(void)n;return 0;}
int ninlil_node_auth_step(ninlil_node*n){(void)n;return 0;}
int ninlil_node_control_step(ninlil_node*n){(void)n;return 0;}
int ninlil_node_routes_step(ninlil_node*n){(void)n;return 0;}
int ninlil_node_root_clock(ninlil_node*n,uint64_t t){(void)n;(void)t;return 0;}
int ninlil_node_result(ninlil_node*n,int rc){if(rc==NINLIL_ERR_IO || rc==NINLIL_ERR_CORRUPT || rc==NINLIL_ERR_FAULT)n->status.fault=rc;return n->status.fault;}
int ninlil_node_bootstrap(ninlil_node*n,const uint8_t*f,size_t l){(void)n;(void)f;(void)l;return 0;}
int ninlil_node_auth_current(ninlil_node*n,const uint8_t*f,size_t l){(void)n;(void)f;(void)l;return 0;}
int ninlil_node_control_receive(ninlil_node*n,uint16_t peer,const uint8_t*p,size_t l,int neighbor)
{if(!neighbor || l!=9u || p[0]!=NODE_PROBE_REPLY)return NINLIL_ERR_UNAUTHORIZED;return ninlil_node_link_receive(n,peer,NODE_PROBE_REPLY,p+1,l-1u);}
void ninlil_join_expire(ninlil_join_authority*a,uint64_t t){(void)a;(void)t;}
int ninlil_set_retry_interval(ninlil_runtime*r,uint32_t t){(void)r;(void)t;return 0;}
int ninlil_secure_unseal_neighbor(ninlil_secure_session*s,const uint8_t*f,size_t n,uint8_t*out,size_t cap,size_t*written)
{if(bad_auth || !s->ready || n!=49u || cap<9u)return NINLIL_ERR_UNAUTHORIZED;memcpy(out,f+32,9u);*written=9u;return 0;}
int ninlil_routed_receive(ninlil_routed*r,const uint8_t*f,size_t n,uint64_t t){(void)r;(void)f;(void)n;(void)t;return 0;}
int ninlil_routed_poll(ninlil_routed*r,uint64_t t){r->now_ms=t;return 0;}
int ninlil_routed_frame_current(ninlil_routed*r,const uint8_t*f,size_t n){(void)r;(void)f;(void)n;return 0;}
int ninlil_sx1262_radio_airtime(const ninlil_sx1262_radio*r,uint16_t l,uint32_t*out){(void)r;(void)l;*out=100000u;return 0;}
int ninlil_sx1262_radio_power(ninlil_sx1262_radio*r,int8_t power)
{ if(stage_result)return stage_result;r->requested_power_dbm=power;return 0; }
int ninlil_sx1262_radio_send(ninlil_sx1262_radio*r,const uint8_t*d,uint16_t l)
{
    (void)d;(void)l;
    if(send_result)return send_result;
    physical_sends++;clock_us+=100000;
    r->applied_power_dbm=wrong_applied?-9:r->requested_power_dbm;return 0;
}
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio*r,uint8_t*out,uint16_t cap,uint16_t*len,ninlil_sx1262_rx_info*info,TickType_t wait)
{ (void)r;(void)wait;if(!wire_size)return NINLIL_ERR_EMPTY;if(cap<wire_size)return NINLIL_ERR_CAPACITY;memcpy(out,wire,wire_size);*len=wire_size;wire_size=0;info->rssi_dbm=-50;info->snr_db=5;return 0; }

static void frame(uint8_t *out,uint64_t token)
{
    memset(out,0,240u);memcpy(out,"NS\001",3u);out[5]=1u;out[7]=2u;
    out[8]=1u;out[31]=2u;out[32]=NODE_PROBE;ninlil_node_put(out+33,token,8u);out[239]=0xa5u;
    node.peers[1].probe_at=UINT64_MAX;node.peers[1].probe_token=token;node.peers[1].probe_sent=0u;
}
static int initialize(void)
{
    memset(&node,0,sizeof(node));memset(&radio,0,sizeof(radio));
    clock_us=0;physical_sends=0;wire_size=0;
    send_result=stage_result=wrong_applied=reject_current=bad_auth=0;
    node.config.local=1;node.config.root=1;node.config.member_count=2;node.config.permitted_profile=1;
    node.members[0].grant.node=1;node.members[1].grant.node=2;
    node.local_index=0;node.root_index=0;node.joined=1;
    for(unsigned int i=0;i<2;i++){
        node.peers[i].member_active=1;node.peers[i].sessions[1].ready=1;
        node.peers[i].sessions[1].material.fingerprint[0]=1;
    }
    radio.profile.tx_power_dbm=-3;radio.applied_power_dbm=-3;
    CHECK(ninlil_esp_node_open(&pump,&radio,&node,800000)==0);
    #ifdef NINLIL_FEEDBACK_DEFAULT
    CHECK(ninlil_esp_node_adaptive_power(&pump,-9)==0 && pump.adaptive_power==2);
#else
    CHECK(ninlil_esp_node_feedback_open(&pump,-9)==0 && pump.adaptive_power==2);
#endif
    return 0;
}
static int send_probe(uint64_t id,int reply)
{
    uint8_t bytes[240];
    frame(bytes,id);
    clock_us=(int64_t)id*4000000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_NORMAL,bytes,sizeof(bytes))==0);
    uint64_t recorded=0;
    for(unsigned int i=0;i<32;i++)if(pump.scheduler.jobs[i].used)recorded=pump.queued_at_us[i];
    clock_us+=100000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_NORMAL,bytes,sizeof(bytes))==0);
    for(unsigned int i=0;i<32;i++)if(pump.scheduler.jobs[i].used)CHECK(pump.queued_at_us[i]==recorded);
    CHECK(ninlil_esp_network_step(&pump)==0);
    CHECK(node.peers[1].probe_sent);
    if(reply){
        memset(wire,0,sizeof(wire));memcpy(wire,"NS\001",3u);wire[5]=2u;wire[7]=1u;wire[31]=2u;wire[32]=NODE_PROBE_REPLY;
        ninlil_node_put(wire+33,id,8u);wire_size=49;clock_us+=100000;
        CHECK(ninlil_esp_network_step(&pump)==0);
        CHECK(pump.feedback_reply_result==0);
    }
    return 0;
}
int main(void)
{
    uint8_t bytes[240];
    ninlil_link_window w;
    CHECK(initialize()==0);
    for(uint64_t i=1;i<=24;i++)CHECK(send_probe(i,1)==0);
    CHECK(radio.applied_power_dbm==-3);
    CHECK(send_probe(25,1)==0 && radio.applied_power_dbm==-6);
    CHECK(ninlil_radio_feedback_read(&pump.feedback,2,101000,&w)==NINLIL_ERR_EMPTY);
    /* Bootstrap traffic retains full reach even when its end target has -6dBm. */
    memset(bytes,0,sizeof(bytes));memcpy(bytes,"NB\001",3u);bytes[5]=1;bytes[7]=2;
    clock_us=104000000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_CONTROL,bytes,100)==0);
    CHECK(ninlil_esp_network_step(&pump)==0 && radio.applied_power_dbm==-3);
    /* Actual node suspend cancels its probe's provisional failure. */
    node.config.root=2;node.root_index=1;node.members[0].grant.role=NINLIL_ROLE_BATTERY_LEAF;
    CHECK(ninlil_node_suspend(&node,105000)==0);
    CHECK(ninlil_node_resume(&node,200000)==0);
    CHECK(ninlil_radio_feedback_read(&pump.feedback,2,200000,&w)==NINLIL_ERR_EMPTY);
    CHECK(initialize()==0);
    frame(bytes,1);clock_us=4000000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_NORMAL,bytes,240)==0);
    send_result=NINLIL_ERR_BUSY;
    CHECK(ninlil_esp_network_step(&pump)==NINLIL_ERR_BUSY);
    CHECK(!pump.feedback.pending && !physical_sends && !node.peers[1].probe_sent);
    send_result=0;clock_us+=1000000;
    CHECK(ninlil_esp_network_step(&pump)==0 && physical_sends==1);
    /* Simulated security rejection never reaches the observer. */
    bad_auth=1;memcpy(wire,"NS\001",3u);wire[5]=2u;wire[7]=1u;wire[31]=2u;wire[32]=NODE_PROBE_REPLY;wire_size=49;ninlil_node_put(wire+33,1,8u);clock_us+=100000;
    CHECK(ninlil_esp_network_step(&pump)==0);
    CHECK(!pump.feedback.peers[pump.feedback.slot].metrics.replied);
    /* Even a valid token delivered through a non-neighbor control path cannot
     * feed the new RF observer. Only node_io's NS channel-2 ingress calls it. */
    bad_auth=0;
    CHECK(ninlil_node_link_receive(&node,2u,NODE_PROBE_REPLY,wire+33,8u)==0);
    CHECK(!pump.feedback.peers[pump.feedback.slot].metrics.replied);
    CHECK(initialize()==0);
    frame(bytes,1);clock_us=4000000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_NORMAL,bytes,240)==0);
    reject_current=1;
    CHECK(ninlil_esp_network_step(&pump)==NINLIL_ERR_STATE && !physical_sends);
    CHECK(!pump.feedback.pending);
    CHECK(initialize()==0);
    frame(bytes,1);clock_us=4000000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_NORMAL,bytes,240)==0);
    stage_result=NINLIL_ERR_IO;
    CHECK(ninlil_esp_network_step(&pump)==NINLIL_ERR_IO);
    CHECK(!pump.scheduler.busy && !pump.feedback.pending && !physical_sends);
    CHECK(pump.feedback.fault==NINLIL_ERR_IO);
    CHECK(initialize()==0);
    frame(bytes,1);clock_us=4000000;
    CHECK(ninlil_esp_network_emit(&pump,2,NINLIL_TRAFFIC_NORMAL,bytes,240)==0);
    wrong_applied=1;
    CHECK(ninlil_esp_network_step(&pump)==NINLIL_ERR_IO);
    CHECK(pump.feedback.fault==NINLIL_ERR_IO);
    CHECK(ninlil_esp_network_step(&pump)==NINLIL_ERR_IO && physical_sends==1);
    printf("actual node hooks + actual pump contract PASS; feedback state=%zu bytes\n",sizeof(ninlil_radio_feedback));
    return 0;
}
