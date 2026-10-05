/* SPDX-License-Identifier: MIT */
#include "bonsai_sync.h"
#include <string.h>
#define BEAT_DENOM 1440000000ll /* native_rate * 60 * 1000 */
#define Q32 4294967296ll

static void taps_clear(struct bonsai_sync_deck *d) { d->tap_count=0; }
static void unlock(struct bonsai_sync *s) {
 s->follower=-1;s->phase_offset_q32=0;
 for(unsigned k=0;k<2;k++)s->deck[k].mode=BONSAI_SYNC_MODE_MANUAL;
}
bool bonsai_sync_init(struct bonsai_sync *s,uint32_t lo,uint32_t hi) {
 if(!s||!lo||lo>hi||hi>4u*BONSAI_SYNC_UNITY)return false;
 memset(s,0,sizeof(*s));s->follower=-1;s->min_tempo_q16=lo;s->max_tempo_q16=hi;return true;
}
bool bonsai_sync_song(struct bonsai_sync *s,unsigned k,uint32_t epoch,uint32_t frames) {
 if(!s||k>1||!epoch||frames>BONSAI_SYNC_MAX_FRAMES)return false;
 struct bonsai_sync_deck *d=&s->deck[k];
 if(d->song_epoch==epoch&&d->loop_frames==frames)return true;
 if(s->follower>=0)unlock(s);
 memset(d,0,sizeof(*d));d->song_epoch=epoch;d->loop_frames=frames;return true;
}
enum bonsai_sync_result bonsai_sync_observe(struct bonsai_sync *s,unsigned k,
 const struct bonsai_sync_position *p) {
 if(!s||k>1||!p)return BONSAI_SYNC_INVALID;
 struct bonsai_sync_deck *d=&s->deck[k];
 if(!d->song_epoch||p->song_epoch!=d->song_epoch)return BONSAI_SYNC_STALE_SONG;
 if(p->loop_frame>=d->loop_frames||p->tempo_q16<s->min_tempo_q16||p->tempo_q16>s->max_tempo_q16) {
  /* A malformed CURRENT load snapshot must not reuse a cached older cursor.
   * Stale-epoch observations above do not disturb a newer load's state. */
  d->observed=false;taps_clear(d);d->mode=BONSAI_SYNC_ERROR;
  return BONSAI_SYNC_INVALID;
 }
 bool jump=false;
 if(d->observed) {
  uint32_t advance=p->source_frame-d->position.source_frame;
  uint32_t elapsed=p->output_frame-d->position.output_frame;
  jump=advance>0x7fffffffu||elapsed>0x7fffffffu||
       ((uint64_t)d->position.loop_frame+advance)%d->loop_frames!=p->loop_frame;
  if(jump||p->tempo_q16!=d->position.tempo_q16||!p->playing)taps_clear(d);
 }
 d->position=*p;d->observed=true;
 if(jump&&s->follower>=0){unlock(s);d->mode=BONSAI_SYNC_ERROR;return BONSAI_SYNC_INCOHERENT;}
 return BONSAI_SYNC_OK;
}
bool bonsai_sync_set_grid(struct bonsai_sync *s,unsigned k,uint32_t epoch,uint32_t bpm,uint32_t beat) {
 if(!s||k>1)return false;
 struct bonsai_sync_deck *d=&s->deck[k];
 if(!epoch||epoch!=d->song_epoch||bpm<20000u||bpm>300000u||beat>=d->loop_frames)return false;
 if(s->follower>=0)unlock(s);
 d->bpm_milli=bpm;d->beat_native=beat;d->grid_valid=true;taps_clear(d);return true;
}
static enum bonsai_sync_result tap_first(struct bonsai_sync *s,unsigned k,enum bonsai_sync_result result) {
 struct bonsai_sync_deck *d=&s->deck[k];
 if(s->follower>=0)unlock(s);
 d->grid_valid=false;d->tap_count=1;d->beat_native=d->position.loop_frame;
 d->tap_source=d->position.source_frame;d->tap_output=d->position.output_frame;return result;
}
enum bonsai_sync_result bonsai_sync_tap(struct bonsai_sync *s,unsigned k) {
 if(!s||k>1)return BONSAI_SYNC_INVALID;
 struct bonsai_sync_deck *d=&s->deck[k];
 if(!d->observed||!d->position.playing){taps_clear(d);return BONSAI_SYNC_NOT_PLAYING;}
 if(!d->tap_count)return tap_first(s,k,BONSAI_SYNC_PENDING);
 uint32_t native=d->position.source_frame-d->tap_source;
 uint32_t elapsed=d->position.output_frame-d->tap_output;
 /* Broad individual bounds allow tap jitter; final source BPM has strict
  * 20..300 bounds. A long pause/duplicate/seek never becomes a beat interval. */
 if(native<2400u||native>144000u||!elapsed||elapsed>1440000u)
  return tap_first(s,k,BONSAI_SYNC_BAD_TAP);
 if(d->tap_count<4)d->tap_intervals[d->tap_count++-1]=native;
 else {d->tap_intervals[0]=d->tap_intervals[1];d->tap_intervals[1]=d->tap_intervals[2];d->tap_intervals[2]=native;}
 d->tap_source=d->position.source_frame;d->tap_output=d->position.output_frame;
 if(d->tap_count<4)return BONSAI_SYNC_PENDING;
 uint64_t total=(uint64_t)d->tap_intervals[0]+d->tap_intervals[1]+d->tap_intervals[2];
 uint32_t bpm=(uint32_t)((3u*BEAT_DENOM+total/2)/total);
 if(bpm<20000u||bpm>300000u)return tap_first(s,k,BONSAI_SYNC_BAD_TAP);
 d->bpm_milli=bpm;d->grid_valid=true;return BONSAI_SYNC_OK;
}
static uint32_t phase(const struct bonsai_sync_deck *d) {
 int64_t n=((int64_t)d->position.loop_frame-d->beat_native)*d->bpm_milli;
 n%=BEAT_DENOM;if(n<0)n+=BEAT_DENOM;
 return (uint32_t)(((uint64_t)n<<32)/(uint64_t)BEAT_DENOM);
}
struct bonsai_sync_request bonsai_sync_step(struct bonsai_sync *s) {
 struct bonsai_sync_request r={.result=BONSAI_SYNC_MANUAL,.mode=BONSAI_SYNC_MODE_MANUAL};
 if(!s){r.result=BONSAI_SYNC_INVALID;return r;}
 if(s->follower<0)return r;
 unsigned k=(unsigned)s->follower;r.deck=(uint8_t)k;
 struct bonsai_sync_deck *f=&s->deck[k],*m=&s->deck[k^1u];
 if(!f->grid_valid||!m->grid_valid)r.result=BONSAI_SYNC_UNKNOWN_GRID;
 else if(!f->observed||!m->observed||!f->position.playing||!m->position.playing)r.result=BONSAI_SYNC_NOT_PLAYING;
 else if(f->position.output_frame!=m->position.output_frame)r.result=BONSAI_SYNC_INCOHERENT;
 else {
  uint64_t base=((uint64_t)m->bpm_milli*m->position.tempo_q16+f->bpm_milli/2)/f->bpm_milli;
  uint32_t delta=phase(m)+s->phase_offset_q32-phase(f);
  int64_t error=delta<0x80000000u?(int64_t)delta:(int64_t)delta-Q32;
  r.phase_error_q32=(int32_t)error;
  if(base<s->min_tempo_q16||base>s->max_tempo_q16)r.result=BONSAI_SYNC_RANGE;
  else {
   uint64_t magnitude=(uint64_t)(error<0?-error:error);
   bool locked=magnitude<=BONSAI_SYNC_LOCK_Q32;
   int64_t correction=locked?0:(int64_t)base*error/(4ll*Q32);
   int64_t maximum=(int64_t)base/20; /* <=5% relative correction */
   if(correction>maximum)correction=maximum;
   if(correction<-maximum)correction=-maximum;
   int64_t rate=(int64_t)base+correction;
   if(rate<s->min_tempo_q16||rate>s->max_tempo_q16)r.result=BONSAI_SYNC_RANGE;
   else {r.tempo_q16=(uint32_t)rate;r.apply=true;r.result=BONSAI_SYNC_OK;
    r.mode=locked?BONSAI_SYNC_LOCKED:BONSAI_SYNC_ALIGNING;f->mode=r.mode;return r;}
  }
 }
 r.mode=BONSAI_SYNC_ERROR;f->mode=r.mode;return r;
}
enum bonsai_sync_result bonsai_sync_follow(struct bonsai_sync *s,unsigned k) {
 if(!s||k>1)return BONSAI_SYNC_INVALID;
 unlock(s);s->follower=(int8_t)k;
 struct bonsai_sync_request r=bonsai_sync_step(s);
 if(!r.apply){s->follower=-1;return r.result;}
 return BONSAI_SYNC_OK;
}
enum bonsai_sync_result bonsai_sync_nudge(struct bonsai_sync *s,unsigned k,int direction) {
 if(!s||k>1||(direction!=-1&&direction!=1))return BONSAI_SYNC_INVALID;
 if(s->follower!=(int8_t)k)return BONSAI_SYNC_MANUAL;
 if(!s->deck[k].grid_valid||!s->deck[k^1u].grid_valid)return BONSAI_SYNC_UNKNOWN_GRID;
 uint32_t old=s->phase_offset_q32;
 s->phase_offset_q32+=direction>0?BONSAI_SYNC_NUDGE_Q32:0u-BONSAI_SYNC_NUDGE_Q32;
 struct bonsai_sync_request r=bonsai_sync_step(s);
 if(!r.apply){s->phase_offset_q32=old;return r.result;}
 return BONSAI_SYNC_OK;
}
void bonsai_sync_manual(struct bonsai_sync *s,unsigned k) {
 if(!s||k>1)return;
 if(s->follower==(int8_t)k)unlock(s);
 s->deck[k].mode=BONSAI_SYNC_MODE_MANUAL;taps_clear(&s->deck[k]);
}
