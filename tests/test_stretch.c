/* SPDX-License-Identifier: MIT */
#include "bonsai_stretch.h"
#include "dual_engine.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
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
                          uint32_t rate,unsigned poison,bool early)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
 bs_init(&state,0,true);assert(bs_request(&state,rate));poison_work(poison);prepare(budget);
 assert(bs_activate(&state,NULL,NULL));
 for(unsigned hop=0;hop<SNAPSHOT_HOPS;hop++) {
  if(early) {
   struct bs_frame old_tail[BS_STEMS][BS_OVERLAP];memcpy(old_tail,state.tail,sizeof(old_tail));
   poison_work(poison);unsigned calls=0;
   while(!state.prepared) {
    uint32_t used=bs_service(&state,&work,source,NULL,gains,oldest,written,budget);
    assert(used&&used<=budget&&++calls<100000);
    assert(!atomic_load(&state.ready)&&!memcmp(old_tail,state.tail,sizeof(old_tail)));
   }
   assert(!work.owner&&state.prepared_sequence==atomic_load(&state.sequence));
   assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,budget));
  }
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
  snapshot_plan(expected,65536,rates[r],0,false);
  for(unsigned b=0;b<sizeof(budgets)/sizeof(budgets[0]);b++)
   for(unsigned p=0;p<sizeof(poisons)/sizeof(poisons[0]);p++)for(unsigned early=0;early<2;early++) {
    snapshot_plan(actual,budgets[b],rates[r],poisons[p],early);
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
 const uint32_t budgets[]={1,2,3,7,8,31,95,96,97,127,511,512,1023,1024};
 for(unsigned refinement=0;refinement<2;refinement++) {
  /* PACK and ENERGY may also begin at index zero. Nonzero complete moments
   * plus the requested stride identify actual coarse/refine correlation. */
  unsigned turns=0;
  while(!work.owner||work.stride!=(refinement?1:8)||work.index||
        !work.moment_e[0]||work.highest!=-1e30f) {
   uint32_t used=bs_service(&state,&work,source,NULL,gains,oldest,written,8);
   assert(used&&used<=8);assert(++turns<10000);
  }
  struct bs_work initial=work,batched;
  for(unsigned b=0;b<sizeof(budgets)/sizeof(*budgets);b++) {
   work=initial;uint32_t used=bs_service(&state,&work,source,NULL,gains,oldest,written,budgets[b]);
   assert(used==(budgets[b]/2)*2);batched=work;
   work=initial;
   for(unsigned comparison=0;comparison<used/2;comparison++)
    assert(bs_service(&state,&work,source,NULL,gains,oldest,written,2)==2);
   /* Includes packed dot, every exact energy moment, chosen candidate,
    * stage transitions and all remaining scalar/scratch state. */
   assert(!memcmp(&work,&batched,sizeof(work)));
   assert(!atomic_load(&state.ready));
  }
  work=initial;
  if(!refinement) {
   assert(bs_service(&state,&work,source,NULL,gains,oldest,written,96)==96);
  } else {
   assert(bs_service(&state,&work,source,NULL,gains,oldest,written,96)==96);
   assert(work.owner==&state);
   bs_init(&state,1234,true);
   assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024)==1);
   assert(!work.owner&&!atomic_load(&state.ready));
   for(unsigned stem=0;stem<BS_STEMS;stem++)for(unsigned i=0;i<BS_OVERLAP;i++)
    assert(!state.tail[stem][i].l&&!state.tail[stem][i].r);
  }
 }
}
static unsigned moment_pattern;
static struct bs_frame moment_source(void *context,unsigned stem,uint32_t p)
{
 (void)source(context,stem,p);
 uint32_t x=p*747796405u+2891336453u;
 x=((x>>((x>>28u)+4u))^x)*277803737u;x=(x>>22u)^x;
 if(moment_pattern==0)return (struct bs_frame){INT16_MIN,INT16_MAX};
 if(moment_pattern==1)return (struct bs_frame){INT16_MAX,INT16_MIN};
 if(moment_pattern==3)return (struct bs_frame){(int16_t)(x%257)-128,(int16_t)(x>>16)};
 if(moment_pattern==4)return (struct bs_frame){(int16_t)x,(int16_t)(x%257)-128};
 return (struct bs_frame){(int16_t)x,(int16_t)(x>>16)};
}
static void assert_exact_moments(void)
{
 uint32_t n=BS_OVERLAP/work.stride;
 int64_t a[2]={0},b[2]={0},e[2]={0};
 for(uint32_t j=0;j<n;j++) {
  struct bs_frame x=work.cache[work.offset+j*work.stride];
  int32_t channels[2]={x.l,x.r};
  for(unsigned ch=0;ch<2;ch++) {
   int64_t energy=(int64_t)channels[ch]*channels[ch];
   a[ch]+=energy;b[ch]+=j*energy;e[ch]+=(int64_t)j*(n-j)*energy;
  }
 }
 assert(!memcmp(a,work.moment_a,sizeof(a)));
 assert(!memcmp(b,work.moment_b,sizeof(b)));
 assert(!memcmp(e,work.moment_e,sizeof(e)));
}
static void test_packed_reference_and_rolling_energy(void)
{
 const uint16_t full[4]={256,256,256,256};
 const uint32_t origins[]={10000,UINT32_MAX-1000};
 unsigned checked=0;
 for(moment_pattern=0;moment_pattern<5;moment_pattern++)for(unsigned origin=0;origin<2;origin++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=origins[origin];written=oldest+1000000;
  bs_init(&state,oldest,true);assert(bs_request(&state,66846));
  assert(bs_service(&state,&work,moment_source,NULL,full,oldest,written,8)==1);
  assert(bs_activate(&state,NULL,NULL));atomic_store(&state.cursor,BS_OVERLAP);
  uint8_t coarse=0,refine=0;
  unsigned calls=0,candidates=0;
  while(!atomic_load(&state.ready)) {
   if(work.owner&&!work.index&&work.moment_e[0]&&work.highest==-1e30f) {
    if(work.stride==8&&!coarse) {
     coarse=work.stage;
     /* Every packed coefficient has at most half-unit rounding error.
      * The common scale retains relative stereo balance and polarity. */
     for(unsigned i=0;i<BS_OVERLAP;i++) {
      struct bs_frame raw=moment_source(NULL,0,atomic_load(&state.chosen)+BS_HOP+i);
      float weight=(float)(i*(BS_OVERLAP-i))*(1.f/(BS_OVERLAP*BS_OVERLAP));
      float l=raw.l*weight*work.balance[0]*work.pack_scale;
      float r=raw.r*weight*work.balance[1]*work.pack_scale;
      assert(fabsf(l-work.reference[i].l)<=.503f);
      assert(fabsf(r-work.reference[i].r)<=.503f);
     }
    }
    if(work.stride==1&&!refine)refine=work.stage;
   }
   if(work.owner&&!work.index&&(work.stage==coarse||work.stage==refine)) {
    assert_exact_moments();++checked;++candidates;
   }
   /* Two-unit slices expose every candidate boundary; source/tail stages
    * need eight. A zero return only means their next source frame cannot
    * fit this particular slice, so retry once with that documented cost. */
   uint32_t used=bs_service(&state,&work,moment_source,NULL,full,oldest,written,2);
   if(!used)used=bs_service(&state,&work,moment_source,NULL,full,oldest,written,8);
   assert(used&&used<=8);assert(++calls<100000);
  }
  assert(coarse&&refine&&candidates>=150);
 }
 assert(checked>=900);
 printf("PASS packed correlation: %u exact candidate-energy checks, full-scale/random/stereo-imbalance/wrap and half-unit reference bounds\n",checked);
}
static void test_parked_plan_tail_boundary_and_reset(void)
{
 for(unsigned reset_stage=0;reset_stage<3;reset_stage++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
  bs_init(&state,0,true);assert(bs_request(&state,66846));prepare(64);
  assert(bs_activate(&state,NULL,NULL));
  struct bs_frame saved_tail[BS_STEMS][BS_OVERLAP];
  memset(state.tail,0xa5,sizeof(state.tail));memcpy(saved_tail,state.tail,sizeof(saved_tail));
  unsigned calls=0;
  while(!state.prepared) {
   assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024)>0);
   assert(++calls<100&&!memcmp(saved_tail,state.tail,sizeof(saved_tail)));
  }
  assert(!work.owner&&!atomic_load(&state.ready));
  atomic_store(&state.cursor,BS_OVERLAP-1);reads=0;
  assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,1024));
  assert(!reads&&!memcmp(saved_tail,state.tail,sizeof(saved_tail)));
  if(reset_stage==1) {
   /* The first safe cursor permits just one four-stem tail copy. Reset
    * must cancel this partial copy and never publish its old descriptor. */
   atomic_store(&state.cursor,BS_OVERLAP);
   assert(bs_service(&state,&work,source,NULL,gains,oldest,written,8)==8);
   assert(work.owner==&state&&work.index==1&&!atomic_load(&state.ready));
  } else if(reset_stage==2) {
   /* Reject a stale sequence even if its generation has not changed. */
   atomic_fetch_add(&state.sequence,2);
   assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,1024));
   assert(!state.prepared&&!work.owner&&!atomic_load(&state.ready));
  }
  uint32_t generation=atomic_load(&state.generation);
  bs_init(&state,4567,true);
  assert(atomic_load(&state.generation)!=generation&&!state.prepared);
  if(work.owner)assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024)==1);
  assert(!work.owner&&!atomic_load(&state.ready));
  for(unsigned k=0;k<BS_STEMS;k++)for(unsigned i=0;i<BS_OVERLAP;i++)
   assert(!state.tail[k][i].l&&!state.tail[k][i].r);
  prepare(64);assert(state.pending.nominal==4567);
  assert(state.pending.generation==atomic_load(&state.generation));
 }
}
static void test_tail_batch_budget_and_exclusive_reset(void)
{
 const uint32_t budgets[]={1,7,8,9,15,16,31,63,127,511,1023,1024};
 const uint32_t origins[]={0,UINT32_MAX-1000};
 for(unsigned origin=0;origin<2;origin++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=origins[origin];written=oldest+1000000;
  bs_init(&state,oldest,true);assert(bs_request(&state,66846));prepare(64);
  assert(bs_activate(&state,NULL,NULL));
  while(!state.prepared)assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024));
  atomic_store(&state.cursor,BS_OVERLAP);
  assert(!bs_service(&state,&work,source,NULL,gains,oldest,written,1));
  assert(work.owner&&work.index==0&&!atomic_load(&state.ready));
  for(unsigned prefix=0;prefix<3;prefix++) {
   if(prefix) {
    unsigned target=prefix==1?127:383;
    while(work.index<target)assert(bs_service(&state,&work,source,NULL,gains,oldest,written,8)==8);
   }
   struct bs_state initial_state=state,batched_state;
   struct bs_work initial_work=work,batched_work;
   for(unsigned b=0;b<sizeof(budgets)/sizeof(*budgets);b++) {
    state=initial_state;work=initial_work;
    uint32_t n=budgets[b],remaining=(BS_OVERLAP-work.index)*8;
    if(n>remaining)n=remaining;
    uint32_t used=bs_service(&state,&work,source,NULL,gains,oldest,written,n);
    assert(used==n/8*8&&!atomic_load(&state.ready));
    batched_state=state;batched_work=work;
    state=initial_state;work=initial_work;
    for(unsigned step=0;step<used/8;step++)assert(bs_service(&state,&work,source,NULL,gains,oldest,written,8)==8);
    assert(!memcmp(&state,&batched_state,sizeof(state)));
    assert(!memcmp(&work,&batched_work,sizeof(work)));
   }
   state=initial_state;work=initial_work;
  }
  /* Reset exclusively between slices, including an almost complete tail.
   * A stale continuation must never write into the fresh generation. */
  bs_init(&state,12345,true);
  assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024)==1);
  assert(!work.owner&&!atomic_load(&state.ready));
  for(unsigned k=0;k<BS_STEMS;k++)for(unsigned i=0;i<BS_OVERLAP;i++)
   assert(!state.tail[k][i].l&&!state.tail[k][i].r);
 }
}
static struct bs_frame source_with_audio_progress(void *context,unsigned stem,uint32_t p)
{
 struct bs_frame x=source(context,stem,p);
 assert(!atomic_load(&state.ready));
 if(atomic_load(&state.cursor)<BS_HOP-1)bs_advance(&state);
 if(atomic_load(&state.cursor)==BS_HOP-1)assert(!bs_buffered(&state,oldest,written));
 return x;
}
static void test_tail_batch_allows_audio_progress_but_not_activation(void)
{
 memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
 bs_init(&state,0,true);assert(bs_request(&state,66846));prepare(64);
 assert(bs_activate(&state,NULL,NULL));
 while(!state.prepared)assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1024));
 atomic_store(&state.cursor,BS_OVERLAP);
 uint32_t chosen=atomic_load(&state.chosen),sequence=atomic_load(&state.sequence);
 for(unsigned batch=0;batch<3;batch++) {
  assert(bs_service(&state,&work,source_with_audio_progress,NULL,gains,oldest,written,1024)==1024);
  assert(atomic_load(&state.chosen)==chosen&&atomic_load(&state.sequence)==sequence);
  assert(!atomic_load(&state.ready));
 }
 assert(work.index==BS_OVERLAP&&atomic_load(&state.cursor)==BS_HOP-1);
 assert(bs_service(&state,&work,source,NULL,gains,oldest,written,1)==1);
 assert(atomic_load(&state.ready)&&bs_buffered(&state,oldest,written));
 for(unsigned k=0;k<BS_STEMS;k++)for(unsigned i=0;i<BS_OVERLAP;i++) {
  struct bs_frame expected=source(NULL,k,chosen+BS_HOP+i);
  assert(!memcmp(&state.tail[k][i],&expected,sizeof(expected)));
 }
}
static void test_parked_transient_retains_selected_source(void)
{
 const uint32_t origins[]={10000u,UINT32_MAX-1000u};
 for(unsigned n=0;n<2;n++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));
  uint32_t nominal=origins[n],chosen=nominal-BS_SEARCH,next=nominal+BS_HOP*5u/4u;
  event_origin=next-BS_SEARCH;event_first=1;event_end=49;
  bs_init(&state,nominal-2000u,true);assert(bs_request(&state,81920));
  atomic_store(&state.active,true);atomic_store(&state.chosen,chosen);
  atomic_store(&state.nominal,nominal);atomic_store(&state.next_nominal,next);
  atomic_store(&state.cursor,0);oldest=bs_retain(&state);written=oldest+DD_RING;
  unsigned calls=0;
  while(!state.prepared) {
   uint32_t used=bs_service(&state,&work,boundary_event,NULL,gains,oldest,written,1024);
   assert(used&&used<=1024&&++calls<100);
  }
  assert(state.prepared_transient&&!work.owner&&!atomic_load(&state.ready));
  uint32_t selected=state.pending.chosen;
  for(unsigned i=0;i<=BS_OVERLAP;i++) {
   atomic_store(&state.cursor,i);uint32_t retained=bs_retain(&state);
   assert(selected-retained<0x80000000u);
  }
  /* Release old history before the deferred copy, as the real ring producer
   * can. The selected transient tail must still be entirely published. */
  oldest=bs_retain(&state);
  while(!atomic_load(&state.ready)) {
   uint32_t used=bs_service(&state,&work,boundary_event,NULL,gains,oldest,written,64);
   assert(used&&used<=64);
  }
  assert(state.pending.chosen==selected&&!state.prepared);
  for(unsigned k=0;k<BS_STEMS;k++)for(unsigned i=0;i<BS_OVERLAP;i++) {
   struct bs_frame expected=boundary_event(NULL,k,selected+i);
   assert(!memcmp(&state.tail[k][i],&expected,sizeof(expected)));
  }
 }
}
static unsigned onset_case;
static struct bs_frame impossible_onset_source(void *context,unsigned stem,uint32_t p)
{
 (void)source(context,stem,p);
 uint32_t i=p-event_origin;
 int16_t x=0;
 if(onset_case<2) {
  if(!i)x=onset_case?400:1000;
  if(i>=1800&&i<1848)x=30000;
 } else if(onset_case==2) {
  if(i>=50&&i<400)x=i==50?30000:7000;
 } else if(onset_case==3) {
  if(i>=50&&i<400)x=i==50?30000:1000;
 } else if(i==50||i==84)x=30000;
 return (struct bs_frame){x,0};
}
static void test_impossible_transients_skip_unnecessary_source_reads(void)
{
 const uint16_t full[4]={256,256,256,256};
 for(onset_case=0;onset_case<5;onset_case++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
  bs_init(&state,10000,true);assert(bs_request(&state,66846));
  assert(bs_service(&state,&work,impossible_onset_source,NULL,full,oldest,written,8)==1);
  assert(bs_activate(&state,NULL,NULL));atomic_store(&state.cursor,BS_OVERLAP);
  event_origin=atomic_load(&state.next_nominal)-BS_SEARCH;reads=0;
  unsigned turns=0;
  while(!atomic_load(&state.ready)) {
   uint32_t used=bs_service(&state,&work,impossible_onset_source,NULL,full,oldest,written,1024);
   assert(used&&used<=1024);assert(++turns<100);
  }
  /* Every pattern must choose the unchanged ordinary-search result. First
   * boundary1000 cannot be quiet even against the largest int16 peak, so it
   * avoids LOOKAHEAD too. Boundary400 needs the actual future peak before
   * rejection. Remaining patterns prove each monotone early failure. */
  assert(!work.transient&&state.pending.chosen==state.pending.nominal);
  uint32_t cached=BS_OVERLAP+2*BS_SEARCH;
  uint32_t scan=BS_SEARCH+BS_HOP+BS_OVERLAP;
  uint32_t expected=(cached+2*BS_OVERLAP)*BS_STEMS;
  if(onset_case)expected+=(scan-cached)*BS_STEMS;
  assert(reads==expected); /* The old full scan needlessly reread lookahead. */
  if(onset_case==2)assert(work.onset_count==257);
  if(onset_case==3)assert(work.nonquiet_count==289&&work.onset_count==1);
  if(onset_case==4)assert(work.onset_count==2&&work.onset_last-work.onset+1-work.onset_count==33);
 }
}
static unsigned reset_read;
static struct bs_frame reset_during_read(void *context,unsigned stem,uint32_t p)
{
 struct bs_frame frame=source(context,stem,p);
 if(reset_read&&!--reset_read)bs_init(&state,9876,true);
 return frame;
}
static void test_private_source_batches_cancel_before_publication(void)
{
 for(unsigned reference=0;reference<2;reference++) {
  memset(&state,0,sizeof(state));memset(&work,0,sizeof(work));oldest=0;written=1000000;
  bs_init(&state,0,true);assert(bs_request(&state,66846));prepare(64);
  assert(bs_activate(&state,NULL,NULL));atomic_store(&state.cursor,BS_OVERLAP);
  assert(bs_service(&state,&work,source,NULL,gains,oldest,written,8)==8);
  if(reference) {
   /* CACHE advances to REFERENCE at its index reset. Tiny calls expose the
    * real stage boundary without duplicating the private stage numbering. */
   unsigned calls=0;
   while(work.index) {assert(bs_service(&state,&work,source,NULL,gains,oldest,written,8)==8);assert(++calls<10000);}
   assert(!work.reference_power[0]&&!work.reference_power[1]);
  }
  uint32_t generation=atomic_load(&state.generation);reset_read=3;
  uint32_t used=bs_service(&state,&work,reset_during_read,NULL,gains,oldest,written,1024);
  assert(used&&used<=1024&&atomic_load(&state.generation)!=generation);
  /* Even an adversarial reset in a read callback can only leave old private
   * scratch. It must never publish or copy a tail into the new generation. */
  if(work.owner)assert(bs_cancel_stale(&work));
  assert(!work.owner&&!state.prepared&&!atomic_load(&state.ready));
  for(unsigned k=0;k<BS_STEMS;k++)for(unsigned i=0;i<BS_OVERLAP;i++)
   assert(!state.tail[k][i].l&&!state.tail[k][i].r);
  prepare(64);assert(state.pending.nominal==9876);
 }
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
 test_packed_reference_and_rolling_energy();
 test_parked_plan_tail_boundary_and_reset();
 test_tail_batch_budget_and_exclusive_reset();
 test_tail_batch_allows_audio_progress_but_not_activation();
 test_parked_transient_retains_selected_source();
 test_private_source_batches_cancel_before_publication();
 test_impossible_transients_skip_unnecessary_source_reads();
 printf("PASS budgeted stretch: raw per-stem convex oracle, exact1x, stereo, clocks, wrap, bounds, coalescing, reset generation, poisoned scratch and budget partition invariance; state=%zu/work=%zu/two=%zu\n",sizeof(state),sizeof(work),sizeof(state)*2+sizeof(work));
}
