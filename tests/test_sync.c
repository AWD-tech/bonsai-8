/* SPDX-License-Identifier: MIT */
#include "bonsai_sync.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct bonsai_sync make(void) {
 struct bonsai_sync s;assert(bonsai_sync_init(&s,32768u,81920u));
 assert(bonsai_sync_song(&s,0,1,1200000));assert(bonsai_sync_song(&s,1,2,1200000));return s;
}
static void put(struct bonsai_sync *s,unsigned k,uint32_t source,uint32_t loop,
 uint32_t output,uint32_t tempo,bool play) {
 struct bonsai_sync_position p={s->deck[k].song_epoch,source,loop,output,tempo,play};
 assert(bonsai_sync_observe(s,k,&p)==BONSAI_SYNC_OK);
}
static void grids(struct bonsai_sync *s,uint32_t a,uint32_t b) {
 assert(bonsai_sync_set_grid(s,0,1,a,0));assert(bonsai_sync_set_grid(s,1,2,b,0));
}
static void four_taps(void) {
 struct bonsai_sync s=make();assert(bonsai_sync_song(&s,0,3,30000));
 for(unsigned i=0;i<4;i++) {
  uint32_t source=3600+i*12000;put(&s,0,source,source%30000,i*24000,65536,true);
  assert(bonsai_sync_tap(&s,0)==(i<3?BONSAI_SYNC_PENDING:BONSAI_SYNC_OK));
 }
 assert(s.deck[0].grid_valid&&s.deck[0].bpm_milli==120000&&s.deck[0].beat_native==3600);
 /* Additional taps refine a bounded three-interval window, not an unbounded allocation. */
 for(unsigned i=4;i<20;i++) {
  uint32_t source=3600+i*12000;put(&s,0,source,source%30000,i*24000,65536,true);
  assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_OK);
 }
 assert(s.deck[0].tap_count==4&&s.deck[0].bpm_milli==120000);
}
static void tapped_source_not_wall_tempo(void) {
 for(uint32_t ratio=32768;ratio<=81920;ratio+=16384) {
  struct bonsai_sync s=make();
  for(unsigned i=0;i<4;i++) {
   uint32_t output=(uint32_t)((uint64_t)i*24000*65536/ratio);
   put(&s,0,i*12000,i*12000,output,ratio,true);
   assert(bonsai_sync_tap(&s,0)==(i<3?BONSAI_SYNC_PENDING:BONSAI_SYNC_OK));
  }
  assert(s.deck[0].bpm_milli==120000);
 }
}
static void counter_wrap(void) {
 struct bonsai_sync s=make();uint32_t start=0xffffff00u,clock=0xfffff000u;
 for(unsigned i=0;i<4;i++) {
  put(&s,0,start+i*12000,(1000+i*12000)%1200000,clock+i*24000,65536,true);
  assert(bonsai_sync_tap(&s,0)==(i<3?BONSAI_SYNC_PENDING:BONSAI_SYNC_OK));
 }
 assert(s.deck[0].bpm_milli==120000&&s.deck[0].beat_native==1000);
 grids(&s,120000,150000);
 put(&s,1,0xffff0000u,1250,clock+72000,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_OK);
 struct bonsai_sync_request r=bonsai_sync_step(&s);
 assert(r.apply&&r.tempo_q16>=32768&&r.tempo_q16<=81920);
}
static void bad_taps_and_pause(void) {
 struct bonsai_sync s=make();put(&s,0,0,0,0,65536,false);
 assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_NOT_PLAYING);
 put(&s,0,0,0,0,65536,true);assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_PENDING);
 assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_BAD_TAP);assert(!s.deck[0].grid_valid);
 put(&s,0,12000,12000,24000,65536,true);assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_PENDING);
 put(&s,0,12000,12000,48000,65536,false);assert(s.deck[0].tap_count==0);
 put(&s,0,24000,24000,72000,65536,true);assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_PENDING);
 put(&s,0,36000,36000,120000,32768,true);assert(s.deck[0].tap_count==0);
 assert(bonsai_sync_tap(&s,0)==BONSAI_SYNC_PENDING);
}
static void grid_limits_and_stale_song(void) {
 struct bonsai_sync s=make();grids(&s,120000,120000);
 struct bonsai_sync_deck other=s.deck[1];
 assert(!bonsai_sync_set_grid(&s,0,2,120000,0));assert(!bonsai_sync_set_grid(&s,0,1,19999,0));
 assert(!bonsai_sync_set_grid(&s,0,1,300001,0));assert(!bonsai_sync_set_grid(&s,0,1,120000,1200000));
 assert(bonsai_sync_song(&s,0,4,1200000));assert(!s.deck[0].grid_valid);
 assert(!memcmp(&other,&s.deck[1],sizeof(other)));
 struct bonsai_sync_position old={1,0,0,0,65536,true};
 assert(bonsai_sync_observe(&s,0,&old)==BONSAI_SYNC_STALE_SONG);
 assert(!s.deck[0].observed);
 assert(bonsai_sync_set_grid(&s,0,4,20000,1199999));
 assert(bonsai_sync_set_grid(&s,0,4,300000,0));
}
static void unknown_incoherent_and_paused_refuse(void) {
 struct bonsai_sync s=make();put(&s,0,0,0,0,65536,true);put(&s,1,0,0,0,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_UNKNOWN_GRID);assert(!bonsai_sync_step(&s).apply);
 grids(&s,120000,120000);put(&s,1,0,0,1,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_INCOHERENT);
 put(&s,0,0,0,1,65536,true);put(&s,1,0,0,1,65536,false);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_NOT_PLAYING);
}
static void ratios_never_clamp_as_success(void) {
 struct bonsai_sync s=make();grids(&s,300000,20000);
 put(&s,0,0,0,0,65536,true);put(&s,1,0,0,0,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_RANGE);assert(!bonsai_sync_step(&s).apply);
 grids(&s,250000,200000);put(&s,0,1152,1152,2304,65536,true);put(&s,1,0,0,2304,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_RANGE);
 grids(&s,120000,150000);put(&s,0,12000,12000,24000,49152,true);put(&s,1,9600,9600,24000,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_OK);
 struct bonsai_sync_request r=bonsai_sync_step(&s);
 assert(r.apply&&r.mode==BONSAI_SYNC_LOCKED&&r.tempo_q16==39322); /* .75*120/150=.6 */
}
static void phase_servo_and_nudge(void) {
 struct bonsai_sync s=make();grids(&s,120000,120000);
 uint64_t a=(uint64_t)4800<<16,b=0;uint32_t output=0,rate=65536;bool locked=false;
 put(&s,0,4800,4800,0,65536,true);put(&s,1,0,0,0,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_OK);
 for(unsigned i=0;i<1875;i++) { /* 20 seconds of nominal audible progress */
  put(&s,0,(uint32_t)(a>>16),(uint32_t)(a>>16)%1200000,output,65536,true);
  put(&s,1,(uint32_t)(b>>16),(uint32_t)(b>>16)%1200000,output,rate,true);
  struct bonsai_sync_request r=bonsai_sync_step(&s);assert(r.apply);
  assert(r.tempo_q16>=65536-65536/20&&r.tempo_q16<=65536+65536/20);
  if(r.mode==BONSAI_SYNC_LOCKED)locked=true;
  rate=r.tempo_q16;a+=(uint64_t)256*65536;b+=(uint64_t)256*rate;output+=512;
 }
 assert(locked&&s.deck[1].mode==BONSAI_SYNC_LOCKED);
 uint32_t before=s.deck[1].position.source_frame;
 assert(bonsai_sync_nudge(&s,1,1)==BONSAI_SYNC_OK);
 struct bonsai_sync_request r=bonsai_sync_step(&s);assert(r.apply&&r.mode==BONSAI_SYNC_ALIGNING);
 assert(r.tempo_q16>65536);assert(s.deck[1].position.source_frame==before); /* no seek */
 assert(bonsai_sync_nudge(&s,1,-1)==BONSAI_SYNC_OK);
 assert(bonsai_sync_step(&s).mode==BONSAI_SYNC_LOCKED);
 bonsai_sync_manual(&s,0);assert(s.follower==1); /* master may be manually adjusted */
 bonsai_sync_manual(&s,1);assert(!bonsai_sync_step(&s).apply&&s.follower==-1);
 assert(bonsai_sync_nudge(&s,1,1)==BONSAI_SYNC_MANUAL);
}
static void loop_phase_and_negative_anchor(void) {
 struct bonsai_sync s=make();assert(bonsai_sync_song(&s,0,1,35000));assert(bonsai_sync_song(&s,1,2,35000));
 assert(bonsai_sync_set_grid(&s,0,1,120000,10000));assert(bonsai_sync_set_grid(&s,1,2,120000,10000));
 put(&s,0,34000,34000,0,65536,true);put(&s,1,34000,34000,0,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_OK);assert(bonsai_sync_step(&s).mode==BONSAI_SYNC_LOCKED);
 put(&s,0,36000,1000,4000,65536,true);put(&s,1,36000,1000,4000,65536,true);
 assert(bonsai_sync_step(&s).mode==BONSAI_SYNC_LOCKED);
 struct bonsai_sync_position jump={2,36000,2000,4000,65536,true};
 assert(bonsai_sync_observe(&s,1,&jump)==BONSAI_SYNC_INCOHERENT);
 assert(!bonsai_sync_step(&s).apply&&s.deck[1].grid_valid); /* seek cancels follow, grid unchanged */
}
static void load_invalidates_follow_and_taps(void) {
 struct bonsai_sync s=make();grids(&s,120000,120000);
 put(&s,0,0,0,0,65536,true);put(&s,1,0,0,0,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_OK);
 assert(bonsai_sync_song(&s,0,5,1200000));assert(!s.deck[0].grid_valid&&s.deck[1].grid_valid);
 assert(!bonsai_sync_step(&s).apply);
}
static void invalid_snapshot_cannot_keep_applying_stale_cursor(void) {
 struct bonsai_sync s=make();grids(&s,120000,120000);
 put(&s,0,0,0,0,65536,true);put(&s,1,0,0,0,65536,true);
 assert(bonsai_sync_follow(&s,1)==BONSAI_SYNC_OK);
 struct bonsai_sync_position bad={2,0,1200000,0,65536,true};
 assert(bonsai_sync_observe(&s,1,&bad)==BONSAI_SYNC_INVALID);
 assert(!bonsai_sync_step(&s).apply);
}
int main(void) {
 four_taps();tapped_source_not_wall_tempo();counter_wrap();bad_taps_and_pause();
 grid_limits_and_stale_song();unknown_incoherent_and_paused_refuse();ratios_never_clamp_as_success();
 phase_servo_and_nudge();loop_phase_and_negative_anchor();load_invalidates_follow_and_taps();
 invalid_snapshot_cannot_keep_applying_stale_cursor();
 printf("11 live-sync scenarios passed; state=%zu bytes, request=%zu bytes\n",sizeof(struct bonsai_sync),sizeof(struct bonsai_sync_request));return 0;
}
