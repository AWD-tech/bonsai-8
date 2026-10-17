/* Host-only boundaries around functions extracted from dual_firmware.inc.
 * Storage is a bounded-memory temporary file, not a timing model of eMMC. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bonsai_record.h"
#include "bonsai_library.h"
#include "bonsai_playlist.h"
#define NUM_SLOTS 16u
#define NTRK 4u
#define SLOT0_BLOCK 4096u
#define TRACK_BLOCKS BONSAI_LIBRARY_TRACK_SECTORS
#define META_MAGIC 0x53453341u
#define X3_MAGIC 0x53453358u
#define X3_VER 1u
#define X3_BLK 3u
#define X3_NBLK 3u
#define DD_READ_BLOCKS 8u
#define BLK_FRAMES 256u
#define BLK_BYTES (BLK_FRAMES * 2u * sizeof(int16_t))
#define DECIM 1u /* Production CMake builds the default 48 kHz hardware path. */
#define MIN(a,b) ((a)<(b)?(a):(b))
/* PRODUCTION_TYPES */
_Static_assert(sizeof(struct meta_blk)==1024,"metadata layout");
_Static_assert(sizeof(struct x3_tab)==1040,"extended layout");
static struct meta_blk g_meta;
static struct x3_tab g_x3;
static struct dd_engine dd;
static struct bonsai_record dd_rec;
static _Atomic unsigned dd_record_state,dd_record_error,dd_storage_ok,dd_flush_req;
static _Atomic int dd_record_slot;
static _Atomic bool dd_record_toggle,dd_record_saving;
static _Atomic uint32_t dd_length[2],dd_position[2],dd_playlist_rows[16];
static uint32_t dd_record_hash,dd_capacity;
static int dd_playlist_state;
static bool g_x3_ok,g_cache_on,g_usb_up,g_emmc_ready,dd_halted;
static unsigned g_xfer_mode,usb_audio_ring;
static uint64_t g_sample_clock;
static uint8_t dd_io[4098],dd_verify[4096],disk_index[4096],original_index[4096];
static FILE *audio_file,*expected_file;
static unsigned target_slot,data_writes,save_ops,fail_save_op,fail_data_write;
static unsigned metadata_writes,flush_calls,transfer_calls,high_water;
static bool final_write_seen;
static uint32_t expected_source,expected_frames;
static uint32_t trk_blk(uint32_t slot,uint32_t stem);
static uint16_t x3_sum(const struct x3_tab *table);
static bool dual_read_index(void);
static void k_sched_lock(void){}
static void k_sched_unlock(void){}
static void feed_wdt(void){}
static unsigned ring_buf_get(unsigned *r,void *data,unsigned n){(void)r;(void)data;return n;}
static void dual_transfer(void){transfer_calls++;assert(atomic_load(&dd_record_saving));}
static bool io_fails(void){
 if(!atomic_load(&dd_record_saving))return false;
 assert(!atomic_load(&dd.deck[0].playing)&&!atomic_load(&dd.deck[1].playing));
 assert(!dd.deck[0].envelope&&!dd.deck[1].envelope);
 return ++save_ops==fail_save_op;
}
static bool emmc_write_blocks(uint32_t block,const uint8_t *data,unsigned count){
 if(io_fails())return false;
 if(block<8){
  assert(atomic_load(&dd_record_saving));assert(block+count<=6);
  metadata_writes+=count;
  memcpy(disk_index+block*512,data,count*512);
  if(disk_index[16+44*target_slot])final_write_seen=true;
  return true;
 }
 assert(count==1&&block==trk_blk(target_slot,0)+data_writes);
 if(++data_writes==fail_data_write)return false;
 assert(fseek(audio_file,(long)(block-trk_blk(target_slot,0))*512,SEEK_SET)==0);
 assert(fwrite(data,512,count,audio_file)==count);return true;
}
static bool emmc_read_blocks(uint32_t block,uint8_t *data,unsigned count){
 if(io_fails())return false;
 if(block<8){assert(block+count<=8);memcpy(data,disk_index+block*512,count*512);return true;}
 assert(block>=trk_blk(target_slot,0)&&block+count<=trk_blk(target_slot,0)+data_writes);
 assert(fseek(audio_file,(long)(block-trk_blk(target_slot,0))*512,SEEK_SET)==0);
 assert(fread(data,512,count,audio_file)==count);return true;
}
static bool emmc_read_blocks_fast(uint32_t block,uint8_t *data,unsigned count){return emmc_read_blocks(block,data,count);}
static bool emmc_cache_flush(void){flush_calls++;return !io_fails();}
static int16_t quantized_average(int16_t a,int16_t b){
 int average=((int)a+b)/2;
 return (int16_t)(average-((average%4+4)%4));
}
static void dual_capture_audio(const int16_t *out){
 /* Independent oracle consumes only the frames accepted by the recorder.
  * File-backed expected audio keeps duration out of the RAM budget. */
 uint32_t accepted=atomic_load(&dd_rec.source_frames)-expected_source;
 assert(accepted<=BLK_FRAMES&&!(accepted&1));
 for(unsigned i=0;i<accepted;i+=2){
  int16_t pair[]={quantized_average(out[i*2],out[i*2+2]),quantized_average(out[i*2+1],out[i*2+3])};
  assert(fwrite(pair,sizeof(pair),1,expected_file)==1);expected_frames++;
 }
 expected_source+=accepted;
}
/* PRODUCTION_FUNCTIONS */

static void close_fixture(void){if(audio_file)fclose(audio_file);if(expected_file)fclose(expected_file);audio_file=expected_file=NULL;}
static void reset(unsigned empty_slot){
 close_fixture();audio_file=tmpfile();expected_file=tmpfile();assert(audio_file&&expected_file);
 target_slot=empty_slot;dd_init(&dd);bonsai_record_init(&dd_rec);
 atomic_store(&dd_record_state,0);atomic_store(&dd_record_error,0);atomic_store(&dd_record_slot,-1);
 atomic_store(&dd_record_toggle,false);atomic_store(&dd_record_saving,false);atomic_store(&dd_flush_req,0);
 g_x3_ok=g_cache_on=g_usb_up=g_emmc_ready=true;g_xfer_mode=0;dd_halted=false;g_sample_clock=0;
 dd_capacity=SLOT0_BLOCK+NUM_SLOTS*NTRK*TRACK_BLOCKS;
 data_writes=save_ops=fail_save_op=fail_data_write=metadata_writes=flush_calls=transfer_calls=high_water=0;
 expected_source=expected_frames=0;final_write_seen=false;
 for(unsigned i=0;i<sizeof(disk_index);i++)disk_index[i]=(uint8_t)(19+i*37);
 memset(&g_meta,0,sizeof(g_meta));g_meta.magic=META_MAGIC;g_meta.cur_slot=15;
 g_meta.fixed_len=0x12345678;g_meta.led_full=0x87654321;
 for(unsigned i=0;i<16;i++){
  g_meta.slot[i].speed_q16=65536;g_meta.slot[i].loop_len=280;
  memset(g_meta.slot[i].present,i==empty_slot?0:1,4);
  for(unsigned s=0;s<4;s++){g_meta.slot[i].trk_len[s]=1;g_meta.trk_content[i][s]=1;}
  g_meta.chop[i][0]=4;g_meta.chop[i][1]=2;g_meta.song_mode[i]=0x51;
 }
 memset(&g_x3,0,sizeof(g_x3));g_x3.magic=X3_MAGIC;g_x3.ver=X3_VER;g_x3.rsv0=0x1234;
 for(unsigned i=0;i<16;i++)for(unsigned s=0;s<4;s++)g_x3.t[i][s]=(struct x3_trk){0,280,1,5,1,128,0};
 g_x3.sum=x3_sum(&g_x3);
 memcpy(disk_index,&g_meta,sizeof(g_meta));memcpy(disk_index+1536,&g_x3,sizeof(g_x3));
 memset(disk_index+3072,0,1024);memcpy(original_index,disk_index,sizeof(disk_index));
 assert(dual_read_index());assert(!dual_song_present(empty_slot));
 for(unsigned k=0;k<2;k++){
  dd_reset_deck(&dd.deck[k],15);atomic_store(&dd.deck[k].playing,1);atomic_store(&dd_length[k],120000);
  atomic_store(&dd_position[k],0);
  for(unsigned s=0;s<4;s++)atomic_store(&dd.deck[k].voice[s].gain,100+17*s+11*k);
 }
 atomic_store(&dd.master,170);
}
static void start_take(void){
 atomic_store(&dd_record_toggle,true);assert(!dual_record_service());
 assert(atomic_load(&dd_record_state)==1&&atomic_load(&dd_record_slot)==(int)target_slot);
}
static void audio_block(void){
 struct dd_frame source[256];int16_t output[BLK_FRAMES*2];
 for(unsigned k=0;k<2;k++)for(unsigned s=0;s<4;s++){
  uint32_t n=MIN(dd_room(&dd.deck[k],s),256u),first=atomic_load(&dd.deck[k].voice[s].written);
  for(uint32_t i=0;i<n;i++){
   source[i].l=(int16_t)((int)((first+i)*(31+s*6+k*23)%20000)-10000);
   source[i].r=(int16_t)((int)((first+i)*(17+s*8+k*19)%18000)-9000);
  }
  assert(dd_push(&dd.deck[k],s,source,n));
 }
 dual_audio_block(output);
 uint32_t used=atomic_load(&dd_rec.written)-atomic_load(&dd_rec.read);
 assert(used<=BONSAI_RECORD_RING);if(used>high_water)high_water=used;
}
static void produce(unsigned blocks){for(unsigned i=0;i<blocks;i++){audio_block();dual_record_service();}}
static void drain_stopped(void){
 atomic_store(&dd_record_toggle,true);dual_record_service();assert(atomic_load(&dd_record_state)==2);
 audio_block(); /* Actual producer must acknowledge the stop request. */
 for(unsigned i=0;i<100&&atomic_load(&dd_record_state)!=3;i++)dual_record_service();
 assert(atomic_load(&dd_record_state)==3);assert(!metadata_writes&&!flush_calls);
}
static void pause_and_save(void){
 atomic_store(&dd.deck[0].playing,0);atomic_store(&dd.deck[1].playing,0);
 for(unsigned i=0;i<100&&(dd.deck[0].envelope||dd.deck[1].envelope);i++)audio_block();
 assert(!dd.deck[0].envelope&&!dd.deck[1].envelope);dual_record_service();
 assert(!atomic_load(&dd_record_saving));
}
static void compare_pcm(void){
 uint8_t sector[512];struct dd_frame decoded[DD_MAX_DECODE];
 rewind(audio_file);rewind(expected_file);uint32_t remaining=expected_frames;
 for(unsigned b=0;b<data_writes;b++){
  assert(fread(sector,512,1,audio_file)==1);assert(dd_decode(sector,DD_P14S,decoded)==140);
  unsigned n=MIN(remaining,140u);
  for(unsigned i=0;i<n;i++){struct dd_frame expected;assert(fread(&expected,sizeof(expected),1,expected_file)==1);assert(!memcmp(&decoded[i],&expected,sizeof(expected)));}
  for(unsigned i=n;i<140;i++)assert(!decoded[i].l&&!decoded[i].r);
  remaining-=n;
 }
 assert(!remaining);assert(fgetc(expected_file)==EOF);
}
static void check_saved(void){
 assert(atomic_load(&dd_record_state)==4&&!dual_record_busy()&&!atomic_load(&dd_record_error));
 assert(atomic_load(&dd_rec.source_frames)==expected_source);
 assert(atomic_load(&dd_rec.committed_frames)==expected_frames);
 assert(atomic_load(&dd_rec.committed_sectors)==(expected_frames+139)/140);
 assert(data_writes==(expected_frames+139)/140);assert(final_write_seen);
 assert(metadata_writes==6&&flush_calls==4&&transfer_calls>0);
 assert(!dd.deck[0].underruns&&!dd.deck[1].underruns);
 compare_pcm();
 memset(&g_meta,0,sizeof(g_meta));memset(&g_x3,0,sizeof(g_x3));g_x3_ok=false;
 assert(dual_read_index()); /* Reconstruct the catalog from stored bytes. */
 assert(dual_song_present(target_slot));
 assert(g_meta.slot[target_slot].loop_len==expected_frames*2);
 assert(g_x3.t[target_slot][0].len_samps==expected_frames*2);
 for(unsigned s=1;s<4;s++)assert(!g_meta.slot[target_slot].present[s]);
 assert(!memcmp(disk_index+6*512,original_index+6*512,2*512));
 for(unsigned slot=0;slot<16;slot++)if(slot!=target_slot){
  assert(!memcmp(disk_index+8+44*slot,original_index+8+44*slot,44));
  assert(!memcmp(disk_index+716+16*slot,original_index+716+16*slot,16));
  assert(!memcmp(disk_index+1536+16+64*slot,original_index+1536+16+64*slot,64));
 }
 assert(!memcmp(disk_index+1536+1040,original_index+1536+1040,496));
}
static void save_case(void){
 for(unsigned slot=0;slot<16;slot++){
  reset(slot);start_take();produce(13);drain_stopped();
  atomic_store(&dd.deck[0].playing,0);dual_record_service();assert(atomic_load(&dd_record_state)==3);
  atomic_store(&dd.deck[1].playing,0);dual_record_service();assert(atomic_load(&dd_record_state)==3);
  assert(!flush_calls&&!metadata_writes);pause_and_save();check_saved();
 }
}
static void failure_case(void){
 reset(11);start_take();produce(13);drain_stopped();pause_and_save();check_saved();unsigned operations=save_ops;
 assert(operations>10);
 for(unsigned op=1;op<=operations;op++){
  reset(11);start_take();produce(13);drain_stopped();fail_save_op=op;pause_and_save();
  assert(atomic_load(&dd_record_state)==5&&atomic_load(&dd_record_error)==5);
  /* A successful final index write is already externally visible, even if
   * its following flush/readback fails. This is the format's real limit. */
  bool visible=disk_index[16+44*target_slot]!=0;
  assert(visible==final_write_seen);assert(visible==(op>operations-2));
  assert(dual_song_present(target_slot)==visible);
  if(!visible)assert(!g_meta.slot[target_slot].present[0]);
 }
 printf("save IO failure points: %u; final-write flush/readback visibility limits: 2\n",operations);
}
static void guards_case(void){
 reset(3);start_take();produce(2);unsigned before=data_writes;
 for(unsigned i=0;i<70&&atomic_load(&dd_rec.state)!=BONSAI_RECORD_FAILED;i++)audio_block();
 assert(atomic_load(&dd_rec.error)==BONSAI_RECORD_OVERFLOW);dual_record_service();
 assert(atomic_load(&dd_record_state)==5&&data_writes==before&&!metadata_writes);
 assert(!memcmp(disk_index,original_index,sizeof(disk_index)));
 reset(3);start_take();fail_data_write=2;produce(4);
 assert(atomic_load(&dd_record_state)==5&&atomic_load(&dd_record_error)==4);
 assert(atomic_load(&dd_rec.committed_sectors)==1&&!metadata_writes);
 assert(!memcmp(disk_index,original_index,sizeof(disk_index)));
 reset(3);start_take();produce(13);drain_stopped();
 assert(fseek(audio_file,100,SEEK_SET)==0);int byte=fgetc(audio_file);assert(byte!=EOF);
 assert(fseek(audio_file,100,SEEK_SET)==0);assert(fputc(byte^1,audio_file)!=EOF);
 pause_and_save();assert(atomic_load(&dd_record_state)==5&&!metadata_writes&&!final_write_seen);
 assert(!memcmp(disk_index,original_index,sizeof(disk_index)));
}
static void admission_case(void){
 for(unsigned mode=0;mode<5;mode++){
  reset(3);
  if(mode==0)atomic_store(&dd_storage_ok,0);
  if(mode==1)g_x3_ok=false;
  if(mode==2)g_xfer_mode=1;
  if(mode==3)dd_capacity=trk_blk(3,0)+TRACK_BLOCKS-1;
  if(mode==4)g_meta.slot[3].present[0]=1;
  atomic_store(&dd_record_toggle,true);dual_record_service();
  assert(atomic_load(&dd_record_state)==5&&atomic_load(&dd_record_slot)==-1&&!data_writes&&!metadata_writes);
 }
 reset(3);start_take();drain_stopped();pause_and_save();
 assert(atomic_load(&dd_record_state)==5&&!data_writes&&!metadata_writes); /* Empty take cannot publish. */
 /* Failed takes are restartable; successful ones reserve the next empty
  * slot instead of replacing the previous recording. */
 reset(3);start_take();fail_data_write=1;produce(2);assert(atomic_load(&dd_record_state)==5);
 fail_data_write=data_writes=expected_frames=expected_source=0;rewind(audio_file);rewind(expected_file);
 start_take();produce(13);drain_stopped();pause_and_save();check_saved();
 memset(g_meta.slot[4].present,0,4);atomic_store(&dd_record_toggle,true);dual_record_service();
 assert(atomic_load(&dd_record_state)==1&&atomic_load(&dd_record_slot)==4);
}
static void maximum_case(void){
 reset(11);start_take();
 while(atomic_load(&dd_record_state)<3){audio_block();dual_record_service();}
 assert(expected_source==MAX_LOOP_SAMPLES);
 assert(expected_frames==BONSAI_LIBRARY_MAX_NATIVE_FRAMES);
 assert(atomic_load(&dd_record_state)==3&&!metadata_writes&&!flush_calls);
 pause_and_save();check_saved();
 printf("maximum take: %u source frames, %u native frames, %u sectors; ring peak %u/%u frames; recorder state %zu bytes\n",expected_source,expected_frames,data_writes,high_water,BONSAI_RECORD_RING,sizeof(dd_rec));
}
static void write_file(const char *directory,const char *name,const uint8_t *data,size_t size){
 char path[1024];assert(snprintf(path,sizeof(path),"%s/%s",directory,name)>0);
 FILE *out=fopen(path,"wb");assert(out&&fwrite(data,1,size,out)==size);assert(!fclose(out));
}
static void copy_file(const char *directory,const char *name,FILE *source){
 char path[1024];assert(snprintf(path,sizeof(path),"%s/%s",directory,name)>0);FILE *out=fopen(path,"wb");assert(out);
 rewind(source);uint8_t block[4096];size_t n;while((n=fread(block,1,sizeof(block),source)))assert(fwrite(block,1,n,out)==n);
 assert(!ferror(source));assert(!fclose(out));
}
int main(int argc,char **argv){
 assert(argc>=2);
 if(!strcmp(argv[1],"save"))save_case();
 else if(!strcmp(argv[1],"failures"))failure_case();
 else if(!strcmp(argv[1],"guards"))guards_case();
 else if(!strcmp(argv[1],"admission"))admission_case();
 else if(!strcmp(argv[1],"maximum"))maximum_case();
 else if(!strcmp(argv[1],"fixture")){
  assert(argc==3);reset(11);start_take();produce(13);drain_stopped();pause_and_save();check_saved();
 }else return 2;
 if(argc==3){
  write_file(argv[2],"meta.bin",disk_index,1024);write_file(argv[2],"x3.bin",disk_index+1536,1536);
  copy_file(argv[2],"audio.bin",audio_file);copy_file(argv[2],"expected.pcm",expected_file);
 }
 close_fixture();puts("PASS: actual recording adapter host replay");return 0;
}
