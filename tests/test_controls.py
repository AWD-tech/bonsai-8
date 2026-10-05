"""Replay physical ADC gestures through the actual firmware control loop.

Only hardware/time/IO boundaries are stubbed. The compiled functions are
extracted from the adapter, so a copied interpretation of controls cannot
silently pass while the on-device loop still behaves differently.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    import re
    match = re.search(r'^static [^\n]*\b' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise AssertionError('Missing actual control function: ' + name)
    start = match.start()
    brace = source.index('{', match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "dual_engine.h"
#include "bonsai_sync.h"
#define NUM_SLOTS 16
#define MIN(a,b) ((a)<(b)?(a):(b))
#define CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
enum track_key {TRK_NONE=-1,TRK_1,TRK_2,TRK_3,TRK_4,TRK_PLAY};
enum vol_btn {VOL_NONE=-1,VOL_TEMPO_DOWN,VOL_DOWN,VOL_TEMPO_UP,VOL_UP,VOL_BOTH};
enum {LAD_TRACKS,LAD_VOL,LAD_FADER0};
static int adc_ladder[6]={LAD_TRACKS,LAD_VOL,LAD_FADER0,LAD_FADER0+1,LAD_FADER0+2,LAD_FADER0+3};
static struct dd_engine dd;
static _Atomic int dd_request[2],dd_slot[2];
__BROWSE_DECLARATIONS__
static _Atomic unsigned dd_record_saving,dd_record_toggle,dd_record_state,dd_storage_ok;
static _Atomic uint32_t dd_position[2],dd_length[2],dd_sync_epoch[2];
static _Atomic uint32_t dd_sync_bpm[2],dd_sync_valid[2],dd_sync_state[2],dd_sync_result[2],dd_sync_taps[2];
static struct bonsai_sync dd_sync;
static uint32_t dd_sync_feedback_until;
static unsigned dd_sync_feedback_deck;
static bool dd_halted;
static uint64_t g_sample_clock;
static uint8_t dd_selected;
static struct dd_pickup dd_pick[4];
static uint16_t dd_button_mask,dd_physical[4];
static int g_hp_on,g_hp_in,g_xfer_mode;
static uint16_t library;
static jmp_buf done;
static unsigned frame,frames;
static int held[2000][3]; /* FUNCTION, track ladder, wheel/volume ladder */
static int consume_at=-1,complete_at=-1,pending_load=-1,request_seen[64],request_count;
static int previous_request=-1;
static unsigned record_trigger_count;
static bool recording;
static uint32_t fake_fraction[2];
static bool fake_tempo_lag;
static uint32_t fake_actual[2];
static void k_sched_lock(void){}
static void k_sched_unlock(void){}
/* The real control functions/math are compiled below. Only the DSP request
 * and physical clock are boundary stubs; separate PCM tests cover the DSP. */
static bool harness_set_tempo(struct dd_deck *d,uint32_t tempo){
 if(tempo<32768||tempo>81920)return false;
 atomic_store(&d->speed,tempo);return true;
}
static uint32_t harness_tempo_actual(const struct dd_deck *d){
 return fake_tempo_lag?fake_actual[d==&dd.deck[1]]:atomic_load(&d->speed);
}
#define dd_set_tempo harness_set_tempo
#define dd_tempo_actual harness_tempo_actual
static int64_t k_uptime_get(void){return 1000+(int64_t)frame*8;}
static uint32_t k_uptime_get_32(void){return (uint32_t)k_uptime_get();}
static bool pwr_pressed(void){return held[frame][0]!=0;}
static int ladder_read(const int *channel){return *channel==LAD_TRACKS?held[frame][1]:*channel==LAD_VOL?held[frame][2]:3700;}
static bool dual_record_busy(void){return recording;}
static bool dual_song_present(unsigned slot){return slot<NUM_SLOTS&&(library&(1u<<slot));}
static void feed_wdt(void){}
static void dual_recovery_check(void){}
static uint16_t dd_ui_tracks(int raw){(void)raw;return 0;}
static int hp_detect_connected(void){return -1;}
static void tas_set_speaker(int on){(void)on;}
static void all_off(void){}
static void status_level(uint32_t value){(void)value;}
static void led_on(unsigned i){(void)i;}
static void track_led_on(unsigned i){(void)i;}
static void track_led_off(unsigned i){(void)i;}
static void dual_mirror_capture(uint32_t now){(void)now;}
static void power_off(void){assert(!"FUNCTION gesture unexpectedly powers off");}
static void k_msleep(int duration){
 assert(duration==8);
 g_sample_clock+=384;
 for(unsigned k=0;k<2;k++)if(atomic_load(&dd.deck[k].playing)&&!dd.deck[k].starved){
  uint32_t advance=192*harness_tempo_actual(&dd.deck[k])+fake_fraction[k];
  fake_fraction[k]=advance&65535;advance>>=16;
  atomic_fetch_add(&dd.deck[k].source_clock,advance);
  atomic_store(&dd_position[k],(atomic_load(&dd_position[k])+advance)%atomic_load(&dd_length[k]));
 }
 if(atomic_exchange(&dd_record_toggle,0))record_trigger_count++;
 int request=atomic_load(&dd_request[dd_selected]);
 if(request>=0&&request!=previous_request){request_seen[request_count++]=request;previous_request=request;}
 /* Exercise the actual storage hand-off: it consumes the request before
  * dual_load has finished its fade/reads and published the new dd_slot. */
 if((int)frame==consume_at){
  pending_load=atomic_exchange(&dd_request[dd_selected],-1);previous_request=-1;
  atomic_store(&dd.deck[dd_selected].playing,0);
 }
 if((int)frame==complete_at)atomic_store(&dd_slot[dd_selected],pending_load);
 if(++frame>=frames)longjmp(done,1);
}
static void reset(unsigned slots,unsigned selected,int slot_a,int slot_b){
 dd_init(&dd);
 fake_tempo_lag=false;fake_actual[0]=fake_actual[1]=65536;
 assert(bonsai_sync_init(&dd_sync,32768,81920));
 for(unsigned k=0;k<2;k++){
  atomic_store(&dd_sync_epoch[k],k+1);atomic_store(&dd_length[k],1200000);atomic_store(&dd_position[k],0);
  assert(bonsai_sync_song(&dd_sync,k,k+1,1200000));fake_fraction[k]=0;
  atomic_store(&dd_sync_bpm[k],0);atomic_store(&dd_sync_valid[k],0);
  atomic_store(&dd_sync_state[k],0);atomic_store(&dd_sync_result[k],BONSAI_SYNC_MANUAL);atomic_store(&dd_sync_taps[k],0);
 }
 g_sample_clock=0;dd_halted=false;dd_sync_feedback_until=0;atomic_store(&dd_record_state,0);
 dd_selected=(uint8_t)selected;library=(uint16_t)slots;frame=frames=0;
 consume_at=-1;complete_at=-1;pending_load=-1;request_count=0;previous_request=-1;recording=false;
 atomic_store(&dd_request[0],-1);atomic_store(&dd_request[1],-1);
 atomic_store(&dd_slot[0],slot_a);atomic_store(&dd_slot[1],slot_b);
 __BROWSE_INIT__
 atomic_store(&dd_storage_ok,1);atomic_store(&dd_record_saving,0);
 atomic_store(&dd_record_toggle,0);record_trigger_count=0;
 atomic_store(&dd.deck[selected^1].playing,1);
 for(unsigned s=0;s<4;s++)dd_pick[s]=(struct dd_pickup){-1,true};
}
static void hold(unsigned count,int fn,int track,int wheel){
 assert(frames+count<2000);
 for(unsigned n=0;n<count;n++){held[frames][0]=fn;held[frames][1]=track;held[frames][2]=wheel;frames++;}
}
static void run(void);
'''

TESTS = r'''
static void run(void){if(!setjmp(done))dual_controls();}
static void test_queued_cursor(void){
 reset(0x1f,1,0,1);
 hold(5,1,0,0);hold(6,1,0,1220);hold(5,1,0,0);
 /* first browse event settles at frame 7; consume it while the loaded
  * slot remains Song 2, as happens during the asynchronous storage load. */
 consume_at=8;
 hold(6,1,0,1220);hold(5,1,0,0);hold(6,1,0,1220);hold(5,0,0,0);
 run();
 assert(pending_load==2);
 assert(request_count==3&&"Repeated FUNCTION+wheel taps must browse beyond the next song while a load is pending");
 assert(request_seen[0]==2&&request_seen[1]==3&&request_seen[2]==4);
 assert(dd_selected==1&&"Releasing a used FUNCTION chord must not switch decks");
 assert(atomic_load(&dd.deck[0].playing)==1);
 assert(atomic_load(&dd_slot[0])==0);
}
static void test_every_slot_and_direction(void){
 reset(0xffff,1,0,1);
 for(unsigned pass=0;pass<3;pass++)for(unsigned n=0;n<NUM_SLOTS;n++){
  dual_choose_song(1);
  int target=atomic_exchange(&dd_request[1],-1);
  assert(target==(int)((2+n)%NUM_SLOTS));
  /* Publish an older consumed load after newer choices have been made. */
  if(n==3)atomic_store(&dd_slot[1],2);
 }
 for(unsigned n=0;n<NUM_SLOTS*2;n++){
  dual_choose_song(-1);
  assert(atomic_exchange(&dd_request[1],-1)==(int)((NUM_SLOTS-n%NUM_SLOTS)%NUM_SLOTS));
 }
 assert(atomic_load(&dd.deck[0].playing)==1&&atomic_load(&dd_slot[0])==0);
}
static void test_sparse_wrap(void){
 reset((1u<<0)|(1u<<7)|(1u<<15),0,15,7);
 hold(5,1,0,0);hold(6,1,0,1220);hold(5,1,0,0);
 hold(6,1,0,1220);hold(5,1,0,0);hold(6,1,0,400);hold(5,0,0,0);
 run();assert(request_count==3);
 assert(request_seen[0]==0&&request_seen[1]==7&&request_seen[2]==0);
 assert(atomic_load(&dd.deck[1].playing)==1);
}
static void test_repeated_hold(void){
 reset(0xffff,1,0,1);
 hold(5,1,0,0);hold(190,1,0,1220);hold(6,1,0,0);hold(6,0,0,0);
 consume_at=8;run();
 assert(request_count>=3);
 for(int i=0;i<request_count;i++)assert(request_seen[i]==(i+2)%NUM_SLOTS);
 assert(dd_selected==1&&atomic_load(&dd.deck[0].playing)==1);
}
static void test_modifier_release(void){
 reset(0x0f,1,0,1);
 hold(5,1,0,0);hold(6,1,0,1220);
 /* Release FUNCTION first, with wheel still held past its repeat time. */
 hold(100,0,0,1220);hold(6,0,0,0);
 run();assert(dd_selected==1);
 assert(atomic_load(&dd.deck[1].speed)==65536&&"Releasing FUNCTION first must not turn song browsing into pitch changes");
 assert(atomic_load(&dd.deck[0].playing)==1);
}
static void test_plain_function_and_pitch(void){
 reset(0x0f,0,0,1);
 hold(6,1,0,0);hold(6,0,0,0); /* normal FUNCTION tap selects B */
 hold(6,0,0,1220);hold(6,0,0,0);
 run();assert(dd_selected==1);
 assert(atomic_load(&dd.deck[1].speed)==65536+655);
 assert(atomic_load(&dd.deck[0].speed)==65536);
 assert(atomic_load(&dd.deck[1].playing)==1);
 reset(0x0f,1,0,1);
 hold(5,1,0,0);hold(6,1,0,1220);hold(8,1,0,0);hold(6,0,0,0);
 /* A new, unmodified wheel press works after releasing the browse chord. */
 hold(6,0,0,1220);hold(6,0,0,0);run();
 assert(dd_selected==1&&atomic_load(&dd.deck[1].speed)==65536+655);
 assert(atomic_load(&dd.deck[0].playing)==1);
}
static void test_reserved_stem_chord_ignores_wheel(void){
 reset(0xffff,0,0,1);
 hold(5,1,200,0);hold(6,1,200,1220);hold(6,1,200,0);
 hold(6,1,200,1220);hold(6,1,200,0);hold(6,0,0,0);
 run();assert(request_count==0);
 assert(atomic_load(&dd.deck[0].speed)==65536);
 assert(atomic_load(&dd.deck[0].mute_mask)==0&&dd_selected==0);
 assert(atomic_load(&dd.deck[1].playing)==1);
}
static void test_record_busy_and_empty(void){
 reset(0xffff,0,0,1);recording=true;
 hold(5,1,0,0);hold(6,1,0,1220);hold(6,0,0,0);run();
 assert(request_count==0&&dd_selected==0);
 reset(0,0,-1,-1);
 hold(5,1,0,0);hold(6,1,0,1220);hold(6,0,0,0);run();assert(request_count==0);
}
static void test_record_chord_requires_full_release(void){
 reset(0xffff,0,0,1);
 hold(5,0,0,0);
 hold(140,1,0,1998);
 /* A debounced volume-ladder gap while FUNCTION remains held must not
  * turn one long recording gesture into a second stop gesture. */
 hold(4,1,0,0);hold(140,1,0,1998);
 hold(6,0,0,0);
 run();assert(record_trigger_count==1);
 assert(dd_selected==0);

 reset(0xffff,0,0,1);
 hold(5,0,0,0);hold(140,1,0,1998);hold(6,0,0,0);
 /* Releasing FUNCTION and both volume buttons re-arms an intentional
  * second gesture, including when the volume buttons release first. */
 hold(140,1,0,1998);hold(6,1,0,0);hold(6,0,0,0);
 run();assert(record_trigger_count==2);
 assert(dd_selected==0);
}
static void test_four_physical_taps_teach_only_selected_deck(void){
 reset(0x1f,0,0,1);atomic_store(&dd.deck[0].playing,1);
 hold(5,1,0,0);
 for(unsigned tap=0;tap<4;tap++){hold(6,1,200,0);hold(56,1,0,0);}
 hold(6,0,0,0);run();
 assert(atomic_load(&dd_sync_valid[0])==1&&atomic_load(&dd_sync_valid[1])==0);
 assert(atomic_load(&dd_sync_bpm[0])>118000&&atomic_load(&dd_sync_bpm[0])<123000);
 assert(atomic_load(&dd_sync_taps[0])==4&&atomic_load(&dd_sync_state[0])==BONSAI_SYNC_MODE_MANUAL);
 assert(dd_selected==0&&atomic_load(&dd.deck[0].mute_mask)==0&&atomic_load(&dd.deck[1].mute_mask)==0);
 assert(request_count==0);
}
static void test_sync_chord_and_manual_tempo(void){
 reset(0x1f,1,0,1);atomic_store(&dd.deck[1].playing,1);
 assert(bonsai_sync_set_grid(&dd_sync,0,1,120000,0));assert(bonsai_sync_set_grid(&dd_sync,1,2,150000,0));
 hold(5,1,0,0);hold(6,1,400,0);hold(6,0,0,0);run();
 assert(dd_sync.follower==1&&atomic_load(&dd.deck[1].speed)<65536);
 assert(atomic_load(&dd.deck[0].speed)==65536&&atomic_load(&dd.deck[1].mute_mask)==0&&dd_selected==1);
 assert(request_count==0&&atomic_load(&dd_sync_result[1])==BONSAI_SYNC_OK);
 /* A subsequent unmodified rocker gesture intentionally leaves beat lock. */
 frame=frames=0;hold(6,0,0,1220);hold(6,0,0,0);run();
 assert(dd_sync.follower==-1&&atomic_load(&dd_sync_state[1])==BONSAI_SYNC_MODE_MANUAL);
 assert(!dual_sync_leds(k_uptime_get_32())&&"manual tempo must restore normal deck lights");
}
static void test_nudge_release_cannot_browse_or_change_manual_tempo(void){
 reset(0xffff,1,0,1);atomic_store(&dd.deck[1].playing,1);
 assert(bonsai_sync_set_grid(&dd_sync,0,1,120000,0));assert(bonsai_sync_set_grid(&dd_sync,1,2,120000,0));
 dual_sync_poll();assert(bonsai_sync_follow(&dd_sync,1)==BONSAI_SYNC_OK);
 hold(5,1,700,0);hold(6,1,700,1220);
 /* Track 3 releases first, but wheel+FUNCTION remain held into repeats. */
 hold(90,1,0,1220);hold(6,1,0,0);hold(6,0,0,0);run();
 assert(dd_sync.phase_offset_q32==BONSAI_SYNC_NUDGE_Q32&&request_count==0&&dd_selected==1);
 assert(atomic_load(&dd.deck[0].speed)==65536&&atomic_load(&dd.deck[1].mute_mask)==0);
}
static void test_unknown_and_starved_sync_never_report_locked(void){
 reset(0x1f,0,0,1);atomic_store(&dd.deck[0].playing,1);
 hold(5,1,0,0);hold(6,1,400,0);hold(6,0,0,0);run();
 assert(atomic_load(&dd_sync_result[0])==BONSAI_SYNC_UNKNOWN_GRID);
 assert(atomic_load(&dd_sync_state[0])==BONSAI_SYNC_ERROR&&dd_sync.follower==-1);
 reset(0x1f,0,0,1);atomic_store(&dd.deck[0].playing,1);
 assert(bonsai_sync_set_grid(&dd_sync,0,1,120000,0));assert(bonsai_sync_set_grid(&dd_sync,1,2,120000,0));
 dd.deck[1].starved=true;
 hold(5,1,0,0);hold(6,1,400,0);hold(6,0,0,0);run();
 assert(atomic_load(&dd_sync_result[0])==BONSAI_SYNC_NOT_PLAYING);
 assert(atomic_load(&dd_sync_state[0])==BONSAI_SYNC_ERROR);
}
static void test_sync_uses_actual_tempo_when_dsp_request_is_pending(void){
 reset(0x1f,1,0,1);atomic_store(&dd.deck[1].playing,1);
 assert(bonsai_sync_set_grid(&dd_sync,0,1,120000,0));assert(bonsai_sync_set_grid(&dd_sync,1,2,150000,0));
 fake_tempo_lag=true;fake_actual[0]=49152;fake_actual[1]=65536;
 /* Pending requested A=1.25x differs from its currently audible .75x.
  * The follower must target .6x, not the pending request's 1x. */
 atomic_store(&dd.deck[0].speed,81920);atomic_store(&dd.deck[1].speed,32768);
 hold(5,1,0,0);hold(6,1,400,0);hold(6,0,0,0);run();
 assert(dd_sync.deck[0].position.tempo_q16==49152);
 assert(dd_sync.deck[1].position.tempo_q16==65536);
 assert(atomic_load(&dd.deck[1].speed)>37000&&atomic_load(&dd.deck[1].speed)<42000);
 assert(atomic_load(&dd.deck[0].speed)==81920);
}
static void test_unloaded_or_unsupported_length_clears_old_grid(void){
 reset(0x1f,1,0,1);atomic_store(&dd.deck[1].playing,1);
 assert(bonsai_sync_set_grid(&dd_sync,0,1,120000,0));assert(bonsai_sync_set_grid(&dd_sync,1,2,120000,0));
 dual_sync_poll();assert(bonsai_sync_follow(&dd_sync,1)==BONSAI_SYNC_OK);
 atomic_store(&dd_length[0],BONSAI_SYNC_MAX_FRAMES+1);dual_sync_poll();
 assert(!atomic_load(&dd_sync_valid[0])&&atomic_load(&dd_sync_valid[1]));
 assert(dd_sync.follower==-1&&atomic_load(&dd_sync_state[1])==BONSAI_SYNC_MODE_MANUAL);
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"all"))test_every_slot_and_direction();
 else if(!strcmp(argv[1],"queued"))test_queued_cursor();
 else if(!strcmp(argv[1],"sparse"))test_sparse_wrap();
 else if(!strcmp(argv[1],"held"))test_repeated_hold();
 else if(!strcmp(argv[1],"release"))test_modifier_release();
 else if(!strcmp(argv[1],"plain"))test_plain_function_and_pitch();
 else if(!strcmp(argv[1],"effects"))test_reserved_stem_chord_ignores_wheel();
 else if(!strcmp(argv[1],"busy"))test_record_busy_and_empty();
 else if(!strcmp(argv[1],"recording"))test_record_chord_requires_full_release();
 else if(!strcmp(argv[1],"taps"))test_four_physical_taps_teach_only_selected_deck();
 else if(!strcmp(argv[1],"sync"))test_sync_chord_and_manual_tempo();
 else if(!strcmp(argv[1],"nudge"))test_nudge_release_cannot_browse_or_change_manual_tempo();
 else if(!strcmp(argv[1],"unknown"))test_unknown_and_starved_sync_never_report_locked();
 else if(!strcmp(argv[1],"actual"))test_sync_uses_actual_tempo_when_dsp_request_is_pending();
 else if(!strcmp(argv[1],"length"))test_unloaded_or_unsupported_length_clears_old_grid();
 else return 2;
 puts("actual physical control replay passed");return 0;
}
'''


class PhysicalControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        adapter = (ROOT/'firmware/src/dual_firmware.inc').read_text()
        main = (ROOT/'firmware/src/main.c').read_text()
        functions = [function(main, 'decode_vol')]
        functions += [function(adapter, name) for name in
                      ('dual_sync_publish','dual_sync_feedback','dual_sync_poll',
                       'dual_sync_tap','dual_sync_follow','dual_sync_nudge',
                       'dual_sync_manual_tempo','dual_sync_leds')]
        # New cursor helper is included once the production fix introduces it.
        if 'static void dual_request_song(' in adapter:
            functions.append(function(adapter, 'dual_request_song'))
        functions += [function(adapter, name) for name in
                      ('dual_choose_song', 'dual_track_key', 'dual_controls')]
        fixed = 'dd_browse_slot[2]' in adapter
        source = HARNESS.replace('__BROWSE_DECLARATIONS__',
                                 'static _Atomic int dd_browse_slot[2];' if fixed else '')
        source = source.replace('__BROWSE_INIT__',
                                'atomic_store(&dd_browse_slot[0],slot_a);atomic_store(&dd_browse_slot[1],slot_b);' if fixed else '')
        src = Path(cls.tmp.name)/'controls.c'
        src.write_text(source+'\n'+'\n'.join(functions)+'\n'+TESTS)
        cls.exe = Path(cls.tmp.name)/'controls'
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-I',str(ROOT/'firmware/src'),
                        str(src),str(ROOT/'firmware/src/dual_engine.c'),str(ROOT/'firmware/src/bonsai_stretch.c'),
                        str(ROOT/'firmware/src/bonsai_sync.c'),'-lm',
                        '-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def replay(self, case):
        result = subprocess.run([str(self.exe),case],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)

    def test_all_16_slots_both_directions(self): self.replay('all')
    def test_next_beyond_loading_slot(self): self.replay('queued')
    def test_previous_next_skip_empty_and_wrap(self): self.replay('sparse')
    def test_hold_function_repeats_browse(self): self.replay('held')
    def test_function_release_does_not_change_pitch(self): self.replay('release')
    def test_normal_deck_tap_and_pitch_after_browsing(self): self.replay('plain')
    def test_effect_wheel_stays_separate(self): self.replay('effects')
    def test_recording_and_empty_library_do_not_browse(self): self.replay('busy')
    def test_record_chord_requires_full_release(self): self.replay('recording')
    def test_four_physical_taps_teach_selected_source_grid(self): self.replay('taps')
    def test_sync_chord_and_manual_tempo_exit(self): self.replay('sync')
    def test_nudge_releases_do_not_browse(self): self.replay('nudge')
    def test_unknown_and_starved_decks_do_not_lock(self): self.replay('unknown')
    def test_sync_snapshots_actual_tempo_not_pending_request(self): self.replay('actual')
    def test_unsupported_or_unloaded_length_clears_grid(self): self.replay('length')
