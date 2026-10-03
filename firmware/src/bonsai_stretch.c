/* SPDX-License-Identifier: MIT
 * Bounded joint-stereo WSOLA: 60 ms source sequence, 16 ms overlap, +/-20 ms
 * seek. The long sequence streams from source rings. Source clock is nominal;
 * retained ring data and the selected waveform position are separate clocks.
 */
#include "bonsai_stretch.h"
#include <math.h>
#include <string.h>
enum { CACHE=1,REFERENCE,COARSE,REFINE,TAIL,PUBLISH };
static bool published(uint32_t p,uint32_t n,uint32_t oldest,uint32_t written)
{return p-oldest<0x80000000u&&written-p<0x80000000u&&written-p>=n;}
void bs_init(struct bs_state *s,uint32_t first,bool enabled)
{
 uint32_t generation=atomic_load_explicit(&s->generation,memory_order_relaxed)+1;
 memset(s,0,sizeof(*s));s->first=first;
 atomic_init(&s->generation,generation);atomic_init(&s->requested,65536);
 atomic_init(&s->actual,65536);atomic_init(&s->sequence,0);
 atomic_init(&s->cursor,BS_HOP);atomic_init(&s->chosen,first);
 atomic_init(&s->nominal,first);atomic_init(&s->fraction,0);
 atomic_init(&s->next_nominal,first);atomic_init(&s->next_fraction,0);
 atomic_init(&s->enabled,enabled);atomic_init(&s->active,false);atomic_init(&s->ready,false);
}
bool bs_request(struct bs_state *s,uint32_t ratio)
{
 if(!s||ratio<32768u||ratio>81920u)return false;
 atomic_store_explicit(&s->requested,ratio,memory_order_release);return true;
}
uint32_t bs_actual(const struct bs_state *s)
{return atomic_load_explicit(&s->actual,memory_order_acquire);}
static bool begin(struct bs_state *s,struct bs_work *w,uint32_t oldest,uint32_t written)
{
 if(!atomic_load_explicit(&s->enabled,memory_order_acquire)||
     atomic_load_explicit(&s->ready,memory_order_acquire))return false;
 uint32_t seq=atomic_load_explicit(&s->sequence,memory_order_acquire);
 if(seq&1)return false;
 bool active=atomic_load_explicit(&s->active,memory_order_relaxed);
 if(active&&atomic_load_explicit(&s->cursor,memory_order_acquire)<BS_OVERLAP)return false;
 struct bs_plan p={.nominal=atomic_load_explicit(&s->next_nominal,memory_order_relaxed),
  .fraction=atomic_load_explicit(&s->next_fraction,memory_order_relaxed),
  .ratio=atomic_load_explicit(&s->requested,memory_order_acquire),
  .generation=atomic_load_explicit(&s->generation,memory_order_acquire),.tail=active};
 uint32_t chosen=atomic_load_explicit(&s->chosen,memory_order_relaxed);
 if(atomic_load_explicit(&s->sequence,memory_order_acquire)!=seq)return false;
 uint32_t first=p.nominal-s->first<BS_SEARCH?s->first:p.nominal-BS_SEARCH;
 uint32_t count=p.nominal-first+BS_SEARCH+BS_OVERLAP;
 if(!active) {first=p.nominal;count=2;}
 if(!published(first,count,oldest,written)||
    (active&&!published(chosen+BS_HOP,BS_OVERLAP,oldest,written)))return false;
 memset(w,0,sizeof(*w));w->owner=s;w->plan=p;w->sequence=seq;
 w->first=first;w->count=count;w->limit=count>=BS_OVERLAP?count-BS_OVERLAP:0;
 if(!active){w->plan.chosen=p.nominal;w->stage=PUBLISH;}
 else {w->stage=CACHE;w->best=p.nominal-first;}
 return true;
}
static bool valid(struct bs_state *s,const struct bs_work *w)
{
 return atomic_load_explicit(&s->enabled,memory_order_acquire)&&
  atomic_load_explicit(&s->generation,memory_order_acquire)==w->plan.generation&&
  atomic_load_explicit(&s->sequence,memory_order_acquire)==w->sequence&&
  !atomic_load_explicit(&s->ready,memory_order_acquire);
}
bool bs_cancel_stale(struct bs_work *w)
{
 if(!w->owner||valid(w->owner,w))return false;
 w->owner=NULL;return true;
}
static struct bs_frame mix(bs_read_fn read,void *ctx,uint32_t p,const uint16_t gain[4])
{
 int32_t l=0,r=0;
 for(unsigned k=0;k<4;k++){struct bs_frame x=read(ctx,k,p);l+=(int32_t)x.l*gain[k];r+=(int32_t)x.r*gain[k];}
 return (struct bs_frame){l/1024,r/1024};
}
static void next_candidate(struct bs_work *w)
{
 float score=w->energy>1?w->dot/sqrtf(w->energy):-1e30f;
 if(score>w->highest){w->highest=score;w->best=w->offset;}
 w->index=0;w->dot=w->energy=0;
 if(w->stage==COARSE) {
  w->offset+=8;
  if(w->offset>w->limit) {
   w->stage=REFINE;w->offset=w->best>7?w->best-7:0;
   w->refine_end=w->best+7;if(w->refine_end>w->limit)w->refine_end=w->limit;
   w->highest=-1e30f;w->stride=1;
  }
 } else if(++w->offset>w->refine_end){w->stage=TAIL;w->index=0;}
}
uint32_t bs_service(struct bs_state *s,struct bs_work *w,bs_read_fn read,void *ctx,
                    const uint16_t gain[4],uint32_t oldest,uint32_t written,uint32_t budget)
{
 if(!s||!w||!read||!gain||!budget)return 0;
 if(w->owner&&w->owner!=s)return 0;
 if(w->owner&&!valid(s,w)){w->owner=NULL;return 1;}
 if(!w->owner&&!begin(s,w,oldest,written))return 0;
 uint32_t used=0;
 while(used<budget) {
  if(!valid(s,w)){w->owner=NULL;return used?used:1;}
  uint32_t chosen=atomic_load_explicit(&s->chosen,memory_order_relaxed);
  if(w->stage==CACHE) {
   if(budget-used<8)break;
   if(!published(w->first+w->index,1,oldest,written)){w->owner=NULL;return used?used:1;}
   w->cache[w->index]=mix(read,ctx,w->first+w->index,gain);used+=8;
   if(++w->index==w->count){w->stage=REFERENCE;w->index=0;}
  } else if(w->stage==REFERENCE) {
   if(budget-used<8)break;
   struct bs_frame x=mix(read,ctx,chosen+BS_HOP+w->index,gain);
   float weight=(float)(w->index*(BS_OVERLAP-w->index))*(1.f/(BS_OVERLAP*BS_OVERLAP));
   w->reference[w->index][0]=x.l*weight;w->reference[w->index][1]=x.r*weight;
   float energy=(float)x.l*x.l+(float)x.r*x.r;
   w->reference_energy+=energy;if(energy>w->reference_peak)w->reference_peak=energy;
   used+=8;
   if(++w->index==BS_OVERLAP) {
    w->index=0;w->dot=w->energy=0;w->highest=-1e30f;w->offset=0;w->stride=4;w->stage=COARSE;
    /* No phase information in silence: keep the nominal plan, rather than
     * attracting an unrelated onset because it alone has nonzero energy.
     * A sparse onset already in the old tail uses its natural continuation,
     * when within the bounded seek region, avoiding a crossfade flam. */
    uint32_t natural=chosen+BS_HOP-w->first;
    if(w->reference_energy<=1||
       (w->reference_peak>65536.f&&w->reference_peak*8>w->reference_energy&&natural<=w->limit)) {
     w->best=w->reference_energy<=1?w->plan.nominal-w->first:natural;
     w->stage=TAIL;
    } else if(w->plan.ratio==65536u&&chosen+BS_HOP==w->plan.nominal) {
     w->best=w->plan.nominal-w->first;w->stage=TAIL;
    }
   }
  } else if(w->stage==COARSE||w->stage==REFINE) {
   if(budget-used<2)break;
   struct bs_frame x=w->cache[w->offset+w->index];float l=x.l,r=x.r;
   w->dot+=l*w->reference[w->index][0]+r*w->reference[w->index][1];
   w->energy+=l*l+r*r;used+=2;w->index+=w->stride;
   if(w->index>=BS_OVERLAP)next_candidate(w);
  } else if(w->stage==TAIL) {
   if(budget-used<8)break;
   /* Current overlap ended before begin(). It cannot read this bank again
    * until the ready descriptor is published after the entire copy. */
   for(unsigned k=0;k<4;k++)s->tail[k][w->index]=read(ctx,k,chosen+BS_HOP+w->index);
   used+=8;if(++w->index==BS_OVERLAP){w->plan.chosen=w->first+w->best;w->stage=PUBLISH;}
  } else {
   uint32_t n=w->plan.fraction+BS_HOP*w->plan.ratio;
   w->plan.next_nominal=w->plan.nominal+(n>>16);w->plan.next_fraction=n&65535;
   s->pending=w->plan;atomic_store_explicit(&s->ready,true,memory_order_release);
   w->owner=NULL;return used?used:1;
  }
 }
 return used;
}
bool bs_activate(struct bs_state *s,uint32_t *nominal,uint32_t *fraction)
{
 if(atomic_load_explicit(&s->cursor,memory_order_relaxed)!=BS_HOP)return true;
 if(!atomic_load_explicit(&s->ready,memory_order_acquire))return false;
 struct bs_plan p=s->pending;
 if(p.generation!=atomic_load_explicit(&s->generation,memory_order_acquire)) {
  atomic_store_explicit(&s->ready,false,memory_order_release);return false;
 }
 atomic_fetch_add_explicit(&s->sequence,1,memory_order_acq_rel);
 atomic_store_explicit(&s->chosen,p.chosen,memory_order_relaxed);
 atomic_store_explicit(&s->nominal,p.nominal,memory_order_relaxed);
 atomic_store_explicit(&s->fraction,p.fraction,memory_order_relaxed);
 atomic_store_explicit(&s->next_nominal,p.next_nominal,memory_order_relaxed);
 atomic_store_explicit(&s->next_fraction,p.next_fraction,memory_order_relaxed);
 atomic_store_explicit(&s->actual,p.ratio,memory_order_relaxed);
 s->active_tail=p.tail;s->cached_valid=false;
 atomic_store_explicit(&s->cursor,0,memory_order_relaxed);
 atomic_store_explicit(&s->active,true,memory_order_relaxed);
 atomic_store_explicit(&s->ready,false,memory_order_relaxed);
 atomic_fetch_add_explicit(&s->sequence,1,memory_order_release);
 if(nominal)*nominal=p.nominal;
 if(fraction)*fraction=p.fraction;
 return true;
}
static bool needed_sample(const struct bs_state *s,uint32_t i,uint32_t oldest,uint32_t written)
{
 if(i==BS_HOP) {
  if(!atomic_load_explicit(&s->ready,memory_order_acquire))return false;
  return s->pending.generation==atomic_load_explicit(&s->generation,memory_order_relaxed)&&
   published(s->pending.chosen,1,oldest,written);
 }
 return published(atomic_load_explicit(&s->chosen,memory_order_relaxed)+i,1,oldest,written);
}
bool bs_buffered(struct bs_state *s,uint32_t oldest,uint32_t written)
{
 if(!atomic_load_explicit(&s->active,memory_order_acquire))return false;
 uint32_t i=atomic_load_explicit(&s->cursor,memory_order_relaxed);
 return i<BS_HOP&&needed_sample(s,i,oldest,written)&&needed_sample(s,i+1,oldest,written);
}
static struct bs_frame raw_sample(struct bs_state *s,unsigned stem,uint32_t i,bs_read_fn read,void *ctx)
{
 if(i==BS_HOP)return s->tail[stem][0];
 struct bs_frame x=read(ctx,stem,atomic_load_explicit(&s->chosen,memory_order_relaxed)+i);
 if(s->active_tail&&i<BS_OVERLAP) {
  struct bs_frame old=s->tail[stem][i];
  x.l=((int32_t)old.l*(int32_t)(BS_OVERLAP-i)+(int32_t)x.l*(int32_t)i)/(int32_t)BS_OVERLAP;
  x.r=((int32_t)old.r*(int32_t)(BS_OVERLAP-i)+(int32_t)x.r*(int32_t)i)/(int32_t)BS_OVERLAP;
 }
 return x;
}
struct bs_frame bs_sample(struct bs_state *s,unsigned stem,unsigned which,bs_read_fn read,void *ctx)
{
 uint32_t i=atomic_load_explicit(&s->cursor,memory_order_relaxed);
 uint32_t seq=atomic_load_explicit(&s->sequence,memory_order_relaxed);
 if(!s->cached_valid||s->cached_cursor!=i||s->cached_sequence!=seq) {
  for(unsigned k=0;k<4;k++) {
   s->cached[k][0]=raw_sample(s,k,i,read,ctx);s->cached[k][1]=raw_sample(s,k,i+1,read,ctx);
  }
  s->cached_cursor=i;s->cached_sequence=seq;s->cached_valid=true;
 }
 return s->cached[stem][which];
}
void bs_advance(struct bs_state *s)
{atomic_fetch_add_explicit(&s->cursor,1,memory_order_release);}
uint32_t bs_retain(const struct bs_state *s)
{
 if(!atomic_load_explicit(&s->active,memory_order_acquire))return s->first;
 uint32_t n=atomic_load_explicit(&s->next_nominal,memory_order_relaxed);
 uint32_t future=n-s->first<BS_SEARCH?s->first:n-BS_SEARCH;
 uint32_t chosen=atomic_load_explicit(&s->chosen,memory_order_relaxed);
 uint32_t i=atomic_load_explicit(&s->cursor,memory_order_relaxed);
 uint32_t current=chosen+i;
 if((int32_t)(current-future)<0)future=current;
 if(!atomic_load_explicit(&s->ready,memory_order_acquire)&&
    (int32_t)(chosen+BS_HOP-future)<0)future=chosen+BS_HOP;
 return future;
}
