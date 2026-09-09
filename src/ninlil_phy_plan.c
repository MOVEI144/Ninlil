#include "ninlil_phy_plan.h"
#include <string.h>
static const ninlil_phy_profile *profile(const ninlil_phy_profile *p,size_t count,uint32_t id)
{
    for(size_t i=0;i<count;i++) if(p[i].id==id) return &p[i];
    return NULL;
}
static int shares_radio(const ninlil_phy_slot *a,const ninlil_phy_slot *b)
{
    return (a->tx==b->tx && a->tx_radio==b->tx_radio) ||
           (a->tx==b->rx && a->tx_radio==b->rx_radio) ||
           (a->rx==b->tx && a->rx_radio==b->tx_radio) ||
           (a->rx==b->rx && a->rx_radio==b->rx_radio);
}
int ninlil_phy_schedule_validate(const ninlil_phy_profile *p,size_t profiles,
                                 const ninlil_phy_slot *s,size_t count,
                                 uint64_t period,uint32_t error,uint32_t recovery)
{
    unsigned int recovery_count=0u;
    if(!p || !s || !profiles || profiles>NINLIL_PHY_PROFILES || !count ||
       count>NINLIL_PHY_SLOTS || !period || period>60000000u || error>1000000u || !recovery)
        return NINLIL_ERR_INVALID;
    for(size_t i=0;i<profiles;i++) {
        if(!p[i].id || !p[i].version || p[i].approved!=1u || !p[i].mtu ||
           p[i].mtu>240u || !p[i].max_airtime_us || p[i].max_airtime_us>400000u ||
           p[i].post_tx_pause_us>1000000u || p[i].retune_us>1000000u)
            return NINLIL_ERR_UNAUTHORIZED;
        for(size_t j=0;j<i;j++) if(p[i].id==p[j].id) return NINLIL_ERR_CONFLICT;
    }
    for(size_t i=0;i<count;i++) {
        const ninlil_phy_profile *q=profile(p,profiles,s[i].profile);
        uint64_t minimum;
        if(!q || s[i].recovery>1u || !s[i].duration_us || s[i].start_us>=period ||
           s[i].duration_us>period-s[i].start_us || s[i].guard_us>10000000u ||
           s[i].wake_us>1000000u || s[i].commit_us>1000000u)
            return NINLIL_ERR_INVALID;
        if(s[i].guard_us < (uint64_t)error*2u+q->retune_us+s[i].wake_us)
            return NINLIL_ERR_STATE;
        if(s[i].recovery) {
            if(s[i].profile!=recovery || s[i].tx || s[i].rx || s[i].airtime_us)
                return NINLIL_ERR_INVALID;
            recovery_count++;
        } else if(!s[i].tx || !s[i].rx || s[i].tx==UINT16_MAX || s[i].rx==UINT16_MAX ||
                  s[i].tx==s[i].rx || !s[i].airtime_us || s[i].airtime_us>q->max_airtime_us)
            return NINLIL_ERR_INVALID;
        minimum=(s[i].recovery?q->max_airtime_us:s[i].airtime_us)+
                (uint64_t)s[i].guard_us*2u+s[i].commit_us+q->post_tx_pause_us;
        if(s[i].duration_us<minimum) return NINLIL_ERR_CAPACITY;
        for(size_t j=0;j<i;j++) {
            int collision=s[i].recovery || s[j].recovery ||
                          !s[i].conflict_domain || !s[j].conflict_domain ||
                          s[i].conflict_domain==s[j].conflict_domain ||
                          shares_radio(&s[i],&s[j]);
            if(collision && s[i].start_us<s[j].start_us+s[j].duration_us &&
               s[j].start_us<s[i].start_us+s[i].duration_us)
                return NINLIL_ERR_CONFLICT;
        }
    }
    return recovery_count?NINLIL_OK:NINLIL_ERR_STATE;
}
static int nonzero(const uint8_t *p,size_t n)
{
    uint8_t bits=0u;
    for(size_t i=0;i<n;i++) bits|=p[i];
    return bits!=0u;
}
static int valid(const ninlil_phy_fragment *f)
{
    return f && nonzero(f->operation,16u) && nonzero(f->digest,32u) &&
           f->authority_epoch && f->plan_epoch && f->profile && f->profile_version &&
           f->activate_ms<f->expires_ms && f->count && f->count<=NINLIL_PHY_FRAGMENTS &&
           f->index<f->count && f->length && f->length<=NINLIL_PHY_FRAGMENT_PAYLOAD &&
           (f->index+1u==f->count || f->length==NINLIL_PHY_FRAGMENT_PAYLOAD);
}
static void put(uint8_t *p,uint64_t x,size_t n)
{
    while(n) {p[--n]=(uint8_t)x;x>>=8;}
}
static uint64_t get(const uint8_t *p,size_t n)
{
    uint64_t x=0;
    for(size_t i=0;i<n;i++) x=(x<<8)|p[i];
    return x;
}
int ninlil_phy_fragment_encode(const ninlil_phy_fragment *f,uint8_t *out,
                               size_t capacity,size_t *written)
{
    if(!valid(f) || !out || !written) return NINLIL_ERR_INVALID;
    if(capacity<NINLIL_PHY_FRAGMENT_HEADER+f->length) return NINLIL_ERR_TOO_LARGE;
    out[0]=NINLIL_PHY_PLAN_DISPATCH;out[1]=1u;out[2]=out[3]=0u;
    memcpy(out+4,f->operation,16u);put(out+20,f->authority_epoch,8u);
    put(out+28,f->plan_epoch,8u);put(out+36,f->profile,4u);put(out+40,f->profile_version,4u);
    put(out+44,f->activate_ms,8u);put(out+52,f->expires_ms,8u);memcpy(out+60,f->digest,32u);
    out[92]=f->index;out[93]=f->count;put(out+94,f->length,2u);
    memcpy(out+96,f->payload,f->length);*written=96u+f->length;
    return NINLIL_OK;
}
int ninlil_phy_fragment_decode(const uint8_t *in,size_t length,ninlil_phy_fragment *out)
{
    ninlil_phy_fragment f={0};
    if(!in || !out || length<97u || length>184u || in[0]!=NINLIL_PHY_PLAN_DISPATCH ||
       in[1]!=1u || in[2] || in[3]) return NINLIL_ERR_INVALID;
    memcpy(f.operation,in+4,16u);f.authority_epoch=get(in+20,8u);f.plan_epoch=get(in+28,8u);
    f.profile=(uint32_t)get(in+36,4u);f.profile_version=(uint32_t)get(in+40,4u);
    f.activate_ms=get(in+44,8u);f.expires_ms=get(in+52,8u);memcpy(f.digest,in+60,32u);
    f.index=in[92];f.count=in[93];f.length=(uint16_t)get(in+94,2u);
    if(!valid(&f) || length!=96u+f.length) return NINLIL_ERR_INVALID;
    memcpy(f.payload,in+96,f.length);*out=f;
    return NINLIL_OK;
}
void ninlil_phy_reassembly_open(ninlil_phy_reassembly *s)
{
    if(s) memset(s,0,sizeof(*s));
}
static int same(const ninlil_phy_fragment *a,const ninlil_phy_fragment *b)
{
    return !memcmp(a->operation,b->operation,16u) && !memcmp(a->digest,b->digest,32u) &&
           a->authority_epoch==b->authority_epoch && a->plan_epoch==b->plan_epoch &&
           a->profile==b->profile && a->profile_version==b->profile_version &&
           a->activate_ms==b->activate_ms && a->expires_ms==b->expires_ms && a->count==b->count;
}
int ninlil_phy_reassembly_push(ninlil_phy_reassembly *s,uint16_t peer,
                               const uint8_t *frame,size_t length,uint64_t now,
                               ninlil_time_quality quality,
                               ninlil_phy_digest_verify verify,void *ctx)
{
    ninlil_phy_fragment f;
    uint16_t bit,all;
    size_t offset;
    int rc;
    if(!s || !peer || peer==UINT16_MAX || !verify || quality!=NINLIL_TIME_RESTART_SAFE ||
       now>UINT64_MAX-60000u || now<s->now_ms) return NINLIL_ERR_INVALID;
    if(s->poisoned) return NINLIL_ERR_STATE;
    rc=ninlil_phy_fragment_decode(frame,length,&f);
    if(rc) return rc;
    if(now>=f.expires_ms || (s->active && now>=s->deadline_ms)) return NINLIL_ERR_EXPIRED;
    if(s->active && (s->peer!=peer || !same(&s->binding,&f))) return NINLIL_ERR_CONFLICT;
    bit=(uint16_t)(UINT32_C(1)<<f.index);offset=(size_t)f.index*NINLIL_PHY_FRAGMENT_PAYLOAD;
    s->now_ms=now;
    if(s->mask&bit) {
        size_t old_length=f.index+1u==f.count?s->length-offset:NINLIL_PHY_FRAGMENT_PAYLOAD;
        if(old_length!=f.length || memcmp(s->bytes+offset,f.payload,f.length)) {
            s->poisoned=1u;s->ready=0u;return NINLIL_ERR_CONFLICT;
        }
        return s->ready?NINLIL_OK:NINLIL_ERR_BUSY;
    }
    if(!s->active) {
        s->binding=f;s->peer=peer;s->active=1u;
        s->deadline_ms=now+60000u<f.expires_ms?now+60000u:f.expires_ms;
    }
    memcpy(s->bytes+offset,f.payload,f.length);s->mask|=bit;
    if(f.index+1u==f.count) s->length=(uint16_t)(offset+f.length);
    all=(uint16_t)((UINT32_C(1)<<f.count)-1u);
    if(s->mask!=all) return NINLIL_ERR_BUSY;
    rc=verify(ctx,s->bytes,s->length,f.digest);
    if(rc) {s->poisoned=1u;s->ready=0u;return rc;}
    s->ready=1u;return NINLIL_OK;
}
