/* SPDX-License-Identifier: MIT */
#include "bonsai_stretch.h"
#include "dual_engine.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static struct bs_state state;
static struct bs_work work;
static uint32_t oldest,written,reads;
static const uint16_t gains[4]={256,128,64,256};
static struct bs_frame source(void *context,unsigned stem,uint32_t p)
{
 (void)context;assert(p-oldest<0x80000000u&&written-p<0x80000000u&&written-p>0);++reads;
 int16_t l=(int16_t)((p*37+stem*311)%48001u-24000);
 return (struct bs_frame){l,(int16_t)-l};
}
static void prepare(uint32_t budget)
{
 unsigned turns=0;
 while(!atomic_load(&state.ready)) {
  uint32_t n=bs_service(&state,&work,source,NULL,gains,oldest,written,budget);
  assert(n<=budget);assert(n);assert(++turns<100000);
 }
}
static void run(uint32_t rate)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
 bs_init(&state,0,true);assert(bs_request(&state,rate));prepare(64);
 uint32_t nominal,fraction;assert(bs_activate(&state,&nominal,&fraction));assert(!nominal&&!fraction);
 uint32_t previous_chosen=0;
 for(unsigned hop=0;hop<24;hop++) {
  uint32_t chosen=atomic_load(&state.chosen);
  assert(bs_actual(&state)==rate);
  for(unsigned i=0;i<BS_HOP;i++) {
   assert(bs_buffered(&state,oldest,written));
   for(unsigned k=0;k<4;k++) {
    struct bs_frame x=bs_sample(&state,k,0,source,NULL),a=source(NULL,k,chosen+i);
    if(hop&&i<BS_OVERLAP) {
     struct bs_frame tail=source(NULL,k,previous_chosen+BS_HOP+i);
     assert(x.l==((int64_t)tail.l*(BS_OVERLAP-i)+(int64_t)a.l*i)/BS_OVERLAP);
     assert(x.r==((int64_t)tail.r*(BS_OVERLAP-i)+(int64_t)a.r*i)/BS_OVERLAP);
    } else assert(x.l==a.l&&x.r==a.r);
    assert(x.l==-x.r);
   }
   bs_advance(&state);
   if(i==BS_OVERLAP)prepare(64);
  }
  previous_chosen=chosen;
  assert(bs_activate(&state,&nominal,&fraction));
  uint64_t ideal=(uint64_t)(hop+1)*BS_HOP*rate;
  assert(nominal==(uint32_t)(ideal>>16)&&fraction==(uint16_t)ideal);
 }
}
static void reset_and_coalesce(void)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=100000;
 bs_init(&state,0,true);assert(bs_request(&state,49152));prepare(64);assert(bs_activate(&state,NULL,NULL));
 atomic_store(&state.cursor,BS_OVERLAP);
 assert(bs_service(&state,&work,source,NULL,gains,oldest,written,64)==64);
 assert(work.owner==&state&&work.plan.ratio==49152);
 assert(bs_request(&state,73728));prepare(64);
 assert(state.pending.ratio==49152);atomic_store(&state.cursor,BS_HOP);
 assert(bs_activate(&state,NULL,NULL));assert(bs_actual(&state)==49152);
 atomic_store(&state.cursor,BS_OVERLAP);prepare(64);assert(state.pending.ratio==73728);
 atomic_store(&state.ready,false);work.owner=NULL;
 assert(bs_service(&state,&work,source,NULL,gains,oldest,written,64)==64);
 uint32_t old_generation=work.plan.generation;
 bs_init(&state,1234,true);assert(atomic_load(&state.generation)!=old_generation);
 assert(bs_service(&state,&work,source,NULL,gains,oldest,written,64)==1);
 assert(!work.owner&&!atomic_load(&state.ready));
 assert(!bs_request(&state,32767)&&!bs_request(&state,81921));
}
static void missing_and_wrap(void)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=UINT32_MAX-1000;written=oldest+1;
 bs_init(&state,oldest,true);reads=0;
 assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,64));assert(!reads&&!work.owner);
 uint32_t cursor=atomic_load(&state.cursor);assert(!bs_activate(&state,NULL,NULL));assert(atomic_load(&state.cursor)==cursor);
 written=oldest+100000;assert(bs_request(&state,50001));prepare(64);assert(bs_activate(&state,NULL,NULL));
 atomic_store(&state.cursor,BS_OVERLAP);prepare(64);
 assert(state.pending.nominal==oldest+(uint32_t)(((uint64_t)BS_HOP*50001)>>16));
}
static void poison_work(unsigned byte)
{
 assert(!work.owner);
 memset(&work,byte,sizeof(work));work.owner=NULL;
}
static void unchanged_bytes(const void *memory,size_t count,unsigned byte)
{
 const unsigned char *p=memory;
 for(size_t i=0;i<count;i++)assert(p[i]==byte);
}
static void test_begin_does_not_clear_sample_scratch(void)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=100000;
 bs_init(&state,0,true);assert(bs_request(&state,49152));poison_work(0xff);
 /* The first plan needs no sample cache. A one-unit call must publish it
  * without writing either duration-independent, but large, scratch array. */
 assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1)==1);
 assert(atomic_load(&state.ready));
 unchanged_bytes(work.cache,sizeof(work.cache),0xff);
 unchanged_bytes(work.reference,sizeof(work.reference),0xff);
 assert(bs_activate(&state,NULL,NULL));atomic_store(&state.cursor,BS_OVERLAP);
 poison_work(0xa5);reads=0;
 /* A budget smaller than one four-stem copy admits the plan but cannot
  * perform sample work. This distinguishes scalar reset from hidden bulk IO. */
 assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,1));
 assert(work.owner==&state&&!atomic_load(&state.ready)&&!reads);
 unchanged_bytes(work.cache,sizeof(work.cache),0xa5);
 unchanged_bytes(work.reference,sizeof(work.reference),0xa5);
 prepare(8);assert(atomic_load(&state.ready));
}

#define SNAPSHOT_HOPS 8u
struct planning_snapshot {
 uint32_t chosen,nominal,fraction,ratio,next_nominal,next_fraction,generation;
 bool tail;
 uint64_t samples;
};
static uint64_t hash_sample(uint64_t hash,int16_t sample)
{ return (hash^(uint16_t)sample)*1099511628211ULL; }
static void snapshot_plan(struct planning_snapshot *out,uint32_t budget,
                          uint32_t rate,unsigned poison)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
 bs_init(&state,0,true);assert(bs_request(&state,rate));poison_work(poison);prepare(budget);
 assert(bs_activate(&state,NULL,NULL));
 for(unsigned hop=0;hop<SNAPSHOT_HOPS;hop++) {
  uint64_t hash=1469598103934665603ULL;
  for(unsigned i=0;i<BS_HOP;i++) {
   assert(bs_buffered(&state,oldest,written));
   for(unsigned stem=0;stem<4;stem++)for(unsigned which=0;which<2;which++) {
    struct bs_frame sample=bs_sample(&state,stem,which,source,NULL);
    hash=hash_sample(hash_sample(hash,sample.l),sample.r);
   }
   bs_advance(&state);
   if(i==BS_OVERLAP) {poison_work(poison);prepare(budget);}
  }
  struct bs_plan p=state.pending;
  out[hop]=(struct planning_snapshot){p.chosen,p.nominal,p.fraction,p.ratio,
   p.next_nominal,p.next_fraction,p.generation,p.tail,hash};
  assert(bs_activate(&state,NULL,NULL));
 }
}
static void test_poisoned_scratch_and_budget_partition_invariance(void)
{
 const uint32_t rates[]={32768,49152,50001,63570,65536,81920};
 const uint32_t budgets[]={8,9,31,64,127,1024};
 const unsigned poisons[]={0xff,0xa5};
 struct planning_snapshot expected[SNAPSHOT_HOPS],actual[SNAPSHOT_HOPS];
 for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++) {
  snapshot_plan(expected,65536,rates[r],0);
  for(unsigned b=0;b<sizeof(budgets)/sizeof(budgets[0]);b++)
   for(unsigned p=0;p<sizeof(poisons)/sizeof(poisons[0]);p++) {
    snapshot_plan(actual,budgets[b],rates[r],poisons[p]);
    for(unsigned hop=0;hop<SNAPSHOT_HOPS;hop++) {
     const struct planning_snapshot *a=&actual[hop],*e=&expected[hop];
     assert(a->chosen==e->chosen&&a->nominal==e->nominal&&a->fraction==e->fraction);
     assert(a->ratio==e->ratio&&a->next_nominal==e->next_nominal&&a->next_fraction==e->next_fraction);
     assert(a->generation==e->generation&&a->tail==e->tail&&a->samples==e->samples);
    }
   }
 }
}
static void test_slow_extreme_grain_fits_p16m_refill_boundary(void)
{
 /* The adapter waits for eight P16M sectors of room before refilling.
  * At 0.5x and the maximum positive chosen offset, the previous grain's
  * tail has the furthest required endpoint relative to retained history. */
 const uint32_t origins[]={10000u,UINT32_MAX-1000u};
 const uint32_t buffered=DD_RING-8u*248u;
 for(unsigned n=0;n<sizeof(origins)/sizeof(origins[0]);n++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));
  uint32_t nominal=origins[n],chosen=nominal+BS_SEARCH;
  bs_init(&state,nominal-2000u,true);assert(bs_request(&state,32768));
  atomic_store(&state.active,true);atomic_store(&state.chosen,chosen);
  atomic_store(&state.nominal,nominal);atomic_store(&state.next_nominal,nominal+BS_HOP/2);
  atomic_store(&state.cursor,BS_OVERLAP);oldest=bs_retain(&state);
  assert(oldest==nominal+BS_HOP/2-BS_SEARCH);
  uint32_t required=chosen+BS_SEQUENCE-oldest;
  assert(required<=buffered&&"Search window exceeds the P16M refill safety margin");
  written=oldest+required-1;reads=0;
  assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,1024));
  assert(!work.owner&&!reads);
  /* Once the whole boundary is present, all planning and tail copies must
   * finish without another refill, including modulo-2^32 source indices. */
  written=oldest+buffered;prepare(1024);assert(atomic_load(&state.ready));
  atomic_store(&state.cursor,BS_HOP);assert(bs_activate(&state,NULL,NULL));
 }
}
static void test_fast_extreme_grain_falls_back_without_full_lookahead(void)
{
 /* At 1.25x the old tail can retain an extra 192 frames beyond the P16M
  * refill floor. A full transient scan cannot be mandatory: the renderer
  * waits at the final interpolation sample for this plan to publish. */
 const uint32_t origins[]={10000u,UINT32_MAX-1000u};
 const uint32_t buffered=DD_RING-8u*248u;
 const uint16_t silent_gains[4]={0,0,0,0};
 for(unsigned n=0;n<sizeof(origins)/sizeof(origins[0]);n++)for(unsigned silent=0;silent<2;silent++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));
  uint32_t nominal=origins[n],chosen=nominal-BS_SEARCH;
  uint32_t next=nominal+BS_HOP*5u/4u;
  bs_init(&state,nominal-2000u,true);assert(bs_request(&state,81920));
  atomic_store(&state.active,true);atomic_store(&state.chosen,chosen);
  atomic_store(&state.nominal,nominal);atomic_store(&state.next_nominal,next);
  atomic_store(&state.cursor,BS_HOP-1u);oldest=bs_retain(&state);
  assert(oldest==chosen+BS_HOP-1u);written=oldest+buffered;
  assert(next+BS_SEARCH+BS_OVERLAP-oldest<=buffered);
  assert(next+BS_HOP+BS_OVERLAP-oldest>buffered);
  assert(!bs_buffered(&state,oldest,written));
  unsigned turns=0;
  while(!atomic_load(&state.ready)) {
   uint32_t used=bs_service(&state,&work,source,NULL,silent?silent_gains:gains,
                            oldest,written,1024);
   assert(used&&used<=1024);assert(++turns<100000);
  }
  if(silent) {
   /* Sparse input enters optional lookahead and must stop at publication. */
   assert(work.scan_count==written-work.first);assert(!work.transient);
  }
  assert(bs_buffered(&state,oldest,written));
  for(unsigned stem=0;stem<4;stem++)for(unsigned which=0;which<2;which++)
   (void)bs_sample(&state,stem,which,source,NULL);
  bs_advance(&state);assert(bs_activate(&state,NULL,NULL));
 }
}
static uint32_t event_origin;
static int32_t event_first,event_end;
static struct bs_frame boundary_event(void *context,unsigned stem,uint32_t p)
{
 (void)source(context,stem,p); /* Keep the publication/bounds oracle. */
 int32_t relative=(int32_t)(p-event_origin);
 int16_t sample=relative>=event_first&&relative<event_end?10000:0;
 return (struct bs_frame){sample,sample};
}
static void test_transient_requires_observed_quiet_boundaries(void)
{
 const uint32_t origins[]={10000u,UINT32_MAX-1000u};
 for(unsigned n=0;n<sizeof(origins)/sizeof(origins[0]);n++)for(unsigned scenario=0;scenario<4;scenario++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));
  uint32_t nominal=origins[n],chosen=nominal-BS_SEARCH,next=nominal+BS_HOP*5u/4u;
  event_origin=next-BS_SEARCH;
  bs_init(&state,nominal-2000u,true);assert(bs_request(&state,81920));
  atomic_store(&state.active,true);atomic_store(&state.chosen,chosen);
  atomic_store(&state.nominal,nominal);atomic_store(&state.next_nominal,next);
  atomic_store(&state.cursor,BS_HOP-1u);oldest=bs_retain(&state);
  written=oldest+DD_RING-8u*248u;
  int32_t available=(int32_t)(written-event_origin);
  /* Clipped starts/ends look like short isolated events inside the scan.
   * Complete pulses with just one quiet boundary sample remain eligible. */
  event_first=scenario==0?-300:scenario==1?available-100:scenario==2?1:available-49;
  event_end=scenario==0?100:scenario==1?available+300:scenario==2?49:available-1;
  unsigned turns=0;
  while(!atomic_load(&state.ready)) {
   uint32_t used=bs_service(&state,&work,boundary_event,NULL,gains,oldest,written,1024);
   assert(used&&used<=1024);assert(++turns<100000);
  }
  assert(work.transient==(scenario>=2));
  assert(bs_buffered(&state,oldest,written));
  if(!scenario) {
   /* Replacing this old tail with a future silent tail would cut a 10000
    * sample straight to zero at the interpolation/grain boundary. */
   struct bs_frame last=bs_sample(&state,0,0,boundary_event,NULL);
   struct bs_frame next_sample=bs_sample(&state,0,1,boundary_event,NULL);
   assert(last.l==10000&&next_sample.l==10000);
  }
 }
}
static void test_correlation_batches_preserve_budget_and_cancellation(void)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
 bs_init(&state,0,true);assert(bs_request(&state,66846));prepare(64);assert(bs_activate(&state,NULL,NULL));
 atomic_store(&state.cursor,BS_OVERLAP);
 /* Reach the first ordinary candidate using tiny production worker slices. */
 unsigned turns=0;
 while(!work.owner||work.stride!=8||work.index||work.offset||work.highest!=-1e30f) {
  assert(bs_service(&state,&work,source,NULL,gains,oldest,written,8)==8);
  assert(++turns<10000);
 }
 struct bs_work initial=work,batched;
 const uint32_t budgets[]={1,2,3,7,8,31,95,96,97,127,511,512,1023,1024};
 for(unsigned b=0;b<sizeof(budgets)/sizeof(*budgets);b++) {
  work=initial;uint32_t used=bs_service(&state,&work,source,NULL,gains,oldest,written,budgets[b]);
  assert(used==(budgets[b]/2)*2);batched=work;
  work=initial;
  for(unsigned comparison=0;comparison<used/2;comparison++)
   assert(bs_service(&state,&work,source,NULL,gains,oldest,written,2)==2);
  /* Every correlation scalar and future candidate must match the exact same
   * additions performed as separate two-unit calls, not only chosen audio. */
  assert(work.offset==batched.offset&&work.index==batched.index&&work.stage==batched.stage);
  assert(work.best==batched.best&&work.highest==batched.highest);
  assert(!memcmp(work.dot,batched.dot,sizeof(work.dot)));
  assert(!memcmp(work.energy,batched.energy,sizeof(work.energy)));
  assert(!atomic_load(&state.ready));
 }
 /* A reset between slices invalidates private partial correlation without
  * publishing or writing the new generation's tail. */
 work=initial;assert(bs_service(&state,&work,source,NULL,gains,oldest,written,96)==96);
 assert(work.owner==&state);
 bs_init(&state,1234,true);
 assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024)==1);
 assert(!work.owner&&!atomic_load(&state.ready));
 for(unsigned stem=0;stem<BS_STEMS;stem++)for(unsigned i=0;i<BS_OVERLAP;i++)
  assert(!state.tail[stem][i].l&&!state.tail[stem][i].r);
}
static void test_adjacent_pairs_reuse_previous_midpoint(void)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
 bs_init(&state,0,true);assert(bs_request(&state,66846));prepare(64);
 for(unsigned grain=0;grain<3;grain++) {
  assert(bs_activate(&state,NULL,NULL));
  for(unsigned cursor=0;cursor<BS_HOP;cursor++) {
   unsigned before=reads;
   for(unsigned phase=0;phase<2;phase++)for(unsigned stem=0;stem<4;stem++)for(unsigned which=0;which<2;which++)
    (void)bs_sample(&state,stem,which,source,NULL);
   /* Both 48k phases share each pair. The preceding midpoint already read
    * the next pair's first sample; only four new source frames are needed.
    * At the final midpoint the ready tail replaces source reads entirely. */
   unsigned needed=cursor==0?8:cursor==BS_HOP-1?0:4;
   assert(reads-before==needed);
   bs_advance(&state);
   if(cursor==BS_OVERLAP)prepare(64);
  }
 }
}
int main(void)
{
 const uint32_t rates[]={32768,49152,65536,73728,81920,50001};
 for(unsigned r=0;r<6;r++)run(rates[r]);
 reset_and_coalesce();
 missing_and_wrap();
 test_begin_does_not_clear_sample_scratch();
 test_poisoned_scratch_and_budget_partition_invariance();
 test_slow_extreme_grain_fits_p16m_refill_boundary();
 test_fast_extreme_grain_falls_back_without_full_lookahead();
 test_transient_requires_observed_quiet_boundaries();
 test_adjacent_pairs_reuse_previous_midpoint();
 test_correlation_batches_preserve_budget_and_cancellation();
 printf("PASS budgeted stretch: raw per-stem convex oracle, exact1x, stereo, clocks, wrap, bounds, coalescing, reset generation, poisoned scratch and budget partition invariance; state=%zu/work=%zu/two=%zu\n",sizeof(state),sizeof(work),sizeof(state)*2+sizeof(work));
}
