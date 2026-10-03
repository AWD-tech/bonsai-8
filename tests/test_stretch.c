/* SPDX-License-Identifier: MIT */
#include "bonsai_stretch.h"
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
int main(void)
{
 const uint32_t rates[]={32768,49152,65536,73728,81920,50001};
 for(unsigned r=0;r<6;r++)run(rates[r]);reset_and_coalesce();missing_and_wrap();
 printf("PASS budgeted stretch: raw per-stem convex oracle, exact1x, stereo, clocks, wrap, bounds, coalescing, reset generation; state=%zu/work=%zu/two=%zu\n",sizeof(state),sizeof(work),sizeof(state)*2+sizeof(work));
}
