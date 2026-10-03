/* SPDX-License-Identifier: MIT
 * All four stems on each deck share a playhead. Ring publication is SPSC.
 * An underrun holds the entire deck, preventing one stem drifting behind.
 */
#include "dual_engine.h"
#include "bonsai_fx.h"
#include <string.h>
#include <limits.h>
static uint32_t bound(uint32_t n, uint32_t lo, uint32_t hi)
{ return n < lo ? lo : n > hi ? hi : n; }
void dd_init(struct dd_engine *e)
{
 memset(e, 0, sizeof(*e)); atomic_init(&e->master, 64);
 for (unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  atomic_init(&d->read,0); atomic_init(&d->speed,65536);
  atomic_init(&d->playing,0); atomic_init(&d->mute_mask,0);
  for (unsigned s=0;s<4;s++) {
   atomic_init(&d->voice[s].written,0);
   atomic_init(&d->voice[s].gain,k?0:256);
  }
 }
}
void dd_reset_deck(struct dd_deck *d,uint8_t mask)
{
 atomic_store(&d->playing,0); atomic_store(&d->read,0);
 d->fraction=0; d->envelope=0; d->starved=false;
 d->last=(struct dd_frame){0,0};
 for(unsigned s=0;s<4;s++) {
  atomic_store(&d->voice[s].written,0);
  d->voice[s].present=!!(mask&(1u<<s)); d->voice[s].current_gain=0;
 }
}
uint32_t dd_room(const struct dd_deck *d,unsigned s)
{
 if(s>=4) return 0;
 uint32_t used=atomic_load_explicit(&d->voice[s].written,memory_order_relaxed)
  -atomic_load_explicit(&d->read,memory_order_acquire);
 return used<=DD_RING?DD_RING-used:0;
}
bool dd_push(struct dd_deck *d,unsigned s,const struct dd_frame *src,uint32_t n)
{
 if(s>=4||n>dd_room(d,s)) return false;
 struct dd_voice *v=&d->voice[s];
 uint32_t w=atomic_load_explicit(&v->written,memory_order_relaxed);
 for(uint32_t i=0;i<n;i++) v->ring[(w+i)&(DD_RING-1)]=src[i];
 atomic_store_explicit(&v->written,w+n,memory_order_release); return true;
}
static int16_t clamp(int32_t n,uint32_t *clips)
{
 if(n>INT16_MAX) {++*clips;return INT16_MAX;}
 if(n<INT16_MIN) {++*clips;return INT16_MIN;}
 return (int16_t)n;
}
static int32_t interpolate(int16_t a,int16_t b,uint32_t fraction)
{
 /* |b-a| <= 65535 and fraction <= 65535: their product fits uint32_t.
  * Applying the sign after the shift preserves C's truncation toward zero,
  * including full-scale opposite-polarity samples, without 64-bit arithmetic
  * in the Cortex-M4's eight-voice, stereo, 48 kHz inner loop. */
 int32_t delta=(int32_t)b-a;
 uint32_t magnitude=(uint32_t)(delta<0?-delta:delta);
 int32_t change=(int32_t)((magnitude*fraction)>>16);
 return a+(delta<0?-change:change);
}
struct steady_voice {
 const struct dd_frame *ring;
 uint32_t gain;
 struct bonsai_fx_voice *fx;
};
static struct dd_frame steady_sample(const struct steady_voice *v,unsigned n,
                                    uint32_t pos,uint32_t fraction)
{
 uint32_t a=pos&(DD_RING-1),b=(pos+1)&(DD_RING-1);
 int32_t l=0,r=0;
 /* At the normal 24k -> 48k playback rate, phases alternate between the
  * original sample and its midpoint. Avoid the general multiply and sign
  * correction there, while retaining its exact rounding toward sample a. */
 if(!fraction) {
  for(unsigned s=0;s<n;s++) {
   struct dd_frame x=v[s].ring[a];
   l+=(int32_t)x.l*(int32_t)v[s].gain;r+=(int32_t)x.r*(int32_t)v[s].gain;
  }
 } else if(fraction==32768) {
  for(unsigned s=0;s<n;s++) {
   struct dd_frame x=v[s].ring[a],y=v[s].ring[b];
   l+=(x.l+((int32_t)y.l-x.l)/2)*(int32_t)v[s].gain;
   r+=(x.r+((int32_t)y.r-x.r)/2)*(int32_t)v[s].gain;
  }
 } else {
  for(unsigned s=0;s<n;s++) {
   struct dd_frame x=v[s].ring[a],y=v[s].ring[b];
   l+=interpolate(x.l,y.l,fraction)*(int32_t)v[s].gain;
   r+=interpolate(x.r,y.r,fraction)*(int32_t)v[s].gain;
  }
 }
 return (struct dd_frame){l/1024,r/1024};
}
static void interpolate_buffer(const struct dd_frame *ring,struct dd_frame *out,
                               uint32_t count,uint32_t pos,uint32_t fraction,
                               uint32_t step)
{
 if(!ring) {memset(out,0,count*sizeof(*out));return;}
 uint32_t i=0;
 if(step==32768&&(fraction==0||fraction==32768)) {
  /* At 1x each source frame yields its original and a midpoint. Read and
   * interpolate that pair once, without per-frame phase multiplication. */
  if(fraction==32768&&count) {
   struct dd_frame x=ring[pos&(DD_RING-1)],y=ring[(pos+1)&(DD_RING-1)];
   out[i++]=(struct dd_frame){x.l+((int32_t)y.l-x.l)/2,x.r+((int32_t)y.r-x.r)/2};
   ++pos;
  }
  for(;i+1<count;i+=2,++pos) {
   struct dd_frame x=ring[pos&(DD_RING-1)],y=ring[(pos+1)&(DD_RING-1)];
   out[i]=x;
   out[i+1]=(struct dd_frame){x.l+((int32_t)y.l-x.l)/2,x.r+((int32_t)y.r-x.r)/2};
  }
  if(i<count) out[i]=ring[pos&(DD_RING-1)];
  return;
 }
 for(;i<count;i++) {
  struct dd_frame x=ring[pos&(DD_RING-1)],y=ring[(pos+1)&(DD_RING-1)];
  out[i]=(struct dd_frame){interpolate(x.l,y.l,fraction),interpolate(x.r,y.r,fraction)};
  fraction+=step;pos+=fraction>>16;fraction&=65535;
 }
}
static void render_effect_chunks(struct dd_engine *e,int16_t *out,uint32_t count,
                                 const struct steady_voice voice[2][4],
                                 const unsigned voices[2],const bool running[2],
                                 uint32_t pos[2],uint32_t fraction[2],
                                 const uint32_t step[2],uint32_t master)
{
 for(uint32_t offset=0;offset<count;offset+=DD_EFFECT_CHUNK) {
  uint32_t n=count-offset;
  if(n>DD_EFFECT_CHUNK) n=DD_EFFECT_CHUNK;
  for(unsigned k=0;k<2;k++) {
   memset(e->effect_sum,0,n*2*sizeof(*e->effect_sum));
   for(unsigned s=0;s<voices[k];s++) {
    const struct steady_voice *v=&voice[k][s];
    interpolate_buffer(v->ring,e->effect_frames,n,pos[k],fraction[k],step[k]);
    if(v->fx) bonsai_fx_process_buffer(v->fx,e->effect_frames,n);
    if(!v->gain) continue;
    for(uint32_t f=0;f<n;f++) {
     e->effect_sum[2*f]+=(int32_t)e->effect_frames[f].l*(int32_t)v->gain;
     e->effect_sum[2*f+1]+=(int32_t)e->effect_frames[f].r*(int32_t)v->gain;
    }
   }
   for(uint32_t f=0;f<n;f++) {
    int32_t l=e->effect_sum[2*f]/1024,r=e->effect_sum[2*f+1]/1024;
    uint32_t i=2*(offset+f);
    if(!k) {out[i]=(int16_t)l;out[i+1]=(int16_t)r;}
    else {
     out[i]=clamp((out[i]+l)*(int32_t)master/256,&e->clips);
     out[i+1]=clamp((out[i+1]+r)*(int32_t)master/256,&e->clips);
    }
    if(running[k]&&f==n-1) e->deck[k].last=(struct dd_frame){l,r};
   }
   if(running[k]) {
    fraction[k]+=n*step[k];pos[k]+=fraction[k]>>16;fraction[k]&=65535;
   }
  }
 }
}
static bool render_steady(struct dd_engine *e,int16_t *out,uint32_t count,
                          uint32_t pos[2],const uint32_t available[2],
                          const uint32_t step[2],const bool play[2],
                          const bool any[2],const uint16_t gain[2][4],uint32_t master,
                          struct bonsai_fx_voice *effect[2][4],bool effects_active)
{
 /* Most blocks have unchanged controls and ample buffered audio. Validate
  * that once, rather than rechecking ramps, presence and starvation for each
  * of eight voices at every output frame. Edge/ramp/starved blocks still run
  * the sample-at-a-time path below. The count bound keeps phase arithmetic
  * within 32 bits; callers may render larger blocks through the general path. */
 if(!count||count>DD_RING) return false;
 struct steady_voice voice[2][4];unsigned voices[2]={0,0};
 uint32_t fraction[2];bool running[2];
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  running[k]=play[k]&&any[k];
  if(d->starved||d->envelope!=(running[k]?256:0)) return false;
  fraction[k]=d->fraction;
  uint32_t needed=2+((fraction[k]+(count-1)*step[k])>>16);
  if(running[k]&&available[k]<needed) return false;
  for(unsigned s=0;s<4;s++) {
   struct dd_voice *v=&d->voice[s];
   if(!running[k]||!v->present) {
    if(effect[k][s]) voice[k][voices[k]++]=(struct steady_voice){NULL,0,effect[k][s]};
    continue;
   }
   if(v->current_gain!=gain[k][s]) return false;
   if(gain[k][s]||effect[k][s])
    voice[k][voices[k]++]=(struct steady_voice){v->ring,gain[k][s],effect[k][s]};
  }
 }
 if(effects_active||!running[0]||!running[1]) {
  render_effect_chunks(e,out,count,voice,voices,running,pos,fraction,step,master);
  for(unsigned k=0;k<2;k++) e->deck[k].fraction=fraction[k];
  return true;
 }
 struct dd_frame last[2]={e->deck[0].last,e->deck[1].last};
 for(uint32_t f=0;f<count;f++) {
  int32_t l=0,r=0;
  for(unsigned k=0;k<2;k++) {
   last[k]=steady_sample(voice[k],voices[k],pos[k],fraction[k]);
   l+=last[k].l;r+=last[k].r;
   fraction[k]+=step[k];pos[k]+=fraction[k]>>16;fraction[k]&=65535;
  }
  out[2*f]=clamp(l*(int32_t)master/256,&e->clips);
  out[2*f+1]=clamp(r*(int32_t)master/256,&e->clips);
 }
 for(unsigned k=0;k<2;k++) {
  e->deck[k].fraction=fraction[k];e->deck[k].last=last[k];
 }
 return true;
}
void dd_render(struct dd_engine *e,int16_t *out,uint32_t count)
{
 uint32_t pos[2],available[2],step[2]; bool play[2],any[2]={false,false};
 uint16_t gain[2][4]; uint32_t master=bound(atomic_load(&e->master),0,256);
 struct bonsai_fx_voice *effect[2][4]={{NULL}};
 bool effects_active=false;
 if(e->fx) {
  bonsai_fx_begin_block(e->fx);
  for(unsigned k=0;k<2;k++) for(unsigned s=0;s<4;s++) {
   struct bonsai_fx_voice *v=&e->fx->voice[k*4+s];
   /* Keep the old effect alive until its bypass fade has completed. */
   if(v->type!=BONSAI_FX_NONE||v->requested_type!=BONSAI_FX_NONE) {
    effect[k][s]=v;effects_active=true;
   }
  }
 }
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  pos[k]=atomic_load_explicit(&d->read,memory_order_relaxed); available[k]=DD_RING;
  step[k]=bound(atomic_load(&d->speed),32768,81920)/2;
  play[k]=atomic_load(&d->playing)!=0; uint32_t mute=atomic_load(&d->mute_mask);
  for(unsigned s=0;s<4;s++) {
   struct dd_voice *v=&d->voice[s];
   gain[k][s]=(mute&(1u<<s))?0:bound(atomic_load(&v->gain),0,256);
   if(!v->present) continue;
   any[k]=true;
   uint32_t n=atomic_load_explicit(&v->written,memory_order_acquire)-pos[k];
   if(n>DD_RING) n=0;
   if(n<available[k]) available[k]=n;
  }
 }
 if(render_steady(e,out,count,pos,available,step,play,any,gain,master,effect,effects_active)) {
  for(unsigned k=0;k<2;k++) atomic_store_explicit(&e->deck[k].read,pos[k],memory_order_release);
  return;
 }
 for(uint32_t f=0;f<count;f++) {
  int32_t ml=0,mr=0;
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&e->deck[k];
   bool buffered=available[k]>=(d->starved?512u:2u);
   if(play[k]&&any[k]&&!buffered) {
    if(!d->starved) {++d->underruns;d->starved=true;}
   } else if(buffered) d->starved=false;
   bool running=play[k]&&any[k]&&buffered;
   if(running&&d->envelope<256) ++d->envelope;
   if(!running&&d->envelope) --d->envelope;
   int32_t dl=d->last.l,dr=d->last.r;
   if(running) {
    dl=dr=0;
    for(unsigned s=0;s<4;s++) {
     struct dd_voice *v=&d->voice[s];
     struct bonsai_fx_voice *fx=effect[k][s];
     if(!v->present) {
      if(fx) (void)bonsai_fx_process(fx,(struct dd_frame){0,0});
      continue;
     }
     if(v->current_gain<gain[k][s]) ++v->current_gain;
     else if(v->current_gain>gain[k][s]) --v->current_gain;
     /* Wet history follows the unmuted source before gain, even while this
      * stem is muted, so unmuting cannot replay a frozen old echo tail. */
     if(!v->current_gain&&!fx) continue;
     struct dd_frame a=v->ring[pos[k]&(DD_RING-1)],b=v->ring[(pos[k]+1)&(DD_RING-1)];
     int32_t l=interpolate(a.l,b.l,d->fraction);
     int32_t r=interpolate(a.r,b.r,d->fraction);
     if(fx) {
      struct dd_frame processed=bonsai_fx_process(fx,(struct dd_frame){l,r});
      l=processed.l;r=processed.r;
     }
     dl+=l*v->current_gain; dr+=r*v->current_gain;
    }
    /* 12dB fixed headroom, then saturate the final two-deck bus. */
    dl/=1024; dr/=1024; d->last=(struct dd_frame){dl,dr};
    d->fraction+=step[k]; uint32_t advance=d->fraction>>16; d->fraction&=65535;
    pos[k]+=advance; available[k]-=advance;
   } else if(effects_active) {
    /* Paused, missing or starved sources feed silence to effect tails. Their
     * output is not audible while transport is stopped, but state and effect
     * changes still advance at the same 48 kHz clock as the other deck. */
    for(unsigned s=0;s<4;s++) if(effect[k][s])
     (void)bonsai_fx_process(effect[k][s],(struct dd_frame){0,0});
   }
   ml+=dl*d->envelope/256; mr+=dr*d->envelope/256;
  }
  out[2*f]=clamp(ml*(int32_t)master/256,&e->clips);
  out[2*f+1]=clamp(mr*(int32_t)master/256,&e->clips);
 }
 for(unsigned k=0;k<2;k++) atomic_store_explicit(&e->deck[k].read,pos[k],memory_order_release);
}
bool dd_pickup_move(struct dd_pickup *p,uint16_t target,uint16_t value)
{
 int delta=(int)value-target;
 if(p->waiting&&((delta>=-6&&delta<=6)||(p->previous>=0&&
  ((p->previous<target&&value>=target)||(p->previous>target&&value<=target))))) p->waiting=false;
 p->previous=value; return !p->waiting;
}
uint32_t dd_decode(const uint8_t b[512],uint8_t codec,struct dd_frame out[DD_MAX_DECODE])
{
 if(b[11]!=0x5b||b[14]>3) return 0;
 if(codec==DD_P14S) {
  if(memcmp(b,"P14S",4)||b[12]!=24||b[13]!=1) return 0;
  const uint8_t *p=b+16;
  for(unsigned g=0;g<70;g++,p+=7) {
   uint32_t a=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
   uint32_t c=(uint32_t)p[3]<<24|(uint32_t)p[4]<<16|(uint32_t)p[5]<<8|p[6];
   int32_t v[4]={(a>>18)&16383,(a>>4)&16383,(c>>14)&16383,c&16383};
   for(unsigned i=0;i<4;i++) {if(v[i]>8191) v[i]-=16384;v[i]=(v[i]*4)>>b[14];}
   out[g*2]=(struct dd_frame){v[0],v[1]}; out[g*2+1]=(struct dd_frame){v[2],v[3]};
  }
  return 140;
 }
 if(codec==DD_P16M) {
  if(memcmp(b,"P16M",4)||b[12]!=240||b[13]!=1||b[14]) return 0;
  for(unsigned i=0;i<248;i++) {
   int16_t v=(int16_t)((uint16_t)b[16+2*i]|(uint16_t)b[17+2*i]<<8);
   out[i]=(struct dd_frame){v,v};
  }
  return 248;
 }
 return 0;
}
