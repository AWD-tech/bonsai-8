/* SPDX-License-Identifier: MIT
 * Dual-deck adapter for Tape Looper's proven SP-1 hardware bring-up.
 * All eMMC IO belongs to this storage thread. Playback never writes the card.
 */
BUILD_ASSERT(sizeof(struct meta_blk)==1024, "uploader metadata layout changed");
BUILD_ASSERT(sizeof(struct x3_tab)==1040, "uploader extended table layout changed");
BUILD_ASSERT(sizeof(struct x3_trk)==16, "uploader track record layout changed");
#include <stdio.h>
#include "dual_mirror.h"
#include "bonsai_record.h"
#include "bonsai_library.h"
#include "bonsai_sync.h"
static struct dd_engine dd;
static struct bonsai_record dd_rec;
/* 0 idle, 1 recording, 2 draining, 3 waiting for pause/save, 4 saved, 5 failed. */
static _Atomic unsigned dd_record_state,dd_record_error;
static _Atomic int dd_record_slot;
static _Atomic bool dd_record_toggle,dd_record_saving;
static uint32_t dd_record_hash;
static bool dual_record_busy(void) {
 unsigned state=atomic_load(&dd_record_state);return state>=1&&state<=3;
}

static struct dd_ui_queue dd_ui;
static uint32_t dd_ui_until;
static uint16_t dd_physical[4];
static uint16_t dd_button_mask;
static char dd_ui_line[2048];
static void dual_mirror_capture(uint32_t now)
{
 uint16_t value[13]={dd_button_mask};
 for(unsigned i=0;i<4;i++) {
  value[1+i]=dd_physical[i];
  value[5+i]=MIN(g_led_trk_duty[i]&0x7fffu,1000u);
  value[9+i]=MIN(g_led_sta_duty[i]&0x7fffu,1000u);
 }
 unsigned key=irq_lock();
 dd_ui_push(&dd_ui,now,value,(int32_t)(dd_ui_until-now)>0);
 irq_unlock(key);
}
static void dual_mirror_listen(void)
{
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 if((int32_t)(dd_ui_until-now)<=0)dd_ui_start(&dd_ui);
 dd_ui_until=now+1000u;irq_unlock(key);
}
static void dual_mirror_reply(void)
{
 BUILD_ASSERT(BONSAI_CDC_REPLY_MAX>=16u*104u+96u,"maximum mirror rows must fit one reserved reply");
 if(bonsai_cdc_room(&g_cdc_tx_queue)<BONSAI_CDC_REPLY_MAX)return;
 dual_mirror_listen();
 uint32_t now=k_uptime_get_32();unsigned key=irq_lock();
 uint32_t lost=dd_ui.lost;irq_unlock(key);
 int used=snprintf(dd_ui_line,sizeof(dd_ui_line),"{\"mirror\":1,\"now\":%u,\"lost\":%u,\"frames\":[",now,lost);
 for(unsigned i=0;i<16;i++) {
  struct dd_ui_frame f;key=irq_lock();bool have=dd_ui_pop(&dd_ui,&f);irq_unlock(key);
  if(!have) break;
  if(i) dd_ui_line[used++]=',';
  int n=dd_ui_format(dd_ui_line+used,sizeof(dd_ui_line)-(size_t)used,&f);
  if(n<0||(size_t)n>=sizeof(dd_ui_line)-(size_t)used-3) return;
  used+=n;
 }
 memcpy(dd_ui_line+used,"]}\n",3);used+=3;
 cdc_tx((const uint8_t *)dd_ui_line,(size_t)used);
}
struct dd_source {
 uint32_t base, length, content, cursor, blocks;
 uint16_t spb;
 uint8_t codec;
};
static struct dd_source dd_src[2][4];
static _Atomic int dd_request[2], dd_slot[2];
/* Latest song choice survives the storage thread consuming dd_request before
 * the fade/read/load has published dd_slot. Otherwise a second wheel gesture
 * during that interval chooses the same next song again. */
static _Atomic int dd_browse_slot[2];
static _Atomic uint32_t dd_position[2],dd_length[2];
/* Controls are the sole sync state owner. Storage/USB read atomic copies. */
static struct bonsai_sync dd_sync;
static _Atomic uint32_t dd_sync_epoch[2],dd_sync_bpm[2],dd_sync_valid[2];
static _Atomic uint32_t dd_sync_state[2],dd_sync_result[2],dd_sync_taps[2];
static uint32_t dd_sync_feedback_until;
static unsigned dd_sync_feedback_deck;
static _Atomic uint32_t dd_storage_ok, dd_errors, dd_flush_req;
static uint32_t dd_capacity, dd_read_errors, dd_bad_blocks;
static uint8_t dd_selected;
#define DD_READ_BLOCKS 8u
/* The streaming DMA appends a two-byte CRC tail after the final sector. */
static uint8_t dd_io[DD_READ_BLOCKS*EMMC_BLOCK_SIZE+2] __aligned(4);
BUILD_ASSERT(sizeof(dd_io)>=DD_READ_BLOCKS*EMMC_BLOCK_SIZE+2,"streaming DMA tail needs space");
static uint8_t dd_verify[4096] __aligned(4);
static struct dd_frame dd_decoded[DD_MAX_DECODE];
static struct dd_pickup dd_pick[4];
static _Atomic bool dd_halted;

static void dual_sync_publish(void)
{
 k_sched_lock();
 for(unsigned k=0;k<2;k++) {
  atomic_store(&dd_sync_bpm[k],dd_sync.deck[k].grid_valid?dd_sync.deck[k].bpm_milli:0);
  atomic_store(&dd_sync_valid[k],dd_sync.deck[k].grid_valid);
  atomic_store(&dd_sync_state[k],dd_sync.deck[k].mode);
  atomic_store(&dd_sync_taps[k],dd_sync.deck[k].tap_count);
 }
 k_sched_unlock();
}
static void dual_sync_feedback(unsigned deck,enum bonsai_sync_result result)
{
 atomic_store(&dd_sync_result[deck],result);
 dd_sync_feedback_deck=deck;dd_sync_feedback_until=k_uptime_get_32()+1200u;
 dual_sync_publish();
}
static void dual_sync_poll(void)
{
 struct bonsai_sync_position p[2];uint32_t lengths[2];
 /* Snapshot nominal AUDIBLE source clocks, never ring reclaim/grain picks.
  * Both are paired to one 48k output clock under a short scheduling lock. */
 k_sched_lock();uint32_t output=(uint32_t)g_sample_clock;
 for(unsigned k=0;k<2;k++) {
  lengths[k]=atomic_load(&dd_length[k]);
  p[k]=(struct bonsai_sync_position){
   .song_epoch=atomic_load(&dd_sync_epoch[k]),
   .source_frame=atomic_load(&dd.deck[k].source_clock),
   .loop_frame=atomic_load(&dd_position[k]),.output_frame=output,
   .tempo_q16=dd_tempo_actual(&dd.deck[k]),
   .playing=atomic_load(&dd.deck[k].playing)&&!dd.deck[k].starved&&
    atomic_load(&dd_storage_ok)&&!g_xfer_mode&&!atomic_load(&dd_record_saving)&&!dd_halted
  };
 }
 k_sched_unlock();
 for(unsigned k=0;k<2;k++) {
  /* Unsupported on-card length must clear an older grid, not leave its
   * same-slot identity available because the strict setter rejected it. */
  uint32_t admitted=lengths[k]<=BONSAI_SYNC_MAX_FRAMES?lengths[k]:0;
  (void)bonsai_sync_song(&dd_sync,k,p[k].song_epoch,admitted);
  if(admitted) (void)bonsai_sync_observe(&dd_sync,k,&p[k]);
 }
 if(dd_sync.follower>=0) {
  unsigned selected=(unsigned)dd_sync.follower;
  enum bonsai_sync_mode before=dd_sync.deck[selected].mode;
  struct bonsai_sync_request r=bonsai_sync_step(&dd_sync);
  if(r.apply&&!dd_set_tempo(&dd.deck[selected],r.tempo_q16)) {
   r.result=BONSAI_SYNC_RANGE;dd_sync.deck[selected].mode=BONSAI_SYNC_ERROR;
  }
  atomic_store(&dd_sync_result[selected],r.result);
  if(before!=dd_sync.deck[selected].mode&&
     (dd_sync.deck[selected].mode==BONSAI_SYNC_LOCKED||dd_sync.deck[selected].mode==BONSAI_SYNC_ERROR))
   dual_sync_feedback(selected,r.result);
 }
 dual_sync_publish();
}
static void dual_sync_tap(unsigned selected)
{
 dual_sync_feedback(selected,bonsai_sync_tap(&dd_sync,selected));
}
static void dual_sync_follow(unsigned selected)
{
 enum bonsai_sync_result result=bonsai_sync_follow(&dd_sync,selected);
 dual_sync_feedback(selected,result);dual_sync_poll();
}
static void dual_sync_nudge(unsigned selected,int dir)
{
 dual_sync_feedback(selected,bonsai_sync_nudge(&dd_sync,selected,dir));
 dual_sync_poll();
}
static void dual_sync_manual_tempo(unsigned selected,int dir)
{
 bonsai_sync_manual(&dd_sync,selected);
 int tempo=(int)dd_tempo_actual(&dd.deck[selected])+dir*655;
 (void)dd_set_tempo(&dd.deck[selected],(uint32_t)CLAMP(tempo,32768,81920));
 atomic_store(&dd_sync_result[selected],BONSAI_SYNC_MANUAL);dual_sync_publish();
}
static bool dual_sync_leds(uint32_t now)
{
 if(dd_sync_feedback_deck!=dd_selected||(int32_t)(dd_sync_feedback_until-now)<=0)return false;
 unsigned result=atomic_load(&dd_sync_result[dd_selected]);
 if(result==BONSAI_SYNC_MANUAL)return false; /* explicit by-ear tempo is not an error */
 enum bonsai_sync_mode mode=dd_sync.deck[dd_selected].mode;
 all_off();status_level(255);
 if(result==BONSAI_SYNC_PENDING) {
  unsigned count=dd_sync.deck[dd_selected].tap_count;
  for(unsigned i=0;i<count&&i<4;i++)led_on((int)i);
  if(count<4&&((now/150u)&1u))led_on((int)count);
 } else if(result!=BONSAI_SYNC_OK) {
  if((now/150u)&1u){led_on(0);led_on(3);}else{led_on(1);led_on(2);}
 } else if(mode==BONSAI_SYNC_LOCKED) {
  for(int i=0;i<4;i++)led_on(i);
 } else if(mode==BONSAI_SYNC_ALIGNING) {
  if((now/200u)&1u){led_on(1);led_on(2);}
 } else led_on(0); /* fourth tap accepted: taught grid, no lock claim */
 return true;
}

static void dual_request_song(unsigned deck,int slot)
{
 atomic_store(&dd_browse_slot[deck],slot);
 atomic_store(&dd_request[deck],slot);
}

static void dual_init(void)
{
 dd_init(&dd);bonsai_record_init(&dd_rec);atomic_init(&dd_record_slot,-1);
 (void)bonsai_sync_init(&dd_sync,32768,81920);
 for(unsigned k=0;k<2;k++) {
  atomic_init(&dd_sync_epoch[k],1);(void)bonsai_sync_song(&dd_sync,k,1,0);
  atomic_init(&dd_sync_bpm[k],0);atomic_init(&dd_sync_valid[k],0);
  atomic_init(&dd_sync_state[k],BONSAI_SYNC_MODE_MANUAL);
  atomic_init(&dd_sync_result[k],BONSAI_SYNC_MANUAL);atomic_init(&dd_sync_taps[k],0);
 }
 for(int k=0;k<2;k++) {atomic_init(&dd_request[k],-1);atomic_init(&dd_slot[k],-1);atomic_init(&dd_browse_slot[k],-1);}
 atomic_init(&dd_storage_ok,0);atomic_init(&dd_errors,0);atomic_init(&dd_flush_req,0);
 for(int s=0;s<4;s++) dd_pick[s]=(struct dd_pickup){-1,true};
}
static void dual_audio_block(int16_t *out)
{
 /* Keep USB feedback serviced, but USB audio is not a ninth mixing source. */
 (void)ring_buf_get(&usb_audio_ring,NULL,BLK_BYTES);
 if(g_xfer_mode || dd_halted || atomic_load(&dd_record_saving)) memset(out,0,BLK_BYTES);
 else {
  uint32_t before[2]={atomic_load(&dd.deck[0].source_clock),atomic_load(&dd.deck[1].source_clock)};
  dd_render(&dd,out,BLK_FRAMES);
  for(unsigned k=0;k<2;k++){
   uint32_t length=atomic_load(&dd_length[k]);
   if(length)atomic_store(&dd_position[k],(atomic_load(&dd_position[k])+atomic_load(&dd.deck[k].source_clock)-before[k])%length);
  }
 }
 /* The audio producer never waits for storage. On overflow the take is
  * invalidated, leaving its reserved empty slot unpublished. */
 uint32_t recorded=atomic_load(&dd_rec.source_frames);
 uint32_t take=recorded<MAX_LOOP_SAMPLES?MIN(BLK_FRAMES,MAX_LOOP_SAMPLES-recorded):0;
 bonsai_record_push48(&dd_rec,out,take);
 if(recorded+take>=MAX_LOOP_SAMPLES) bonsai_record_request_stop(&dd_rec);
 dual_capture_audio(out);
 g_sample_clock+=BLK_FRAMES;
}
static bool dual_read_index(void)
{
 atomic_store(&dd_storage_ok,0);
 if(!g_emmc_ready || !emmc_read_blocks(0,dd_io,2)) return false;
 struct meta_blk candidate;
 memcpy(&candidate,dd_io,sizeof(candidate));
 if(candidate.magic!=META_MAGIC || candidate.cur_slot>=NUM_SLOTS) return false;
 memcpy(&g_meta,&candidate,sizeof(g_meta));
 g_x3_ok=0;
 if(emmc_read_blocks(X3_BLK,dd_io,X3_NBLK)) {
  memcpy(&g_x3,dd_io,sizeof(g_x3));g_x3_ok=x3_valid(&g_x3);
 }
 atomic_store(&dd_storage_ok,1);return true;
}
static bool dual_song_present(unsigned slot)
{
 if(!atomic_load(&dd_storage_ok)||slot>=NUM_SLOTS) return false;
 for(int s=0;s<4;s++) if(g_meta.slot[slot].present[s]) return true;
 return false;
}
static void dual_load(unsigned k,unsigned slot)
{
 struct dd_source next[4]={0}; uint8_t mask=0;
 if(slot>=NUM_SLOTS) return;
 k_sched_lock();atomic_store(&dd.deck[k].playing,0);atomic_store(&dd_length[k],0);
 uint32_t epoch=atomic_load(&dd_sync_epoch[k])+1u;
 atomic_store(&dd_sync_epoch[k],epoch?epoch:1u);k_sched_unlock();
 k_msleep(6); /* let the audio thread fade the previous deck before replacing it */
 for(unsigned s=0;s<4 && atomic_load(&dd_storage_ok);s++) {
  const struct slot_state *m=&g_meta.slot[slot];
  if(!m->present[s]) continue;
  uint32_t base=trk_blk(slot,s);
  if(base>=dd_capacity || !emmc_read_blocks(base,dd_io,1)) {++dd_read_errors;continue;}
  uint8_t codec=!memcmp(dd_io,"P14S",4)?DD_P14S:!memcmp(dd_io,"P16M",4)?DD_P16M:0;
  uint32_t spb=dd_decode(dd_io,codec,dd_decoded);
  if(!spb) {atomic_fetch_or(&dd_errors,1u<<(k*4+s));continue;}
  uint32_t blocks=m->trk_len[s];
  if(!blocks && m->loop_len) blocks=(m->loop_len+2*spb-1)/(2*spb);
  if(!blocks || blocks>TRACK_BLOCKS || base+blocks>dd_capacity) {atomic_fetch_or(&dd_errors,1u<<(k*4+s));continue;}
  uint32_t len=blocks*spb,anchor=m->trk_start[s]*spb;
  uint32_t content=g_meta.trk_content[slot][s];
  if(!content) content=blocks;
  if(g_x3_ok) {
   const struct x3_trk *x=&g_x3.t[slot][s];
   if(x->codec_id==codec && x->len_samps && (x->len_samps+1)/2<=len) {
    len=(x->len_samps+1)/2;anchor=x->start_samps/2;
    if(x->content_blocks) content=x->content_blocks;
   }
  }
  if(content>blocks) {atomic_fetch_or(&dd_errors,1u<<(k*4+s));continue;}
  next[s]=(struct dd_source){base,len,content*spb,(len-anchor%len)%len,blocks,spb,codec};
  mask|=1u<<s;
 }
 /* No audio block can observe partially reset rings or half-written descriptors.
  * This critical section has no IO, no sleeps, and does not mask hardware IRQs. */
 k_sched_lock();
 dd_reset_deck(&dd.deck[k],mask);
 dd_set_stretch(&dd.deck[k],true);
 memcpy(dd_src[k],next,sizeof(next));
 atomic_store(&dd_slot[k],(int)slot);
 atomic_store(&dd_position[k],0);uint32_t length=0;for(unsigned s=0;s<4;s++)length=MAX(length,next[s].length);atomic_store(&dd_length[k],length);
 k_sched_unlock();
}
static bool dual_fill(unsigned k,unsigned s)
{
 struct dd_source *v=&dd_src[k][s];
 if(!v->length) return false;
 uint32_t room=dd_room(&dd.deck[k],s);
 if(room<v->spb) return false;
 uint32_t offset=v->cursor%v->spb;
 if(v->cursor>=v->content) {
  uint32_t n=MIN(MIN(room,DD_MAX_DECODE),v->length-v->cursor);
  memset(dd_decoded,0,n*sizeof(dd_decoded[0]));
  if(!dd_push(&dd.deck[k],s,dd_decoded,n)) return false;
  v->cursor=(v->cursor+n)%v->length;return true;
 }
 uint32_t block=v->cursor/v->spb;
 uint32_t burst=MIN(DD_READ_BLOCKS,room/v->spb);
 burst=MIN(burst,v->blocks-block);
 burst=MIN(burst,(v->content-v->cursor+offset+v->spb-1)/v->spb);
 if(!burst) return false;
 /* Chain DMA reads while the audio thread runs. The synchronous transfer
  * helper busy-waits for each sector and cannot feed both decks reliably. */
 if(!emmc_read_blocks_fast(v->base+block,dd_io,burst)) {++dd_read_errors;return false;}
 for(uint32_t b=0;b<burst;b++) {
  uint32_t n=dd_decode(dd_io+b*512,v->codec,dd_decoded);
  if(n!=v->spb) {
   ++dd_bad_blocks;atomic_fetch_or(&dd_errors,1u<<(k*4+s));
   /* A corrupt block is silent for its exact duration, never arbitrary PCM. */
   n=v->spb;memset(dd_decoded,0,n*sizeof(dd_decoded[0]));
  }
  uint32_t take=MIN(n-offset,v->length-v->cursor);
  if(!dd_push(&dd.deck[k],s,dd_decoded+offset,take)) return false;
  v->cursor+=take;offset=0;
  if(v->cursor==v->length) {v->cursor=0;break;}
 }
 return true;
}
static void dual_status(void)
{
 /* The storage owner formats one reply at a time; cdc_tx copies it before
  * this shared buffer is reused. Leave stack space for playback planning. */
 char *line=dd_ui_line;
 unsigned key=irq_lock();
 uint32_t audio_total=g_dual_audio_us_total,audio_blocks=g_dual_audio_blocks;
 uint32_t push_us=dd_capture_push_us,push_calls=dd_capture_push_calls,push_max=dd_capture_push_max;
 uint32_t sof_us=dd_capture_sof_us,sof_calls=dd_capture_sof_calls,sof_max=dd_capture_sof_max;
 irq_unlock(key);
 int n=snprintf(line,sizeof(dd_ui_line),
  "{\"firmware\":\"bonsai-8-0.4.5\",\"mirror\":1,\"effects\":[],\"pitch_preserving\":1,\"grid_bpm\":[%u,%u],\"grid_valid\":[%u,%u],\"sync\":[%u,%u],\"sync_error\":[%u,%u],\"tap_count\":[%u,%u],\"usb_capture\":1,\"capture_active\":%u,\"capture_packets\":%u,\"capture_underruns\":%u,\"capture_overflows\":%u,\"capture_errors\":%u,\"position\":[%u,%u],\"length\":[%u,%u],\"record\":{\"state\":%u,\"slot\":%d,\"frames\":%u,\"error\":%u},\"fx\":[%u,%u,%u,%u,%u,%u,%u,%u],\"fx_clips\":%u,\"bulk_write\":8,\"storage\":%u,\"deck\":%u,\"slots\":[%d,%d],\"playing\":[%u,%u],\"underruns\":[%u,%u],\"read_errors\":%u,\"bad_blocks\":%u,\"unsupported\":%u,\"clips\":%u,\"audio_us_max\":%u,\"audio_us_total\":%u,\"audio_blocks\":%u,\"capture_push_us\":%u,\"capture_push_calls\":%u,\"capture_push_max\":%u,\"capture_sof_us\":%u,\"capture_sof_calls\":%u,\"capture_sof_max\":%u,\"cdc_us\":%u,\"cdc_max\":%u,\"cdc_bytes\":%u,\"i2s_errors\":%u,\"crc_errors\":%u,\"refill_blocks\":%u,\"refill_ms\":%u,\"refill_retries\":%u,\"refill_fallbacks\":%u,\"reset\":%u,\"fault\":%u,\"master\":%u,\"speed\":[%u,%u],\"mute\":[%u,%u],\"gains\":[%u,%u,%u,%u,%u,%u,%u,%u]}\n",
  atomic_load(&dd_sync_bpm[0]),atomic_load(&dd_sync_bpm[1]),
  atomic_load(&dd_sync_valid[0]),atomic_load(&dd_sync_valid[1]),
  atomic_load(&dd_sync_state[0]),atomic_load(&dd_sync_state[1]),
  atomic_load(&dd_sync_result[0]),atomic_load(&dd_sync_result[1]),
  atomic_load(&dd_sync_taps[0]),atomic_load(&dd_sync_taps[1]),
  dd_usb_capture_on,dd_usb_packets,dd_usb_audio.underruns,dd_usb_audio.overflows,dd_usb_send_errors,
  atomic_load(&dd_position[0]),atomic_load(&dd_position[1]),atomic_load(&dd_length[0]),atomic_load(&dd_length[1]),
  atomic_load(&dd_record_state),atomic_load(&dd_record_slot)+1,atomic_load(&dd_rec.source_frames)/2,atomic_load(&dd_record_error),
  0u,0u,0u,0u,0u,0u,0u,0u,0u,atomic_load(&dd_storage_ok),dd_selected,atomic_load(&dd_slot[0])+1,atomic_load(&dd_slot[1])+1,
  atomic_load(&dd.deck[0].playing),atomic_load(&dd.deck[1].playing),
  dd.deck[0].underruns,dd.deck[1].underruns,dd_read_errors,dd_bad_blocks,atomic_load(&dd_errors),dd.clips,
  g_audio_us_max,audio_total,audio_blocks,push_us,push_calls,push_max,sof_us,sof_calls,sof_max,g_dual_cdc_us,g_dual_cdc_max,g_dual_cdc_bytes,g_i2s_wfail_cnt,emmc_crc_rd_errs,g_m71_as,g_m71_wu,g_m71_rt,g_m71_fb,g_resetreas,g_last_fault_reason,atomic_load(&dd.master),
  dd_tempo_actual(&dd.deck[0]),dd_tempo_actual(&dd.deck[1]),
  atomic_load(&dd.deck[0].mute_mask),atomic_load(&dd.deck[1].mute_mask),
  atomic_load(&dd.deck[0].voice[0].gain),atomic_load(&dd.deck[0].voice[1].gain),
  atomic_load(&dd.deck[0].voice[2].gain),atomic_load(&dd.deck[0].voice[3].gain),
  atomic_load(&dd.deck[1].voice[0].gain),atomic_load(&dd.deck[1].voice[1].gain),
  atomic_load(&dd.deck[1].voice[2].gain),atomic_load(&dd.deck[1].voice[3].gain));
 if(n>0&&(size_t)n<sizeof(dd_ui_line))cdc_tx((const uint8_t *)line,(size_t)n);
 else {static const char error[]="{\"error\":\"Status response overflow\"}\n";cdc_tx((const uint8_t *)error,sizeof(error)-1);}
}
/* Listing never enters transfer mode or interrupts either transport. */
static void dual_library(void)
{
 size_t used=(size_t)snprintf(dd_ui_line,sizeof(dd_ui_line),
  "{\"library\":1,\"sample_rate\":48000,\"slots\":[");
 unsigned count=0;
 for(unsigned slot=0;slot<NUM_SLOTS;slot++) if(dual_song_present(slot)) {
  const struct slot_state *m=&g_meta.slot[slot];
  int n=snprintf(dd_ui_line+used,sizeof(dd_ui_line)-used,
   "%s{\"slot\":%u,\"present\":[%u,%u,%u,%u],\"frames\":%u,\"title\":null}",
   count?",":"",slot+1,!!m->present[0],!!m->present[1],!!m->present[2],!!m->present[3],m->loop_len);
  if(n<0||(size_t)n>=sizeof(dd_ui_line)-used-4) return;
  used+=(size_t)n;count++;
 }
 memcpy(dd_ui_line+used,"]}\n",3);used+=3;
 cdc_tx((const uint8_t *)dd_ui_line,used);
}
static void dual_host_load(const char *command)
{
 if(dual_record_busy()) {
  static const char error[]="{\"loaded\":0,\"error\":\"Stop recording and pause both decks to save first\"}\n";
  cdc_tx((const uint8_t *)error,sizeof(error)-1);return;
 }
 unsigned deck=2,slot=0;char extra;
 if(sscanf(command,"DDLOAD %u %u %c",&deck,&slot,&extra)!=2||deck>1||slot<1||slot>NUM_SLOTS) {
  static const char error[]="{\"loaded\":0,\"error\":\"Invalid deck or song\"}\n";
  cdc_tx((const uint8_t *)error,sizeof(error)-1);return;
 }
 if(!dual_song_present(slot-1)) {
  static const char error[]="{\"loaded\":0,\"error\":\"Song is empty\"}\n";
  cdc_tx((const uint8_t *)error,sizeof(error)-1);return;
 }
 /* Storage-thread ownership: only this deck is stopped and cued. */
 atomic_store(&dd_browse_slot[deck],(int)slot-1);
 atomic_store(&dd_request[deck],-1);
 dual_load(deck,slot-1);
 char line[80];int n=snprintf(line,sizeof(line),"{\"loaded\":1,\"deck\":%u,\"slot\":%u}\n",deck,slot);
 cdc_tx((const uint8_t *)line,(size_t)n);
}
static void dual_transfer(void);
static uint32_t dual_checksum(uint32_t hash,const uint8_t *data,size_t count)
{
 for(size_t i=0;i<count;i++) hash=(hash^data[i])*16777619u;
 return hash;
}
static void dual_record_failed(unsigned error)
{
 bonsai_record_fail(&dd_rec,BONSAI_RECORD_STORAGE_ERROR);
 atomic_store(&dd_record_error,error);atomic_store(&dd_record_state,5);
}
static bool dual_write_checked(uint32_t block,const uint8_t *data,unsigned count)
{
 for(unsigned i=0;i<count;i++) {
  feed_wdt();
  if(!emmc_write_blocks(block+i,data+i*512,1)) return false;
 }
 if(g_cache_on&&!emmc_cache_flush()) return false;
 if(g_usb_up) dual_transfer();
 for(unsigned i=0;i<count;i++) {
  feed_wdt();
  if(!emmc_read_blocks(block+i,dd_io,1)||memcmp(dd_io,data+i*512,512)) return false;
 }
 return true;
}
static bool dual_record_publish(void)
{
 unsigned slot=(unsigned)atomic_load(&dd_record_slot);
 uint32_t count=atomic_load(&dd_rec.committed_sectors);
 uint32_t frames=atomic_load(&dd_rec.committed_frames);
 if(!count||slot>=NUM_SLOTS||dual_song_present(slot)) return false;
 /* Cache flush can stall for seconds: this path is entered only after both
  * decks pause. Verify every recorded sector after flush before publishing. */
 if(g_cache_on&&!emmc_cache_flush()) return false;
 uint32_t hash=2166136261u;
 for(uint32_t at=0;at<count;) {
  feed_wdt();unsigned n=MIN(DD_READ_BLOCKS,count-at);
  if(!emmc_read_blocks_fast(trk_blk(slot,0)+at,dd_io,n)) return false;
  hash=dual_checksum(hash,dd_io,n*512);at+=n;
  if(g_usb_up) dual_transfer();
 }
 if(hash!=dd_record_hash) return false;
 /* Keep reserved bytes and other slots from their current on-card values. */
 if(!emmc_read_blocks(0,dd_verify,2)||!emmc_read_blocks(X3_BLK,dd_verify+1024,3)) return false;
 if(!bonsai_library_prepare(dd_verify,dd_verify+1024,slot,frames,count)) return false;
 if(!dual_write_checked(X3_BLK,dd_verify+1024,3)||!dual_write_checked(0,dd_verify,2)) return false;
 if(!bonsai_library_publish(dd_verify,dd_verify+1024,slot)) return false;
 unsigned sector=(16+44*slot)/512;
 /* Publish only the sector containing present[0], after all fields persisted.
  * The inherited format has no journal; sector tears remain a power-loss risk. */
 if(!dual_write_checked(sector,dd_verify+512*sector,1)) return false;
 memcpy(&g_meta,dd_verify,sizeof(g_meta));memcpy(&g_x3,dd_verify+1024,sizeof(g_x3));g_x3_ok=1;
 return true;
}
static bool dual_record_service(void)
{
 if(atomic_exchange(&dd_record_toggle,false)) {
  if(atomic_load(&dd_record_state)==1) {
   bonsai_record_request_stop(&dd_rec);atomic_store(&dd_record_state,2);
  } else if(!dual_record_busy()&&!atomic_load(&dd_flush_req)) {
   int slot=-1;
   for(unsigned i=0;i<NUM_SLOTS;i++) if(!dual_song_present(i)) {slot=(int)i;break;}
   if(!atomic_load(&dd_storage_ok)||!g_x3_ok||g_xfer_mode||slot<0||
      trk_blk((unsigned)slot,0)+TRACK_BLOCKS>dd_capacity) {
    atomic_store(&dd_record_slot,-1);dual_record_failed(slot<0?2:3);
   } else {
    k_sched_lock();
    atomic_store(&dd_record_slot,slot);atomic_store(&dd_record_error,0);
    dd_record_hash=2166136261u;bonsai_record_start(&dd_rec);
    atomic_store(&dd_record_state,1);k_sched_unlock();
   }
  }
 }
 if(!dual_record_busy()) return false;
 unsigned core=atomic_load(&dd_rec.state);
 if(core==BONSAI_RECORD_FAILED) {atomic_store(&dd_record_error,atomic_load(&dd_rec.error));atomic_store(&dd_record_state,5);return false;}
 if(core==BONSAI_RECORD_DRAINING) atomic_store(&dd_record_state,2);
 uint32_t n=bonsai_record_peek_sector(&dd_rec,dd_verify);
 if(n) {
  uint32_t sector=atomic_load(&dd_rec.committed_sectors);
  if(sector>=TRACK_BLOCKS||!emmc_write_blocks(trk_blk((unsigned)atomic_load(&dd_record_slot),0)+sector,dd_verify,1)) {
   dual_record_failed(4);return false;
  }
  dd_record_hash=dual_checksum(dd_record_hash,dd_verify,512);
  if(!bonsai_record_commit_sector(&dd_rec)) {dual_record_failed(4);return false;}
  return true;
 }
 if(atomic_load(&dd_rec.state)==BONSAI_RECORD_COMPLETE) {
  atomic_store(&dd_record_state,3);
  k_sched_lock();
  bool can_save=!atomic_load(&dd.deck[0].playing)&&!atomic_load(&dd.deck[1].playing)&&
                !dd.deck[0].envelope&&!dd.deck[1].envelope;
  if(can_save) atomic_store(&dd_record_saving,true);
  k_sched_unlock();
  if(can_save) {
   bool ok=dual_record_publish();
   if(ok) atomic_store(&dd_record_state,4);else {dual_record_failed(5);dual_read_index();}
   atomic_store(&dd_record_saving,false);
  }
 }
 return false;
}
static bool dual_commit(void)
{
 bool ok=!g_cache_on||emmc_cache_flush();
 ok=dual_read_index()&&ok;
 for(int k=0;k<2;k++) {
  int slot=atomic_load(&dd_slot[k]);dual_request_song((unsigned)k,slot<0?k:slot);
 }
 return ok;
}
static bool dual_format(void)
{
 if(!g_emmc_ready || dd_capacity<SLOT0_BLOCK+NUM_SLOTS*NTRK*TRACK_BLOCKS) return false;
 /* Explicit host ERASE command only. Never reached from boot or PLAY. */
 memset(&g_meta,0,sizeof(g_meta));g_meta.magic=META_MAGIC;
 for(unsigned s=0;s<NUM_SLOTS;s++) g_meta.slot[s].speed_q16=65536;
 memset(dd_io,0,6*512);
 memcpy(dd_io,&g_meta,sizeof(g_meta));
 bool ok=emmc_write_blocks(1,dd_io+512,5)&&emmc_write_blocks(0,dd_io,1);
 if(g_cache_on) ok=emmc_cache_flush()&&ok;
 return dual_read_index()&&ok;
}
#ifdef CONFIG_THREAD_RUNTIME_STATS
struct dual_profile_line { char text[768]; size_t used; unsigned count; };
static void dual_profile_thread(const struct k_thread *thread,void *data)
{
 struct dual_profile_line *line=data;
 k_thread_runtime_stats_t stats;
 if(k_thread_runtime_stats_get((k_tid_t)thread,&stats)) return;
 const char *name=thread==&audio_tcb?"audio":thread==&streamer_tcb?"storage":k_thread_name_get((k_tid_t)thread);
 int n=snprintf(line->text+line->used,sizeof(line->text)-line->used,
  "%s[\"%.16s\",%d,%u]",line->count?",":"",name?name:"unnamed",
  k_thread_priority_get((k_tid_t)thread),(uint32_t)stats.total_cycles);
 if(n>0&&(size_t)n<sizeof(line->text)-line->used-4) {line->used+=(size_t)n;line->count++;}
}
static void dual_profile(void)
{
 struct dual_profile_line line={.text="{\"threads\":[",.used=12};
 k_thread_foreach_unlocked(dual_profile_thread,&line);
 memcpy(line.text+line.used,"]}\n",3);line.used+=3;
 cdc_tx((const uint8_t *)line.text,line.used);
}
#endif
static void dual_transfer(void)
{
 static int64_t last;
 static struct bonsai_cdc_parser parser;
 static uint32_t session;
 uint8_t c;
 bool online=cdc_session_sync();
 if(session!=g_cdc_session) {
  session=g_cdc_session;bonsai_cdc_parser_reset(&parser);
  /* An interrupted transfer is already paused. Flush acknowledged writes and
   * reload the existing index; never initialize or publish a new song here. */
  if(g_xfer_mode){(void)dual_commit();g_xfer_mode=0;}
  dd_ui_until=0;
 }
 if(!online)return;
 g_cdc_response_session=g_cdc_session;
 if(!g_xfer_mode) {
  /* Leave at most one complete command pending under backpressure. The RX
   * callback stops accepting into a full ring rather than discarding bytes. */
  for(unsigned budget=0;parser.pending==BONSAI_CDC_IDLE&&budget<BONSAI_CDC_RX_BUDGET;budget++) {
   if(ring_buf_get(&g_cdc_rx,&c,1)!=1)break;
   (void)bonsai_cdc_parse(&parser,c);
  }
  cdc_rx_resume();
  /* Subscribe when requested, even while TX is full. Waiting must not expire
   * the edge stream or pop its press/release/LED transitions prematurely. */
  if(parser.pending==BONSAI_CDC_MIRROR)dual_mirror_listen();
  if(!bonsai_cdc_admissible(&parser,&g_cdc_tx_queue))return;
  /* All normal responses fit REPLY_MAX. This sole producer reserves capacity
   * before popping mirror edges, formatting status, or acting on DDLOAD. */
  switch(parser.pending) {
   case BONSAI_CDC_STATUS:dual_status();break;
   case BONSAI_CDC_MIRROR:dual_mirror_reply();break;
   case BONSAI_CDC_LIBRARY:dual_library();break;
   case BONSAI_CDC_LOAD:dual_host_load(parser.line);break;
#ifdef CONFIG_THREAD_RUNTIME_STATS
   case BONSAI_CDC_PROFILE:dual_profile();break;
#endif
   case BONSAI_CDC_ENTER:
    if(!dual_record_busy()) {
     /* Every earlier reply is in the driver's ordered FIFO before binary
      * mode begins. Binary replies use the same queue/FIFO, never poll_out. */
     g_xfer_mode=1;
     atomic_store(&dd.deck[0].playing,0);atomic_store(&dd.deck[1].playing,0);
     last=k_uptime_get();
    }
    break;
   default:break;
  }
  bonsai_cdc_parser_reset(&parser);return;
 }
 /* Reserve a complete largest response BEFORE reading a binary command.
  * Header/payload reads retain the existing transfer timeout/verification. */
 if(bonsai_cdc_room(&g_cdc_tx_queue)<BONSAI_CDC_REPLY_MAX)return;
 if(ring_buf_get(&g_cdc_rx,&c,1)!=1) {
  if(k_uptime_get()-last>15000) {dual_commit();g_xfer_mode=0;}
  return;
 }
 last=k_uptime_get();uint8_t reply='E';
 if(c=='P') {
  uint32_t info[6]={512,NUM_SLOTS,4,SLOT0_BLOCK,TRACK_BLOCKS,META_MAGIC};
  cdc_tx((const uint8_t *)"SP1!",4);cdc_tx((const uint8_t *)info,sizeof(info));return;
 }
 if(c=='S') {dual_status();return;}
 if(c=='B') {
  uint8_t header[5];
  if(!cdc_rx(header,sizeof(header),1000)) {xfer_resync('E');return;}
  uint32_t block=(uint32_t)header[0]|(uint32_t)header[1]<<8|(uint32_t)header[2]<<16|(uint32_t)header[3]<<24;
  uint32_t count=header[4],limit=MIN(dd_capacity,SLOT0_BLOCK+NUM_SLOTS*NTRK*TRACK_BLOCKS);
  /* One bounded request at a time: drain USB while receiving, then write and
   * read back the whole burst. No acknowledgement until every byte matches. */
  if(!count||count>8) {xfer_resync('E');return;}
  if(!cdc_rx(dd_io,count*512,4000)) {xfer_resync('E');return;}
  if(atomic_load(&dd_storage_ok)&&block<limit&&count<=limit-block&&
     emmc_write_blocks(block,dd_io,count)&&emmc_read_blocks(block,dd_verify,count)&&
     !memcmp(dd_io,dd_verify,count*512)) reply='b';
  cdc_tx(&reply,1);return;
 }
 if(c=='R'||c=='W') {
  uint8_t addr[4];
  if(!cdc_rx(addr,4,1000)) {xfer_resync(c=='R'?'e':'E');return;}
  uint32_t block=(uint32_t)addr[0]|(uint32_t)addr[1]<<8|(uint32_t)addr[2]<<16|(uint32_t)addr[3]<<24;
  uint32_t limit=MIN(dd_capacity,SLOT0_BLOCK+NUM_SLOTS*NTRK*TRACK_BLOCKS);
  if(c=='R') {
   bool ok=g_emmc_ready&&block<limit&&emmc_read_blocks(block,dd_io,1);
   reply=ok?'r':'e';cdc_tx(&reply,1);if(ok) cdc_tx(dd_io,512);return;
  }
  if(!cdc_rx(dd_io,512,4000)) {xfer_resync('E');return;}
  /* Unknown stock/2.x storage is readable for backup but write-locked. */
  if(atomic_load(&dd_storage_ok)&&block<limit&&emmc_write_blocks(block,dd_io,1)) reply='w';
 } else if(c=='F'||c=='X') {
  if(dual_commit()) reply=c=='F'?'f':'x';
  if(c=='X') g_xfer_mode=0;
 } else if(c=='I') {
  uint8_t token[16];
  if(cdc_rx(token,16,1000)&&!memcmp(token,"ERASE ALL SONGS!",16)&&dual_format()) reply='i';
 }
 cdc_tx(&reply,1);
}
static void dual_storage_thread(void *a,void *b,void *c)
{
 ARG_UNUSED(a);ARG_UNUSED(b);ARG_UNUSED(c);
 emmc_init();g_emmc_clk_half_us=0;g_emmc_ready=emmc_is_ready();
 if(g_emmc_ready&&emmc_read_ext_csd(dd_io)) {
  dd_capacity=(uint32_t)dd_io[212]|(uint32_t)dd_io[213]<<8|(uint32_t)dd_io[214]<<16|(uint32_t)dd_io[215]<<24;
  uint32_t cache=(uint32_t)dd_io[249]|(uint32_t)dd_io[250]<<8|(uint32_t)dd_io[251]<<16|(uint32_t)dd_io[252]<<24;
  if(cache&&dd_io[192]>=6) g_cache_on=emmc_cache_enable();
 }
 dual_read_index();g_meta_loaded=1;
 int first=-1,second=-1;
 for(unsigned s=0;s<NUM_SLOTS;s++) if(dual_song_present(s)) {if(first<0) first=s;else {second=s;break;}}
 dual_request_song(0,first<0?0:first);dual_request_song(1,second<0?1:second);
 while(1) {
  if(atomic_load(&dd_flush_req)&&!dual_record_busy()) {
   if(g_cache_on) emmc_cache_flush();
   dd_halted=true;atomic_store(&dd_flush_req,0);
  }
  if(dd_halted) {k_msleep(10);continue;}
  if(g_xfer_mode) {
   if(g_usb_up)dual_transfer();
   k_msleep(1);continue;
  }
  /* One record sector per iteration; playback gets a refill opportunity after
   * every write. The bounded producer fails rather than silently losing time. */
  dual_record_service();
  for(unsigned k=0;k<2;k++) {
   int request=atomic_exchange(&dd_request[k],-1);
   if(request>=0&&!dual_record_busy()) dual_load(k,(unsigned)request);
  }
  /* Wait for eight sectors of room: immediately topping off a single sector
   * repeats CMD23/CMD18 and task wakeups eight times as often. At the refill
   * threshold each voice still has >=2112 native frames (70 ms at 1.25x). */
  unsigned best=8;uint32_t room=0;
  for(unsigned n=0;n<8;n++) if(dd_src[n/4][n%4].length) {
   uint32_t r=dd_room(&dd.deck[n/4],n%4);
   if(r>room && r>=DD_READ_BLOCKS*dd_src[n/4][n%4].spb) {room=r;best=n;}
  }
  bool filled=best<8&&dual_fill(best/4,best%4);
  /* Source refill has priority over bounded tempo planning and host replies.
   * Correlation belongs here, never in the real-time audio callback. */
  bool planned=dd_stretch_service(&dd,1024u)>0;
  if(g_usb_up)dual_transfer();
  if(filled||planned)k_yield();else k_msleep(1);
 }
}
static void dual_stop_and_flush(void)
{
 atomic_store(&dd.deck[0].playing,0);atomic_store(&dd.deck[1].playing,0);
 atomic_store(&dd_record_toggle,false);atomic_store(&dd_flush_req,1);bonsai_record_request_stop(&dd_rec);
 for(unsigned n=0;n<12000&&atomic_load(&dd_flush_req);n++) {feed_wdt();k_msleep(10);}
}
static void dual_recovery_check(void)
{
 static int64_t held=-1;int raw=ladder_read(&adc_ladder[LAD_TRACKS]);
 /* Upstream's measured, dedicated Track 1+4 ladder band. Always available. */
 if(raw>=1256&&raw<1347) {
  if(held<0) held=k_uptime_get();
  else if(k_uptime_get()-held>=3000) enter_dfu();
 } else held=-1;
}
static void dual_choose_song(int direction)
{
 if(dual_record_busy()) return;
 int slot=atomic_load(&dd_browse_slot[dd_selected]);
 if(slot<0) slot=atomic_load(&dd_slot[dd_selected]);
 if(slot<0) slot=direction>0?-1:0;
 for(int n=1;n<=(int)NUM_SLOTS;n++) {
  unsigned next=(unsigned)(slot+direction*n+(int)(2u*NUM_SLOTS))%NUM_SLOTS;
  if(dual_song_present(next)) {dual_request_song(dd_selected,(int)next);break;}
 }
}
static int dual_track_key(int raw)
{
 if(raw<0) return -2;
 if(raw<110) return TRK_NONE;
 if(raw>=110&&raw<308) return TRK_1;
 if(raw>=308&&raw<488) return TRK_2;
 if(raw>=650&&raw<795) return TRK_3;
 if(raw>=1154&&raw<1256) return TRK_4;
 if(raw>=1773&&raw<1840) return TRK_PLAY;
 return -2; /* Chords never masquerade as single buttons. */
}
static void dual_controls(void)
{
 int tc=TRK_NONE,tcan=TRK_NONE,tcount=0,vc=VOL_NONE,vcan=VOL_NONE,vcount=0;
 bool spent=false,fn_used=false,fn_before=false,solo=false,track_blocked=false,modified_wheel=false,stem_modified=false;uint32_t solo_saved=0;
 int modified_wheel_action=-1;unsigned wheel_deck=0;
 int pressed_deck=0,pressed_track=TRK_NONE;int64_t track_time=0,fn_time=0,repeat=0,hp_time=0;
 int smooth[4]={-1,-1,-1,-1};unsigned fi=0;uint32_t led_tick=0;
 int64_t record_chord=0;bool record_latched=false,record_blocked=false;
 uint16_t ui_track=0,ui_candidate=0;unsigned ui_count=0;
 while(1) {
  feed_wdt();dual_recovery_check();int64_t now=k_uptime_get();
  dual_sync_poll();
  int ladder=ladder_read(&adc_ladder[LAD_TRACKS]);
  int traw=dual_track_key(ladder);
  uint16_t mask=dd_ui_tracks(ladder);
  if(ladder>=0) {
   if(mask==ui_track) ui_count=0;
   else if(mask!=ui_candidate) {ui_candidate=mask;ui_count=1;}
   else if(++ui_count>=3) {ui_track=mask;ui_count=0;}
  }
  int vraw=decode_vol(ladder_read(&adc_ladder[LAD_VOL]));
  bool fn=pwr_pressed();
  if(fn&&!fn_before) {fn_time=now;fn_used=false;}
  if(traw==tc) tcount=0;else if(traw==tcan) {if(++tcount>=3) {tc=traw;tcount=0;}} else {tcan=traw;tcount=1;}
  int oldv=vc;
  if(vraw==vc) vcount=0;else if(vraw==vcan) {if(++vcount>=3) {vc=vraw;vcount=0;}} else {vcan=vraw;vcount=1;}
  dd_button_mask=ui_track|(fn?1u<<5:0)|
   (vc==VOL_TEMPO_UP?1u<<6:0)|(vc==VOL_TEMPO_DOWN?1u<<7:0)|
   ((vc==VOL_UP||vc==VOL_BOTH)?1u<<8:0)|((vc==VOL_DOWN||vc==VOL_BOTH)?1u<<9:0);
  if(fn&&vc==VOL_BOTH) {
   fn_used=true;record_blocked=true;
   if(!record_chord) record_chord=now;
   if(!record_latched&&now-record_chord>=1000) {
    atomic_store(&dd_record_toggle,true);record_latched=true;
   }
  } else record_chord=0;
  /* One recording gesture requires a complete release. A brief volume ADC
   * gap while FUNCTION stays held must not re-arm and stop the same take. */
  if(!fn&&vc==VOL_NONE) {record_latched=false;record_blocked=false;}
  if(vc==VOL_NONE) {modified_wheel=false;modified_wheel_action=-1;}
  if(tc==-2) {spent=true;track_blocked=true;fn_used|=fn;}
  if(tc==TRK_NONE) track_blocked=false;
  if(fn&&tc>=0) {fn_used=true;if(pressed_track>=0){stem_modified=true;spent=true;}}
  if(!track_blocked&&pressed_track==TRK_NONE&&tc>=0) {
   pressed_track=tc;pressed_deck=dd_selected;track_time=now;spent=false;stem_modified=false;
   if(fn) {
    fn_used=spent=stem_modified=true;
    if(tc==TRK_1)dual_sync_tap((unsigned)pressed_deck);
    else if(tc==TRK_2)dual_sync_follow((unsigned)pressed_deck);
    if(tc==TRK_PLAY&&!atomic_load(&dd_record_saving)) {
     bool start=!(atomic_load(&dd.deck[0].playing)&&atomic_load(&dd.deck[1].playing));
     atomic_store(&dd.deck[0].playing,start);atomic_store(&dd.deck[1].playing,start);
    }
   }
  }
  if(pressed_track>=0&&!spent&&tc==pressed_track&&now-track_time>=450) {
   struct dd_deck *d=&dd.deck[pressed_deck];spent=true;
   if(pressed_track==TRK_PLAY) dual_request_song((unsigned)pressed_deck,atomic_load(&dd_slot[pressed_deck]));
   else {solo_saved=atomic_load(&d->mute_mask);atomic_store(&d->mute_mask,15u^(1u<<pressed_track));solo=true;}
  }
  if(pressed_track>=0&&tc==TRK_NONE) {
   struct dd_deck *d=&dd.deck[pressed_deck];
   if(solo) {atomic_store(&d->mute_mask,solo_saved);solo=false;}
   else if(!spent&&!fn&&!atomic_load(&dd_record_saving)) {
    if(pressed_track==TRK_PLAY) atomic_store(&d->playing,!atomic_load(&d->playing));
    else atomic_fetch_xor(&d->mute_mask,1u<<pressed_track);
   }
   pressed_track=TRK_NONE;spent=false;stem_modified=false;
  }
  /* A wheel gesture begun with FUNCTION stays a modified gesture until the
   * wheel is released. Releasing FUNCTION first must not change deck pitch. */
  bool wheel=vc==VOL_TEMPO_UP||vc==VOL_TEMPO_DOWN;
  if(!record_blocked&&!(wheel&&(modified_wheel||stem_modified)&&!fn)&&vc!=VOL_NONE&&(vc!=oldv||now>=repeat)) {
   repeat=now+(vc!=oldv?500:110);
   int dir=(vc==VOL_UP||vc==VOL_TEMPO_UP)?1:-1;
   if(vc==VOL_UP||vc==VOL_DOWN) {
    int volume=(int)atomic_load(&dd.master)+dir*8;
    atomic_store(&dd.master,CLAMP(volume,0,256));fn_used|=fn;
   } else if(vc==VOL_TEMPO_UP||vc==VOL_TEMPO_DOWN) {
    if(fn) {
     if(!modified_wheel) {
      /* Freeze a wheel gesture's role until its complete release. Releasing
       * Track 3 first cannot turn a phase nudge into library browsing. */
      modified_wheel_action=tc==TRK_3?1:(tc>=TRK_1&&tc<=TRK_4?2:0);
      wheel_deck=(unsigned)pressed_deck;modified_wheel=true;
     }
     if(modified_wheel_action==1&&tc==TRK_3&&pressed_track==TRK_3)
      dual_sync_nudge(wheel_deck,dir);
     else if(modified_wheel_action==0&&(tc<TRK_1||tc>TRK_4))dual_choose_song(dir);
     repeat=now+500;fn_used=true;
    }
    else dual_sync_manual_tempo(dd_selected,dir);
   }
  }
  if(fn&&!fn_used&&now-fn_time>=1500) power_off();
  if(!fn&&fn_before&&!fn_used&&now-fn_time>=35) {
   dd_selected^=1;
   for(int s=0;s<4;s++) dd_pick[s]=(struct dd_pickup){-1,true};
  }
  fn_before=fn;
  int raw=ladder_read(&adc_ladder[LAD_FADER0+fi]);
  if(raw>=0) {
   smooth[fi]=smooth[fi]<0?raw:(smooth[fi]*3+raw)/4;
   unsigned value=MIN((unsigned)smooth[fi]*256/3700,256u);
   dd_physical[fi]=(uint16_t)MIN((unsigned)raw*256/3700,256u);
   struct dd_voice *v=&dd.deck[dd_selected].voice[fi];
   if(dd_pickup_move(&dd_pick[fi],atomic_load(&v->gain),value)) atomic_store(&v->gain,value);
  }
  fi=(fi+1)%4;
  /* Stock-style speaker muting when headphones are inserted. */
  if(now-hp_time>=250&&g_hp_on==1) {
   hp_time=now;int connected=hp_detect_connected();
   if(connected>=0&&connected!=g_hp_in) {g_hp_in=connected;tas_set_speaker(!connected);}
  }
  if(++led_tick%4==0) {
   all_off();
   if(!atomic_load(&dd_storage_ok)) {if((now/350)&1) {led_on(0);led_on(2);}else{led_on(1);led_on(3);}}
   else if(fn) {
    int slot=atomic_load(&dd_slot[dd_selected]);
    if(slot>=0) {led_on(slot%4);if((now/250)&1) led_on(slot/4);}
   } else {
    led_on(dd_selected?3:0);
    if(atomic_load(&dd.deck[0].playing)) led_on(1);
    if(atomic_load(&dd.deck[1].playing)) led_on(2);
   }
   unsigned recording=atomic_load(&dd_record_state);
   if(!recording)(void)dual_sync_leds((uint32_t)now);
   if(recording>=1&&recording<=3) {
    if((now/(recording==1?500:150))&1) {led_on(0);led_on(1);led_on(2);led_on(3);}else all_off();
   } else if(recording==5&&((now/150)&1)) {led_on(0);led_on(3);}
   uint32_t mute=atomic_load(&dd.deck[dd_selected].mute_mask);
   for(unsigned s=0;s<4;s++) {
    bool on=dd.deck[dd_selected].voice[s].present&&!(mute&(1u<<s));
    if(dd_pick[s].waiting) on=on&&((now/150)&1);
    if(g_xfer_mode) on=(now/200)&1;
    if(on) track_led_on(s);else track_led_off(s);
   }
  }
  dual_mirror_capture(k_uptime_get_32());
  k_msleep(8);
 }
}
