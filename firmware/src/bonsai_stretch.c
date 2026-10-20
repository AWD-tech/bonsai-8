/* SPDX-License-Identifier: MIT
 * Bounded joint-stereo WSOLA: 60 ms source sequence, 16 ms overlap, +/-25 ms
 * seek. The long sequence streams from source rings. Source clock is nominal;
 * retained ring data and the selected waveform position are separate clocks.
 */
#include "bonsai_stretch.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
enum { CACHE=1,LOOKAHEAD,REFERENCE,TRANSIENT,COARSE,REFINE,TAIL,PUBLISH };
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
 uint32_t scan_count=p.nominal-first+BS_HOP+BS_OVERLAP;
 if(!active) {first=p.nominal;count=2;}
 if(!published(first,count,oldest,written)||
    (active&&!published(chosen+BS_HOP,BS_OVERLAP,oldest,written)))return false;
 /* Cache/reference samples are overwritten before use. Clear only control
  * state so starting one budgeted slice never clears the entire scratch. */
 memset(&w->owner,0,sizeof(*w)-offsetof(struct bs_work,owner));
 w->owner=s;w->plan=p;w->sequence=seq;
 w->first=first;w->count=count;w->limit=count>=BS_OVERLAP?count-BS_OVERLAP:0;
 w->scan_count=scan_count;
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
 float energy=w->energy[0]*w->balance[0]+w->energy[1]*w->balance[1];
 float dot=w->dot[0]*w->balance[0]+w->dot[1]*w->balance[1];
 float score=energy>1?dot/sqrtf(energy):-1e30f;
 if(score>w->highest){w->highest=score;w->best=w->offset;}
 w->index=0;w->dot[0]=w->dot[1]=w->energy[0]=w->energy[1]=0;
 if(w->stage==COARSE) {
  w->offset+=8;
  if(w->offset>w->limit) {
   w->stage=REFINE;w->offset=w->best>7?w->best-7:0;
   w->refine_end=w->best+7;if(w->refine_end>w->limit)w->refine_end=w->limit;
   w->highest=-1e30f;w->stride=1;
  }
 } else if(++w->offset>w->refine_end){w->stage=TAIL;w->index=0;}
}
static void ordinary_search(struct bs_work *w,uint32_t chosen)
{
 w->index=0;w->stage=COARSE;
 uint32_t natural=chosen+BS_HOP-w->first;
 if(w->reference_energy<=1||
    (w->reference_peak>65536.f&&w->reference_peak*8>w->reference_energy&&natural<=w->limit)) {
  w->best=w->reference_energy<=1?w->plan.nominal-w->first:natural;
  w->stage=TAIL;
 } else if(w->plan.ratio==65536u&&chosen+BS_HOP==w->plan.nominal) {
  w->best=w->plan.nominal-w->first;w->stage=TAIL;
 }
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
   struct bs_frame x=mix(read,ctx,w->first+w->index,gain);
   w->cache[w->index]=x;used+=8;
   float energy=(float)x.l*x.l+(float)x.r*x.r;
   w->scan_energy+=energy;if(energy>w->scan_peak)w->scan_peak=energy;
   if(++w->index==w->count) {
    if(w->scan_peak>1&&w->scan_energy>=w->scan_peak*256.f) {
     /* Dense material keeps the ordinary cache/input requirement. */
     w->scan_count=w->count;w->stage=REFERENCE;w->index=0;
    } else w->stage=LOOKAHEAD;
   }
  } else if(w->stage==LOOKAHEAD) {
   if(w->index==w->scan_count){w->stage=REFERENCE;w->index=0;continue;}
   if(budget-used<8)break;
   if(!published(w->first+w->index,1,oldest,written)) {
    /* Optional onset lookahead must never prevent a ready ordinary grain
     * when the producer's bounded refill has not published the far end. */
    w->scan_count=w->index;w->stage=REFERENCE;w->index=0;continue;
   }
   struct bs_frame x=mix(read,ctx,w->first+w->index,gain);
   float energy=(float)x.l*x.l+(float)x.r*x.r;
   w->scan_energy+=energy;if(energy>w->scan_peak)w->scan_peak=energy;
   ++w->index;used+=8;
  } else if(w->stage==REFERENCE) {
   if(budget-used<8)break;
   struct bs_frame x=mix(read,ctx,chosen+BS_HOP+w->index,gain);
   float weight=(float)(w->index*(BS_OVERLAP-w->index))*(1.f/(BS_OVERLAP*BS_OVERLAP));
   w->reference[w->index][0]=x.l*weight;w->reference[w->index][1]=x.r*weight;
   w->reference_power[0]+=(float)x.l*x.l*weight;w->reference_power[1]+=(float)x.r*x.r*weight;
   float energy=(float)x.l*x.l+(float)x.r*x.r;
   int16_t channel[2]={x.l,x.r};
   for(unsigned c=0;c<2;c++)if(channel[c]) {
    int8_t sign=channel[c]>0?1:-1;
    if(w->sign[c]&&w->sign[c]!=sign&&w->crossings[c]<255)w->crossings[c]++;
    w->sign[c]=sign;
   }
   w->reference_energy+=energy;if(energy>w->reference_peak)w->reference_peak=energy;
   used+=8;
   if(++w->index==BS_OVERLAP) {
    /* Balance phase matching across slow/fast channels. Without this, a
     * fast waveform can dominate the shared grain choice and systematically
     * detune the slower channel. One grain still serves every stem/channel. */
    float maximum=fmaxf(w->reference_power[0],w->reference_power[1]);
    for(unsigned c=0;c<2;c++)w->balance[c]=w->reference_power[c]>1?
     maximum/(fmaxf(w->reference_power[c],maximum*.01f)*(1+w->crossings[c])):0;
    w->index=0;w->dot[0]=w->dot[1]=w->energy[0]=w->energy[1]=0;w->highest=-1e30f;w->offset=0;w->stride=8;w->stage=COARSE;
    /* An isolated onset needs its nominal time mapping, not correlation to
     * another copy of itself. Look through the entire next grain so its
     * late onsets cannot escape the overlap-sized correlation cache. */
    if(w->scan_peak>65536.f&&w->scan_energy<w->scan_peak*256.f)w->stage=TRANSIENT;
    else ordinary_search(w,chosen);
   }
  } else if(w->stage==TRANSIENT) {
   uint32_t cost=w->index<w->count?2u:8u;if(budget-used<cost)break;
   if(w->index>=w->count&&!published(w->first+w->index,1,oldest,written)) {
    w->owner=NULL;return used?used:1;
   }
   struct bs_frame x=w->index<w->count?w->cache[w->index]:mix(read,ctx,w->first+w->index,gain);
   float energy=(float)x.l*x.l+(float)x.r*x.r;
   if(energy>w->scan_peak*.0001f)w->nonquiet_count++;
   if(energy>w->scan_peak*.04f) {
    if(!w->onset_count)w->onset=w->index;
    w->onset_last=w->index;w->onset_count++;
   }
   used+=cost;
   if(++w->index==w->scan_count) {
    /* Preserve ordinary overlap on sustained/background material. Replacing
     * an old tail is safe only around one short event surrounded by silence.
     * Require observed quiet at both scan edges: a clipped longer event can
     * otherwise appear short and cause an abrupt cut at the grain boundary. */
    struct bs_frame first=w->cache[0];
    float first_energy=(float)first.l*first.l+(float)first.r*first.r;
    if(first_energy<=w->scan_peak*.0001f&&energy<=w->scan_peak*.0001f&&
       w->onset_count&&w->onset_count<=256&&w->nonquiet_count<=w->onset_count+32&&
       w->onset_last-w->onset+1<=w->onset_count+32) {
     int64_t relative=(int32_t)(w->first+w->onset-w->plan.nominal);
     int64_t best=(int64_t)w->onset-relative*65536/w->plan.ratio;
     if(best<0)best=0;
     if(best>w->limit)best=w->limit;
     w->best=(uint32_t)best;w->transient=true;w->index=0;w->stage=TAIL;
    } else ordinary_search(w,chosen);
   }
  } else if(w->stage==COARSE||w->stage==REFINE) {
   if(budget-used<2)break;
   /* Correlation touches only this worker's cache/reference/scratch. Bound
    * one batch by both the caller's remaining budget and this candidate;
    * validate again before another batch or any shared-tail/publication work.
    * Reading four acquire atomics for every stereo comparison otherwise
    * spends more than 60000 Cortex-M barriers on one dense grain. */
   uint32_t comparisons=(BS_OVERLAP-w->index+w->stride-1u)/w->stride;
   if(comparisons>(budget-used)/2u)comparisons=(budget-used)/2u;
   uint32_t end=w->index+comparisons*w->stride;
   float dot_l=w->dot[0],dot_r=w->dot[1],energy_l=w->energy[0],energy_r=w->energy[1];
   for(uint32_t i=w->index;i<end;i+=w->stride) {
    struct bs_frame x=w->cache[w->offset+i];float l=x.l,r=x.r;
    dot_l+=l*w->reference[i][0];dot_r+=r*w->reference[i][1];
    /* Keep accumulation order and candidate/reference window identical. */
    float weight=(float)(i*(BS_OVERLAP-i))*(1.f/(BS_OVERLAP*BS_OVERLAP));
    energy_l+=l*l*weight;energy_r+=r*r*weight;
   }
   w->dot[0]=dot_l;w->dot[1]=dot_r;w->energy[0]=energy_l;w->energy[1]=energy_r;
   w->index=end;used+=comparisons*2u;
   if(w->index>=BS_OVERLAP)next_candidate(w);
  } else if(w->stage==TAIL) {
   if(budget-used<8)break;
   /* Current overlap ended before begin(). It cannot read this bank again
    * until the ready descriptor is published after the entire copy. */
   uint32_t tail=w->transient?w->first+w->best:chosen+BS_HOP;
   for(unsigned k=0;k<4;k++)s->tail[k][w->index]=read(ctx,k,tail+w->index);
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
static struct bs_frame raw_sample(struct bs_state *s,unsigned stem,uint32_t chosen,uint32_t i,bs_read_fn read,void *ctx)
{
 if(i==BS_HOP)return s->tail[stem][0];
 struct bs_frame x=read(ctx,stem,chosen+i);
 if(s->active_tail&&i<BS_OVERLAP) {
  struct bs_frame old=s->tail[stem][i];
  x.l=((int32_t)old.l*(int32_t)(BS_OVERLAP-i)+(int32_t)x.l*(int32_t)i)/(int32_t)BS_OVERLAP;
  x.r=((int32_t)old.r*(int32_t)(BS_OVERLAP-i)+(int32_t)x.r*(int32_t)i)/(int32_t)BS_OVERLAP;
 }
 return x;
}
const struct bs_frame (*bs_samples(struct bs_state *s,bs_read_fn read,void *ctx))[2]
{
 uint32_t i=atomic_load_explicit(&s->cursor,memory_order_relaxed);
 uint32_t seq=atomic_load_explicit(&s->sequence,memory_order_relaxed);
 if(!s->cached_valid||s->cached_cursor!=i||s->cached_sequence!=seq) {
  bool adjacent=s->cached_valid&&s->cached_cursor+1==i&&s->cached_sequence==seq;
  uint32_t chosen=atomic_load_explicit(&s->chosen,memory_order_relaxed);
  for(unsigned k=0;k<4;k++) {
   /* The next native frame was already read for the preceding midpoint.
    * Descriptor activation invalidates the cache before tail reuse. */
   s->cached[k][0]=adjacent?s->cached[k][1]:raw_sample(s,k,chosen,i,read,ctx);
   s->cached[k][1]=raw_sample(s,k,chosen,i+1,read,ctx);
  }
  s->cached_cursor=i;s->cached_sequence=seq;s->cached_valid=true;
 }
 return s->cached;
}
struct bs_frame bs_sample(struct bs_state *s,unsigned stem,unsigned which,bs_read_fn read,void *ctx)
{return bs_samples(s,read,ctx)[stem][which];}
void bs_advance(struct bs_state *s)
{
 /* The renderer is the sole cursor writer between exclusive mode resets.
  * Publish progress to the planner without an unnecessary exclusive RMW. */
 uint32_t i=atomic_load_explicit(&s->cursor,memory_order_relaxed);
 atomic_store_explicit(&s->cursor,i+1,memory_order_release);
}
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
