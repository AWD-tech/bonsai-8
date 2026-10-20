/* SPDX-License-Identifier: MIT
 * Work-demand replay of production rendering/planning against local P14S stems.
 * No device access or audio export. Files are private inputs, never test assets.
 *
 * The finite budget is an explicit capacity scenario, not an MCU emulator.
 * Refills are deliberately free here: a miss even with this optimistic source
 * service rules out fixing that workload through refill scheduling alone at
 * the stated planner capacity. Actual CPU cost varies by worker stage.
 */
#include "dual_engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000u
#define BLOCK 256u
#define BURST (8u*140u)
static struct dd_engine engine;
static struct dd_frame *source[8], scratch[BURST];
static uint32_t source_length[8], start_frame[2];
static int16_t output[BLOCK*2];
static uint64_t work, calls, full_calls;
static uint32_t peak_frame_work, refill_calls, late_frames[2], grains[2];

static void load_file(unsigned n,const char *path)
{
 FILE *f=fopen(path,"rb");if(!f){perror(path);exit(2);}
 assert(!fseek(f,0,SEEK_END));long size=ftell(f);
 assert(size>0&&size%512==0);rewind(f);
 size_t frames=(size_t)(size/512)*140;
 assert(frames<=UINT32_MAX);
 source[n]=malloc(frames*sizeof(struct dd_frame));assert(source[n]);
 uint8_t block[512];struct dd_frame decoded[DD_MAX_DECODE];
 for(size_t p=0;p<frames;p+=140) {
  assert(fread(block,1,512,f)==512);assert(dd_decode(block,DD_P14S,decoded)==140);
  memcpy(source[n]+p,decoded,140*sizeof(struct dd_frame));
 }
 source_length[n]=(uint32_t)frames;fclose(f);
}
static bool refill(void)
{
 unsigned best=8;uint32_t largest=0;
 for(unsigned n=0;n<8;n++) {
  uint32_t room=dd_room(&engine.deck[n/4],n%4);
  if(room>=BURST&&room>largest){largest=room;best=n;}
 }
 if(best==8)return false;
 struct dd_deck *d=&engine.deck[best/4];
 uint32_t first=atomic_load(&d->voice[best%4].written);
 for(unsigned i=0;i<BURST;i++)
  scratch[i]=source[best][((uint64_t)start_frame[best/4]+first+i)%source_length[best]];
 assert(dd_push(d,best%4,scratch,BURST));refill_calls++;return true;
}
static void fill_all(void){while(refill()){}}
static void reset_counts(void)
{
 work=calls=full_calls=0;peak_frame_work=refill_calls=0;
 memset(late_frames,0,sizeof(late_frames));memset(grains,0,sizeof(grains));
 for(unsigned k=0;k<2;k++) {
  engine.deck[k].underruns=engine.deck[k].plan_underruns=engine.deck[k].source_underruns=0;
 }
}
int main(int argc,char **argv)
{
 if(argc!=15) {
  fprintf(stderr,"usage: %s seconds units_per_second ratio_a ratio_b start_a start_b eight-p14s-paths\n",argv[0]);
  return 2;
 }
 unsigned seconds=(unsigned)strtoul(argv[1],NULL,10);
 uint32_t capacity=(uint32_t)strtoul(argv[2],NULL,10);
 uint32_t ratios[2]={(uint32_t)strtoul(argv[3],NULL,10),(uint32_t)strtoul(argv[4],NULL,10)};
 start_frame[0]=(uint32_t)strtoul(argv[5],NULL,10);
 start_frame[1]=(uint32_t)strtoul(argv[6],NULL,10);
 assert(seconds&&seconds<=600);
 for(unsigned n=0;n<8;n++)load_file(n,argv[7+n]);
 dd_init(&engine);atomic_store(&engine.master,8);
 for(unsigned k=0;k<2;k++) {
  dd_reset_deck(&engine.deck[k],15);dd_set_stretch(&engine.deck[k],true);
  atomic_store(&engine.deck[k].playing,1);
  for(unsigned s=0;s<4;s++)atomic_store(&engine.deck[k].voice[s].gain,256);
 }
 /* Warm settled dry playback before applying the same rocker request. */
 for(unsigned n=0;n<RATE/BLOCK;n++){fill_all();dd_render(&engine,output,BLOCK);}
 for(unsigned k=0;k<2;k++)assert(dd_set_tempo(&engine.deck[k],ratios[k]));
 fill_all();reset_counts();
 uint32_t beginning[2]={atomic_load(&engine.deck[0].source_clock),atomic_load(&engine.deck[1].source_clock)};
 uint64_t fraction=0;
 for(unsigned frame=0;frame<seconds*RATE;frame+=BLOCK) {
  fill_all();
  /* No banking idle capacity into future deadlines. A scenario with zero
   * capacity means unlimited service, the former functional-test behavior. */
  fraction+=(uint64_t)capacity*BLOCK;
  uint32_t budget=capacity?(uint32_t)(fraction/RATE):65536;
  fraction%=RATE;uint32_t frame_work=0;
  while(budget) {
   unsigned slice=budget>1024?1024:budget;
   uint32_t used=dd_stretch_service(&engine,slice);
   calls++;if(used>=slice-8)full_calls++;
   assert(used<=slice);work+=used;frame_work+=used;
   if(!used)break;
   budget-=used;
   if(used<8)break;
  }
  if(frame_work>peak_frame_work)peak_frame_work=frame_work;
  uint32_t sequence[2]={atomic_load(&engine.deck[0].stretch.sequence),atomic_load(&engine.deck[1].stretch.sequence)};
  dd_render(&engine,output,BLOCK);
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&engine.deck[k];
   if(d->starved)late_frames[k]++;
   grains[k]+=(atomic_load(&d->stretch.sequence)-sequence[k])/2;
  }
 }
 printf("{\"seconds\":%u,\"capacity_units_per_second\":%u,\"ratios\":[%u,%u],"
  "\"charged_work\":%llu,\"work_per_second\":%.3f,\"calls\":%llu,\"full_calls\":%llu,"
  "\"peak_work_per_callback\":%u,\"free_refills\":%u,"
  "\"plan_underruns\":[%u,%u],\"source_underruns\":[%u,%u],"
  "\"starved_callbacks\":[%u,%u],\"grains\":[%u,%u],"
  "\"source_advance\":[%u,%u],\"clips\":%u}\n",
  seconds,capacity,ratios[0],ratios[1],(unsigned long long)work,(double)work/seconds,
  (unsigned long long)calls,(unsigned long long)full_calls,peak_frame_work,refill_calls,
  engine.deck[0].plan_underruns,engine.deck[1].plan_underruns,
  engine.deck[0].source_underruns,engine.deck[1].source_underruns,
  late_frames[0],late_frames[1],grains[0],grains[1],
  atomic_load(&engine.deck[0].source_clock)-beginning[0],
  atomic_load(&engine.deck[1].source_clock)-beginning[1],engine.clips);
 bool failed=engine.deck[0].underruns||engine.deck[1].underruns;
 for(unsigned n=0;n<8;n++)free(source[n]);
 return failed?1:0;
}
