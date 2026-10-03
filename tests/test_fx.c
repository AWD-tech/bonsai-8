#include "bonsai_fx.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct bonsai_fx fx;
static const struct dd_frame silence = {0,0};
static void configure(unsigned voice, enum bonsai_fx_type type, unsigned amount)
{
 assert(bonsai_fx_set(&fx, voice, type, amount));
 bonsai_fx_begin_block(&fx);
}
static void warm(unsigned voice)
{
 for (unsigned i = 0; i < 4800; ++i)
  (void)bonsai_fx_process(&fx.voice[voice], silence);
}
static void test_bypass_and_bounds(void)
{
 bonsai_fx_init(&fx);
 assert(sizeof(fx) <= 18u * 1024u);
 assert(!bonsai_fx_set(&fx, 8, BONSAI_FX_ECHO, 1));
 assert(!bonsai_fx_set(&fx, 0, (enum bonsai_fx_type)-1, 1));
 assert(!bonsai_fx_set(&fx, 0, (enum bonsai_fx_type)4, 1));
 for (unsigned voice = 0; voice < 8; ++voice) {
  for (int sample = INT16_MIN; sample <= INT16_MAX; ++sample) {
   struct dd_frame in = {(int16_t)sample, (int16_t)(-sample - 1)};
   struct dd_frame out = bonsai_fx_process(&fx.voice[voice], in);
   assert(out.l == in.l && out.r == in.r);
  }
 }
 configure(0, BONSAI_FX_ECHO, 99999);
 assert(fx.voice[0].requested_amount == 256);
 configure(0, BONSAI_FX_NONE, 256);
 assert(fx.voice[0].requested_amount == 0);
}
static void test_filter_response_and_stereo(void)
{
 bonsai_fx_init(&fx); configure(0, BONSAI_FX_FILTER, 256); warm(0);
 int64_t energy = 0;
 for (unsigned i = 0; i < 2400; ++i) {
  int16_t a = i & 1 ? 24000 : -24000;
  struct dd_frame out = bonsai_fx_process(&fx.voice[0], (struct dd_frame){a,-a});
  /* Independent stereo channels preserve opposite polarity; no mono collapse. */
  assert(abs(out.l + out.r) <= 1);
  if (i > 100) energy += abs(out.l);
 }
 assert(energy / 2299 < 400); /* low-pass strongly rejects Nyquist input */
 for (unsigned i = 0; i < 4800; ++i)
  (void)bonsai_fx_process(&fx.voice[0], (struct dd_frame){12000,-8000});
 struct dd_frame dc = bonsai_fx_process(&fx.voice[0], (struct dd_frame){12000,-8000});
 assert(abs(dc.l - 12000) <= 1 && abs(dc.r + 8000) <= 1);
 /* Settings survive arbitrary later blocks and simulated button release. */
 for (unsigned i = 0; i < 4; ++i) bonsai_fx_begin_block(&fx);
 assert(fx.voice[0].requested_type == BONSAI_FX_FILTER);
 configure(0, BONSAI_FX_FILTER, 0);
 for (unsigned i = 0; i < 256; ++i)
  (void)bonsai_fx_process(&fx.voice[0], silence);
 struct dd_frame in = {-32768,32767};
 struct dd_frame out = bonsai_fx_process(&fx.voice[0], in);
 assert(out.l == in.l && out.r == in.r);
}
static void test_echo_impulse_timing_and_decay(void)
{
 bonsai_fx_init(&fx); configure(0, BONSAI_FX_ECHO, 256); warm(0);
 unsigned first = 0;
 int64_t first_energy = 0, second_energy = 0;
 int tail_peak = 0;
 for (unsigned i = 0; i < 192000; ++i) {
  struct dd_frame in = i < 12 ? (struct dd_frame){12000,12000} : silence;
  struct dd_frame out = bonsai_fx_process(&fx.voice[0], in);
  assert(out.l == out.r);
  if (i >= 12 && out.l && !first) first = i;
  if (i >= 11990 && i < 12060) first_energy += abs(out.l);
  if (i >= 23990 && i < 24060) second_energy += abs(out.l);
  if (i > 144000 && abs(out.l) > tail_peak) tail_peak = abs(out.l);
 }
 assert(first >= 12000 && first <= 12024);
 assert(first_energy > 50000 && first_energy < 80000);
 assert(second_energy > first_energy * 45 / 100);
 assert(second_energy < first_energy * 55 / 100);
 assert(tail_peak <= 4);
 assert(fx.voice[0].clipped == 0);
}
static void test_reverb_tail_and_isolation(void)
{
 bonsai_fx_init(&fx);
 for (unsigned voice = 0; voice < 8; ++voice) {
  configure(voice, BONSAI_FX_REVERB, 256); warm(voice);
 }
 unsigned first = 0, nonzero = 0;
 int tail_peak = 0;
 for (unsigned i = 0; i < 144000; ++i) {
  struct dd_frame in = i < 12 ? (struct dd_frame){24000,24000} : silence;
  struct dd_frame out = bonsai_fx_process(&fx.voice[7], in);
  if (i >= 12 && out.l) { if (!first) first = i; ++nonzero; }
  if (i > 96000 && abs(out.l) > tail_peak) tail_peak = abs(out.l);
  for (unsigned voice = 0; voice < 7; ++voice) {
   struct dd_frame other = bonsai_fx_process(&fx.voice[voice], silence);
   assert(other.l == 0 && other.r == 0);
  }
 }
 assert(first >= 149u * 12u && first <= 149u * 12u + 24u);
 assert(nonzero > 1000);
 assert(tail_peak < 8);
 assert(fx.voice[7].clipped == 0);
}
static void test_incremental_reset_and_switch(void)
{
 bonsai_fx_init(&fx);
 for (unsigned i = 0; i < BONSAI_FX_HISTORY; ++i) fx.voice[0].history[i] = 1234;
 configure(0, BONSAI_FX_ECHO, 256);
 (void)bonsai_fx_process(&fx.voice[0], silence);
 assert(fx.voice[0].history[0] == 0);
 assert(fx.voice[0].history[1] == 1234); /* no full callback-time clear */
 assert(fx.voice[0].history[1023] == 1234);
 warm(0);
 for (unsigned i = 0; i < 24000; ++i)
  (void)bonsai_fx_process(&fx.voice[0], (struct dd_frame){16000,16000});
 configure(0, BONSAI_FX_REVERB, 256);
 for (unsigned i = 0; i < 256; ++i)
  (void)bonsai_fx_process(&fx.voice[0], silence);
 /* Once old wet has faded out, no stale echo bytes may become reverb taps. */
 for (unsigned i = 0; i < 40000; ++i) {
  struct dd_frame out = bonsai_fx_process(&fx.voice[0], silence);
  assert(out.l == 0 && out.r == 0);
 }
 configure(0, BONSAI_FX_REVERB, 0);
 for (unsigned i = 0; i < 256; ++i)
  (void)bonsai_fx_process(&fx.voice[0], silence);
 struct dd_frame out = bonsai_fx_process(&fx.voice[0], (struct dd_frame){555,-777});
 assert(out.l == 555 && out.r == -777);
}
static void test_full_scale_stability(void)
{
 for (unsigned type = BONSAI_FX_FILTER; type <= BONSAI_FX_REVERB; ++type) {
  bonsai_fx_init(&fx); configure(0, (enum bonsai_fx_type)type, 256); warm(0);
  for (unsigned i = 0; i < 192000; ++i) {
   int16_t level = i < 96000 ? INT16_MAX : INT16_MIN;
   struct dd_frame out = bonsai_fx_process(&fx.voice[0], (struct dd_frame){level,level});
   if (i > 90000 && i < 96000) assert(out.l > 32000);
   if (i > 185000) assert(out.l < -32000);
  }
  int tail_peak = 0;
  for (unsigned i = 0; i < 240000; ++i) {
   struct dd_frame out = bonsai_fx_process(&fx.voice[0], silence);
   if (i > 235000 && abs(out.l) > tail_peak) tail_peak = abs(out.l);
  }
  assert(tail_peak < 8); /* feedback cannot self-oscillate after loud input */
 }
}
static void test_song_reset_preserves_settings_only(void)
{
 bonsai_fx_init(&fx);configure(0,BONSAI_FX_ECHO,200);warm(0);
 configure(1,BONSAI_FX_REVERB,123);
 for(unsigned i=0;i<24000;i++)
  (void)bonsai_fx_process(&fx.voice[0],(struct dd_frame){18000,18000});
 uint32_t saved=atomic_load(&fx.config[0]);
 fx.voice[0].clipped=47;
 fx.voice[1].history[0]=7654;
 unsigned old_position=fx.voice[0].position[0];
 int16_t old_history=fx.voice[0].history[old_position];
 assert(old_history!=0);
 assert(!bonsai_fx_reset_voice(&fx,8));
 assert(bonsai_fx_reset_voice(&fx,0));
 assert(atomic_load(&fx.config[0])==saved);
 assert(fx.voice[0].clipped==47);
 assert(fx.voice[0].history[old_position]==old_history); /* no full clear */
 assert(fx.voice[1].history[0]==7654);
 assert(atomic_load(&fx.config[1])==((BONSAI_FX_REVERB<<16)|123));
 bonsai_fx_begin_block(&fx);
 for(unsigned i=0;i<30000;i++) {
  struct dd_frame out=bonsai_fx_process(&fx.voice[0],silence);
  assert(out.l==0&&out.r==0); /* previous song's echo cannot reappear */
 }
 assert(fx.voice[0].type==BONSAI_FX_ECHO&&fx.voice[0].mix==200);
}
static void test_buffer_matches_sample_processing(void)
{
 static const unsigned counts[]={0,1,7,12,63,64,65,255,256,257};
 static struct bonsai_fx reference;
 struct dd_frame actual[257], expected[257];
 uint32_t random=0x51b08u;
 bonsai_fx_init(&fx);bonsai_fx_init(&reference);
 for(unsigned block=0;block<4000;block++) {
  unsigned n=counts[block%10];
  if(block%29==0) {
   unsigned type=(block/29)%4, amount=(block/116)%3==0?1:256;
   assert(bonsai_fx_set(&fx,0,(enum bonsai_fx_type)type,amount));
   assert(bonsai_fx_set(&reference,0,(enum bonsai_fx_type)type,amount));
  }
  if(block%113==0) {
   assert(bonsai_fx_set(&fx,0,BONSAI_FX_ECHO,0));
   assert(bonsai_fx_set(&reference,0,BONSAI_FX_ECHO,0));
  }
  bonsai_fx_begin_block(&fx);bonsai_fx_begin_block(&reference);
  for(unsigned i=0;i<n;i++) {
   random^=random<<13;random^=random>>17;random^=random<<5;
   actual[i]=(struct dd_frame){(int16_t)random,(int16_t)(random>>16)};
   if(block%7==0) actual[i]=silence;
   if(block%7==1) actual[i]=(struct dd_frame){INT16_MIN,INT16_MAX};
   expected[i]=bonsai_fx_process(&reference.voice[0],actual[i]);
  }
  bonsai_fx_process_buffer(&fx.voice[0],actual,n);
  assert(!memcmp(actual,expected,n*sizeof(*actual)));
  assert(!memcmp(&fx.voice[0],&reference.voice[0],sizeof(fx.voice[0])));
 }
}
int main(void)
{
 test_bypass_and_bounds(); test_filter_response_and_stereo();
 test_echo_impulse_timing_and_decay(); test_reverb_tail_and_isolation();
 test_incremental_reset_and_switch(); test_full_scale_stability();
 test_song_reset_preserves_settings_only();
 test_buffer_matches_sample_processing();
 printf("PASS: stereo bypass/filter, 250 ms echo, reverb decay, isolation, "
        "incremental reset and bounded feedback (%zu bytes)\n", sizeof(fx));
}
