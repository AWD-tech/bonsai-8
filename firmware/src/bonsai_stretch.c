/* SPDX-License-Identifier: MIT
 * Bounded joint-stereo WSOLA: 60 ms source sequence, 16 ms overlap, +/-25 ms
 * seek. The long sequence streams from source rings. Source clock is nominal;
 * retained ring data and the selected waveform position are separate clocks.
 */
#include "bonsai_stretch.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
enum { CACHE=1,LOOKAHEAD,REFERENCE,TRANSIENT,COARSE,REFINE,TAIL,PUBLISH,PACK,ENERGY };
/* Constant-fold the exact existing float expression once into flash. Every
 * candidate uses the same overlap window, so no per-comparison conversion or
 * multiplication is necessary. No RAM or correlation arithmetic changes. */
#define BS_W(i) ((float)((i)*(BS_OVERLAP-(i)))*(1.f/(BS_OVERLAP*BS_OVERLAP)))
#define BS_W8(i) BS_W(i),BS_W((i)+1),BS_W((i)+2),BS_W((i)+3),\
 BS_W((i)+4),BS_W((i)+5),BS_W((i)+6),BS_W((i)+7)
static const float window[BS_OVERLAP]={
 BS_W8(0),BS_W8(8),BS_W8(16),BS_W8(24),
 BS_W8(32),BS_W8(40),BS_W8(48),BS_W8(56),
 BS_W8(64),BS_W8(72),BS_W8(80),BS_W8(88),
 BS_W8(96),BS_W8(104),BS_W8(112),BS_W8(120),
 BS_W8(128),BS_W8(136),BS_W8(144),BS_W8(152),
 BS_W8(160),BS_W8(168),BS_W8(176),BS_W8(184),
 BS_W8(192),BS_W8(200),BS_W8(208),BS_W8(216),
 BS_W8(224),BS_W8(232),BS_W8(240),BS_W8(248),
 BS_W8(256),BS_W8(264),BS_W8(272),BS_W8(280),
 BS_W8(288),BS_W8(296),BS_W8(304),BS_W8(312),
 BS_W8(320),BS_W8(328),BS_W8(336),BS_W8(344),
 BS_W8(352),BS_W8(360),BS_W8(368),BS_W8(376),
};
#undef BS_W8
#undef BS_W
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
 if(s->prepared) {
  if(s->pending.generation!=atomic_load_explicit(&s->generation,memory_order_acquire)||
     s->prepared_sequence!=seq){s->prepared=false;return false;}
  if(atomic_load_explicit(&s->cursor,memory_order_acquire)<BS_OVERLAP)return false;
  uint32_t tail=s->prepared_transient?s->pending.chosen:
   atomic_load_explicit(&s->chosen,memory_order_relaxed)+BS_HOP;
  if(!published(tail,BS_OVERLAP,oldest,written))return false;
  memset(&w->owner,0,sizeof(*w)-offsetof(struct bs_work,owner));
  w->owner=s;w->plan=s->pending;w->sequence=seq;w->first=w->plan.chosen;
  w->transient=s->prepared_transient;w->stage=TAIL;
  return true;
 }
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
 /* For stride s, i=s*j and i*(OVERLAP-i)=s*s*j*(N-j).
  * The common reference scaling cancels when comparing candidate scores. */
 float factor=(float)(w->stride*w->stride)*(1.f/(BS_OVERLAP*BS_OVERLAP));
 float energy=(float)w->moment_e[0]*factor*w->balance[0]+
  (float)w->moment_e[1]*factor*w->balance[1];
 float score=energy>1?(float)w->corr_dot/sqrtf(energy):-1e30f;
 if(score>w->highest){w->highest=score;w->best=w->offset;}
 w->index=0;w->corr_dot=0;
 if(w->stage==COARSE&&w->offset+8>w->limit) {
  w->offset=w->best>7?w->best-7:0;
  w->refine_end=w->best+7;if(w->refine_end>w->limit)w->refine_end=w->limit;
  w->highest=-1e30f;w->stride=1;w->stage=ENERGY;
  memset(w->moment_a,0,sizeof(w->moment_a));
  memset(w->moment_b,0,sizeof(w->moment_b));
  memset(w->moment_e,0,sizeof(w->moment_e));
  return;
 }
 if(w->stage==REFINE&&w->offset+1>w->refine_end){w->stage=TAIL;return;}
 struct bs_frame old=w->cache[w->offset],next=w->cache[w->offset+BS_OVERLAP];
 int32_t x[2]={old.l,old.r},y[2]={next.l,next.r};
 uint32_t n=BS_OVERLAP/w->stride;
 for(unsigned ch=0;ch<2;ch++) {
  int64_t removed=(int64_t)x[ch]*x[ch],added=(int64_t)y[ch]*y[ch];
  int64_t keep=w->moment_a[ch]-removed;
  /* Sliding q(j)=j*(N-j) left once gives
   * q(j-1)=q(j)+2*j-(N+1). All operations remain exact. */
  w->moment_e[ch]+=2*w->moment_b[ch]-(n+1)*keep+(n-1)*added;
  w->moment_b[ch]+=(n-1)*added-keep;
  w->moment_a[ch]=keep+added;
 }
 w->offset+=w->stride;
}
static void pack_reference(struct bs_work *w)
{
 /* window <= .25. A shared scale preserves the existing channel balance;
  * nearest rounding loses at most half one coefficient unit. It cannot be
  * bit-identical to the old float accumulation near equal-score candidates.
  * Every coefficient stays within int16, even at full-scale source input. */
 float maximum=.25f*fmaxf(w->reference_abs[0]*w->balance[0],
                          w->reference_abs[1]*w->balance[1]);
 w->pack_scale=32767.f/fmaxf(1.f,maximum);w->stage=PACK;
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
uint32_t bs_service_with_batch(struct bs_state *s,struct bs_work *w,bs_read_fn read,void *ctx,
                    const uint16_t gain[4],uint32_t oldest,uint32_t written,uint32_t budget,
                    bs_mix_batch_fn batch)
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
   uint32_t count=(budget-used)/8u;
   if(count>w->count-w->index)count=w->count-w->index;
   uint32_t first=w->first+w->index;
   if(!published(first,1,oldest,written)){w->owner=NULL;return used?used:1;}
   if(count>written-first)count=written-first;
   uint32_t end=w->index+count;
   /* Cache writes are private. Source publication is fixed for this slice;
    * validate once per bounded batch, before any eventual shared-tail work. */
   if(batch)batch(ctx,first,count,gain,w->cache+w->index);
   for(uint32_t i=w->index;i<end;i++) {
    struct bs_frame x=batch?w->cache[i]:mix(read,ctx,w->first+i,gain);w->cache[i]=x;
    float energy=(float)x.l*x.l+(float)x.r*x.r;
    w->scan_energy+=energy;if(energy>w->scan_peak)w->scan_peak=energy;
   }
   w->index=end;used+=count*8u;
   if(w->index==w->count) {
    struct bs_frame first_frame=w->cache[0];
    float first_energy=(float)first_frame.l*first_frame.l+(float)first_frame.r*first_frame.r;
    /* An isolated onset requires a quiet first boundary. Even a future
     * full-scale stereo peak cannot make this boundary quiet, so optional
     * lookahead cannot change the eventual ordinary-search decision. */
    if((w->scan_peak>1&&w->scan_energy>=w->scan_peak*256.f)||
       first_energy>2147483648.f*.0001f) {
     /* Keep the ordinary cache/input requirement. */
     w->scan_count=w->count;w->stage=REFERENCE;w->index=0;
    } else w->stage=LOOKAHEAD;
   }
  } else if(w->stage==LOOKAHEAD) {
   if(w->index==w->scan_count){w->stage=REFERENCE;w->index=0;continue;}
   if(budget-used<8)break;
   uint32_t first=w->first+w->index;
   if(!published(first,1,oldest,written)) {
    w->scan_count=w->index;w->stage=REFERENCE;w->index=0;continue;
   }
   uint32_t count=(budget-used)/8u;
   if(count>w->scan_count-w->index)count=w->scan_count-w->index;
   if(count>written-first)count=written-first;
   uint32_t end=w->index+count;
   /* All changes are private. Validate again after this bounded batch,
    * before another stage can copy the shared tail or publish a plan. */
   for(uint32_t i=w->index;i<end;i++) {
    struct bs_frame x=mix(read,ctx,w->first+i,gain);
    float energy=(float)x.l*x.l+(float)x.r*x.r;
    w->scan_energy+=energy;if(energy>w->scan_peak)w->scan_peak=energy;
   }
   w->index=end;used+=count*8u;
  } else if(w->stage==REFERENCE) {
   if(budget-used<8)break;
   uint32_t count=(budget-used)/8u;
   if(count>BS_OVERLAP-w->index)count=BS_OVERLAP-w->index;
   uint32_t end=w->index+count;
   if(batch)batch(ctx,chosen+BS_HOP+w->index,count,gain,w->reference+w->index);
   for(uint32_t i=w->index;i<end;i++) {
    struct bs_frame x=batch?w->reference[i]:mix(read,ctx,chosen+BS_HOP+i,gain);
    float weight=window[i];
    w->reference[i]=x;
    uint32_t abs_l=x.l<0?-(int32_t)x.l:x.l,abs_r=x.r<0?-(int32_t)x.r:x.r;
    if(abs_l>w->reference_abs[0])w->reference_abs[0]=abs_l;
    if(abs_r>w->reference_abs[1])w->reference_abs[1]=abs_r;
    w->reference_power[0]+=(float)x.l*x.l*weight;w->reference_power[1]+=(float)x.r*x.r*weight;
    float energy=(float)x.l*x.l+(float)x.r*x.r;
    int16_t channel[2]={x.l,x.r};
    for(unsigned c=0;c<2;c++)if(channel[c]) {
     int8_t sign=channel[c]>0?1:-1;
     if(w->sign[c]&&w->sign[c]!=sign&&w->crossings[c]<255)w->crossings[c]++;
     w->sign[c]=sign;
    }
    w->reference_energy+=energy;if(energy>w->reference_peak)w->reference_peak=energy;
   }
   w->index=end;used+=count*8u;
   if(w->index==BS_OVERLAP) {
    /* Balance phase matching across slow/fast channels. Without this, a
     * fast waveform can dominate the shared grain choice and systematically
     * detune the slower channel. One grain still serves every stem/channel. */
    float maximum=fmaxf(w->reference_power[0],w->reference_power[1]);
    for(unsigned c=0;c<2;c++)w->balance[c]=w->reference_power[c]>1?
     maximum/(fmaxf(w->reference_power[c],maximum*.01f)*(1+w->crossings[c])):0;
    w->index=0;w->corr_dot=0;w->highest=-1e30f;w->offset=0;w->stride=8;w->stage=COARSE;
    /* An isolated onset needs its nominal time mapping, not correlation to
     * another copy of itself. Look through the entire next grain so its
     * late onsets cannot escape the overlap-sized correlation cache. */
    struct bs_frame first_frame=w->cache[0];
    float first_energy=(float)first_frame.l*first_frame.l+(float)first_frame.r*first_frame.r;
    /* With the final peak known, a nonquiet first boundary alone rules out
     * transient replacement; scanning every onset cannot reverse that. */
    if(w->scan_peak>65536.f&&w->scan_energy<w->scan_peak*256.f&&
       first_energy<=w->scan_peak*.0001f)w->stage=TRANSIENT;
    else ordinary_search(w,chosen);
    if(w->stage==COARSE)pack_reference(w);
   }
  } else if(w->stage==TRANSIENT) {
   uint32_t cost=w->index<w->count?2u:8u;if(budget-used<cost)break;
   uint32_t count=(budget-used)/cost;
   uint32_t limit=w->index<w->count?w->count:w->scan_count;
   if(count>limit-w->index)count=limit-w->index;
   if(w->index>=w->count) {
    uint32_t first=w->first+w->index;
    if(!published(first,1,oldest,written)){w->owner=NULL;return used?used:1;}
    if(count>written-first)count=written-first;
   }
   uint32_t end=w->index+count;float energy=0;
   /* Stay within one cached/source region and one caller budget. Preserve
    * sample order and comparisons while avoiding per-frame acquire loads. */
   for(;w->index<end;w->index++) {
    struct bs_frame x=w->index<w->count?w->cache[w->index]:mix(read,ctx,w->first+w->index,gain);
    energy=(float)x.l*x.l+(float)x.r*x.r;
    if(energy>w->scan_peak*.0001f)w->nonquiet_count++;
    if(energy>w->scan_peak*.04f) {
     if(!w->onset_count)w->onset=w->index;
     w->onset_last=w->index;w->onset_count++;
    }
    used+=cost;
    /* These failures are monotone: onset count can only increase; at most
     * 256 onsets permit at most 288 nonquiet frames; gaps between onsets
     * never shrink. The full scan would choose ordinary search anyway. */
    if(w->onset_count>256||w->nonquiet_count>288||
       (w->onset_count&&w->onset_last-w->onset+1>w->onset_count+32)) {
     ordinary_search(w,chosen);
     if(w->stage==COARSE)pack_reference(w);
     break;
    }
   }
   if(w->stage!=TRANSIENT)continue;
   if(w->index==w->scan_count) {
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
    } else {
     ordinary_search(w,chosen);
     if(w->stage==COARSE)pack_reference(w);
    }
   }
  } else if(w->stage==PACK) {
   if(budget-used<2)break;
   uint32_t count=(budget-used)/2;
   if(count>BS_OVERLAP-w->index)count=BS_OVERLAP-w->index;
   uint32_t end=w->index+count;
   for(uint32_t i=w->index;i<end;i++) {
    struct bs_frame x=w->reference[i];
    float l=x.l*window[i]*w->balance[0]*w->pack_scale;
    float r=x.r*window[i]*w->balance[1]*w->pack_scale;
    int32_t il=(int32_t)(l+(l>=0?.5f:-.5f)),ir=(int32_t)(r+(r>=0?.5f:-.5f));
    if(il>32767)il=32767;
    if(il<-32767)il=-32767;
    if(ir>32767)ir=32767;
    if(ir<-32767)ir=-32767;
    w->reference[i]=(struct bs_frame){il,ir};
   }
   w->index=end;used+=2*count;
   if(w->index==BS_OVERLAP) {w->index=0;w->stage=ENERGY;}
  } else if(w->stage==ENERGY) {
   /* Initialize three moments only once per coarse/refine search. Across
    * every candidate, next_candidate rolls them forward in constant work.
    * Worst E is 10133030442106880 at N=384/full scale, well inside int64. */
   if(budget-used<2)break;
   uint32_t count=(BS_OVERLAP-w->index+w->stride-1)/w->stride;
   if(count>(budget-used)/2)count=(budget-used)/2;
   uint32_t end=w->index+count*w->stride,n=BS_OVERLAP/w->stride;
   for(uint32_t i=w->index;i<end;i+=w->stride) {
    struct bs_frame x=w->cache[w->offset+i];int32_t channels[2]={x.l,x.r};
    uint32_t j=i/w->stride;
    for(unsigned ch=0;ch<2;ch++) {
     int64_t energy=(int64_t)channels[ch]*channels[ch];
     w->moment_a[ch]+=energy;w->moment_b[ch]+=j*energy;
     w->moment_e[ch]+=(int64_t)j*(n-j)*energy;
    }
   }
   w->index=end;used+=2*count;
   if(w->index>=BS_OVERLAP){w->index=0;w->stage=w->stride==8?COARSE:REFINE;}
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
   /* Joint-stereo packed multiply-accumulate is exact on host and Cortex.
    * Worst sum is 824608555008, so the 64-bit accumulator cannot overflow.
    * memcpy handles alignment and aliasing without type-punning samples. */
   int64_t dot=w->corr_dot;
   const struct bs_frame *cache=w->cache+w->offset,*reference=w->reference;
   uint32_t stride=w->stride;
   for(uint32_t i=w->index;i<end;i+=stride) {
#ifdef __ARM_FEATURE_DSP
    uint32_t a,b;memcpy(&a,&cache[i],sizeof(a));memcpy(&b,&reference[i],sizeof(b));
    dot=__builtin_arm_smlald(a,b,dot);
#else
    struct bs_frame x=cache[i],r=reference[i];
    dot+=(int64_t)x.l*r.l+(int64_t)x.r*r.r;
#endif
   }
   w->corr_dot=dot;
   w->index=end;used+=comparisons*2u;
   if(w->index>=BS_OVERLAP)next_candidate(w);
  } else if(w->stage==TAIL) {
   if(budget-used<8)break;
   /* Search uses only private scratch and can finish before the renderer's
    * old overlap. Park its scalar result so a paused/early deck cannot hold
    * the only worker while the other deck needs it. The renderer reads
    * pending only after ready, and reset exclusively clears prepared. */
   if(atomic_load_explicit(&s->cursor,memory_order_acquire)<BS_OVERLAP) {
    w->plan.chosen=w->first+w->best;s->pending=w->plan;
    s->prepared_sequence=w->sequence;s->prepared_transient=w->transient;
    s->prepared=true;w->owner=NULL;return used?used:1;
   }
   /* Cursor publication guarantees the old overlap (including its cached
    * next native sample) ended. Tail cannot be read again until publication. */
   uint32_t tail=w->transient?w->first+w->best:chosen+BS_HOP;
   uint32_t count=(budget-used)/8u;
   if(count>BS_OVERLAP-w->index)count=BS_OVERLAP-w->index;
   if(!published(tail+w->index,count,oldest,written)){w->owner=NULL;return used?used:1;}
   uint32_t end=w->index+count;
   /* Resets and source writes belong to this worker and cannot interleave.
    * ready remains false, so audio can only advance/pause the existing grain;
    * it cannot change chosen/sequence or read this tail until publication. */
   for(uint32_t i=w->index;i<end;i++)
    for(unsigned k=0;k<4;k++)s->tail[k][i]=read(ctx,k,tail+i);
   w->index=end;used+=count*8u;
   if(w->index==BS_OVERLAP){w->plan.chosen=w->first+w->best;w->stage=PUBLISH;}
  } else {
   uint32_t n=w->plan.fraction+BS_HOP*w->plan.ratio;
   w->plan.next_nominal=w->plan.nominal+(n>>16);w->plan.next_fraction=n&65535;
   s->pending=w->plan;s->prepared=false;atomic_store_explicit(&s->ready,true,memory_order_release);
   w->owner=NULL;return used?used:1;
  }
 }
 return used;
}
uint32_t bs_service(struct bs_state *s,struct bs_work *w,bs_read_fn read,void *ctx,
                    const uint16_t gain[4],uint32_t oldest,uint32_t written,uint32_t budget)
{return bs_service_with_batch(s,w,read,ctx,gain,oldest,written,budget,NULL);}
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
