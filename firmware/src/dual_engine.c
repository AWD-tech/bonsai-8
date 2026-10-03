/* SPDX-License-Identifier: MIT
 * All four stems on each deck share a playhead. Ring publication is SPSC.
 * An underrun holds the entire deck, preventing one stem drifting behind.
 */
#include "dual_engine.h"
#include <string.h>
#include <limits.h>
static uint32_t bound(uint32_t n, uint32_t lo, uint32_t hi)
{ return n < lo ? lo : n > hi ? hi : n; }
void dd_init(struct dd_engine *e)
{
 memset(e, 0, sizeof(*e)); atomic_init(&e->master, 64);
 for (unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  atomic_init(&d->read,0); atomic_init(&d->source_clock,0); atomic_init(&d->speed,65536);
  atomic_init(&d->playing,0); atomic_init(&d->mute_mask,0);
  bs_init(&d->stretch,0,false);
  for (unsigned s=0;s<4;s++) {
   atomic_init(&d->voice[s].written,0);
   atomic_init(&d->voice[s].gain,k?0:256);
  }
 }
}
void dd_reset_deck(struct dd_deck *d,uint8_t mask)
{
 atomic_store(&d->playing,0); atomic_store(&d->read,0); atomic_store(&d->source_clock,0);
 d->fraction=0; d->envelope=0; d->starved=false;
 d->source_fraction=0;
 bs_init(&d->stretch,0,atomic_load_explicit(&d->stretch.enabled,memory_order_relaxed));
 d->last=(struct dd_wide_frame){0,0};
 for(unsigned s=0;s<4;s++) {
  atomic_store(&d->voice[s].written,0);
  d->voice[s].present=!!(mask&(1u<<s)); d->voice[s].current_gain=0;
 }
}
void dd_set_stretch(struct dd_deck *d,bool enabled)
{
 uint32_t first=atomic_load_explicit(&d->source_clock,memory_order_relaxed);
 bs_init(&d->stretch,first,enabled);d->fraction=0;d->source_fraction=0;
 atomic_store_explicit(&d->read,first,memory_order_release);
}
bool dd_set_tempo(struct dd_deck *d,uint32_t tempo)
{return d&&bs_request(&d->stretch,tempo);}
uint32_t dd_tempo_actual(const struct dd_deck *d)
{return atomic_load_explicit(&d->stretch.enabled,memory_order_acquire)?
 bs_actual(&d->stretch):bound(atomic_load_explicit(&d->speed,memory_order_relaxed),32768,81920);}
static struct bs_frame stretch_source(void *context,unsigned stem,uint32_t p)
{
 struct dd_deck *d=context;
 if(!d->voice[stem].present)return (struct bs_frame){0,0};
 struct dd_frame x=d->voice[stem].ring[p&(DD_RING-1)];return (struct bs_frame){x.l,x.r};
}
static bool stretching(const struct dd_deck *d)
{
 const struct bs_state *s=&d->stretch;
 return atomic_load_explicit(&s->enabled,memory_order_acquire)&&
  (atomic_load_explicit(&s->active,memory_order_acquire)||
   atomic_load_explicit(&s->ready,memory_order_acquire)||
   atomic_load_explicit(&s->requested,memory_order_relaxed)!=65536u);
}
uint32_t dd_stretch_service(struct dd_engine *e,uint32_t budget)
{
 uint32_t used=bs_cancel_stale(&e->stretch_work)?1u:0u;
 for(unsigned k=0;k<2&&used<budget;k++) {
  struct dd_deck *d=&e->deck[k];struct bs_state *s=&d->stretch;
  if(e->stretch_work.owner&&e->stretch_work.owner!=s)continue;
  if(!stretching(d)&&!e->stretch_work.owner)continue;
  uint32_t oldest=atomic_load_explicit(&d->read,memory_order_acquire),written=oldest+DD_RING;
  uint16_t gains[4];bool any=false;uint32_t mute=atomic_load_explicit(&d->mute_mask,memory_order_relaxed);
  for(unsigned j=0;j<4;j++) {
   gains[j]=(mute&(1u<<j))?0:bound(atomic_load_explicit(&d->voice[j].gain,memory_order_relaxed),0,256);
   if(!d->voice[j].present)continue;
   any=true;uint32_t n=atomic_load_explicit(&d->voice[j].written,memory_order_acquire)-oldest;
   if(n>DD_RING)n=0;
   if(n<written-oldest)written=oldest+n;
  }
  if(!any)continue;
  if(!atomic_load_explicit(&s->active,memory_order_acquire)&&
     !atomic_load_explicit(&s->ready,memory_order_acquire)&&!e->stretch_work.owner) {
   uint32_t p=atomic_load_explicit(&d->source_clock,memory_order_acquire);
   s->first=p;atomic_store_explicit(&s->next_nominal,p,memory_order_relaxed);
   atomic_store_explicit(&s->next_fraction,0,memory_order_relaxed);
  }
  used+=bs_service(s,&e->stretch_work,stretch_source,d,gains,oldest,written,budget-used);
 }
 return used;
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
};
static struct dd_wide_frame steady_sample(const struct steady_voice *v,unsigned n,
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
 return (struct dd_wide_frame){l/1024,r/1024};
}
static bool render_steady(struct dd_engine *e,int16_t *out,uint32_t count,
                          uint32_t pos[2],const uint32_t available[2],
                          const uint32_t step[2],const bool play[2],
                          const bool any[2],const uint16_t gain[2][4],uint32_t master)
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
  if(stretching(d))return false;
  running[k]=play[k]&&any[k];
  if(d->starved||d->envelope!=(running[k]?256:0)) return false;
  fraction[k]=d->fraction;
  uint32_t needed=2+((fraction[k]+(count-1)*step[k])>>16);
  if(running[k]&&available[k]<needed) return false;
  for(unsigned s=0;s<4;s++) {
   struct dd_voice *v=&d->voice[s];
   if(!running[k]||!v->present) continue;
   if(v->current_gain!=gain[k][s]) return false;
   if(gain[k][s]) voice[k][voices[k]++]=(struct steady_voice){v->ring,gain[k][s]};
  }
 }
 struct dd_wide_frame last[2]={e->deck[0].last,e->deck[1].last};
 for(uint32_t f=0;f<count;f++) {
  int32_t l=0,r=0;
  for(unsigned k=0;k<2;k++) {
   if(!running[k]) continue;
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
 uint32_t pos[2],before[2],available[2],step[2],clock[2],written[2];
 bool play[2],any[2]={false,false},stretch[2];
 uint16_t gain[2][4]; uint32_t master=bound(atomic_load(&e->master),0,256);
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  pos[k]=atomic_load_explicit(&d->read,memory_order_relaxed); before[k]=pos[k]; available[k]=DD_RING;
  clock[k]=atomic_load_explicit(&d->source_clock,memory_order_relaxed);stretch[k]=stretching(d);
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
  written[k]=pos[k]+available[k];
 }
 if(render_steady(e,out,count,pos,available,step,play,any,gain,master)) goto publish;
 for(uint32_t f=0;f<count;f++) {
  int32_t ml=0,mr=0;
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&e->deck[k];
   bool buffered;
   if(stretch[k]) {
    uint32_t nominal=clock[k],fraction=d->source_fraction/2;
    bool boundary=atomic_load_explicit(&d->stretch.cursor,memory_order_relaxed)==BS_HOP;
    bool ready=(!play[k]||!any[k])?false:bs_activate(&d->stretch,&nominal,&fraction);
    if(ready&&boundary) {
     clock[k]=nominal;d->source_fraction=fraction*2;
     /* A first descriptor starts on a whole native frame. A dry callback
      * may have stopped at its midpoint; do not shorten the first grain by
      * carrying that old interpolation phase into the new output clock. */
     if(!d->stretch.active_tail)d->fraction=0;
    }
    buffered=ready&&bs_buffered(&d->stretch,pos[k],written[k]);
   } else buffered=available[k]>=(d->starved?512u:2u);
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
     if(!v->present) continue;
     if(v->current_gain<gain[k][s]) ++v->current_gain;
     else if(v->current_gain>gain[k][s]) --v->current_gain;
     /* Every source still advances in phase; a voice at zero gain does not
      * need interpolation until its audible gain ramp begins again. */
     if(!v->current_gain) continue;
     struct dd_frame a,b;
     if(stretch[k]) {
      struct bs_frame x=bs_sample(&d->stretch,s,0,stretch_source,d),
       y=bs_sample(&d->stretch,s,1,stretch_source,d);
      a=(struct dd_frame){x.l,x.r};b=(struct dd_frame){y.l,y.r};
     } else {a=v->ring[pos[k]&(DD_RING-1)];b=v->ring[(pos[k]+1)&(DD_RING-1)];}
     int32_t l=interpolate(a.l,b.l,d->fraction);
     int32_t r=interpolate(a.r,b.r,d->fraction);
     dl+=l*v->current_gain; dr+=r*v->current_gain;
    }
    /* 12dB fixed headroom, then saturate the final two-deck bus. */
    dl/=1024; dr/=1024; d->last=(struct dd_wide_frame){dl,dr};
    d->fraction+=stretch[k]?32768u:step[k];
    uint32_t advance=d->fraction>>16;d->fraction&=65535;
    if(stretch[k]) {
     if(advance)bs_advance(&d->stretch);
     d->source_fraction+=bs_actual(&d->stretch);clock[k]+=d->source_fraction>>17;d->source_fraction&=131071;
     uint32_t retain=bs_retain(&d->stretch);
     if((int32_t)(retain-clock[k])>0)retain=clock[k];
     if((int32_t)(retain-pos[k])>0)pos[k]=retain;
    } else {pos[k]+=advance;available[k]-=advance;}
   }
   ml+=dl*d->envelope/256; mr+=dr*d->envelope/256;
  }
  out[2*f]=clamp(ml*(int32_t)master/256,&e->clips);
  out[2*f+1]=clamp(mr*(int32_t)master/256,&e->clips);
 }
publish:
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  /* Tape playback consumes and releases the same number of native frames.
   * Keep the audible clock independent: future stretch modes retain history
   * without advancing the displayed song merely because storage is released. */
  uint32_t consumed=pos[k]-before[k];
  atomic_store_explicit(&d->source_clock,stretch[k]?clock[k]:
   atomic_load_explicit(&d->source_clock,memory_order_relaxed)+consumed,memory_order_release);
  atomic_store_explicit(&d->read,pos[k],memory_order_release);
 }
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
