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
void dd_render(struct dd_engine *e,int16_t *out,uint32_t count)
{
 uint32_t pos[2],available[2],step[2]; bool play[2],any[2]={false,false};
 uint16_t gain[2][4]; uint32_t master=bound(atomic_load(&e->master),0,256);
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
     struct dd_voice *v=&d->voice[s]; if(!v->present) continue;
     if(v->current_gain<gain[k][s]) ++v->current_gain;
     else if(v->current_gain>gain[k][s]) --v->current_gain;
     struct dd_frame a=v->ring[pos[k]&(DD_RING-1)],b=v->ring[(pos[k]+1)&(DD_RING-1)];
     /* Difference fits 17 signed bits; int64 prevents full-scale overflow. */
     int32_t l=a.l+(int32_t)(((int64_t)b.l-a.l)*d->fraction/65536);
     int32_t r=a.r+(int32_t)(((int64_t)b.r-a.r)*d->fraction/65536);
     dl+=l*v->current_gain; dr+=r*v->current_gain;
    }
    /* 12dB fixed headroom, then saturate the final two-deck bus. */
    dl/=1024; dr/=1024; d->last=(struct dd_frame){dl,dr};
    d->fraction+=step[k]; uint32_t advance=d->fraction>>16; d->fraction&=65535;
    pos[k]+=advance; available[k]-=advance;
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
