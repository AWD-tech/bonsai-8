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
 assert(sizeof(fx) == 224u);
 assert(!bonsai_fx_set(&fx, 8, BONSAI_FX_FILTER, 1));
 assert(!bonsai_fx_set(&fx, 0, (enum bonsai_fx_type)-1, 1));
 assert(!bonsai_fx_set(&fx, 0, (enum bonsai_fx_type)4, 1));
 for(unsigned type=2;type<=3;type++)for(unsigned amount=0;amount<=256;amount+=256) {
  assert(!bonsai_fx_set(&fx,0,(enum bonsai_fx_type)type,amount));
  assert(atomic_load(&fx.config[0])==0);
 }
 for (unsigned voice = 0; voice < 8; ++voice) {
  for (int sample = INT16_MIN; sample <= INT16_MAX; ++sample) {
   struct dd_frame in = {(int16_t)sample, (int16_t)(-sample - 1)};
   struct dd_wide_frame out = bonsai_fx_process(&fx.voice[voice], in);
   assert(out.l == in.l && out.r == in.r);
  }
 }
 configure(0, BONSAI_FX_FILTER, 99999);
 assert(fx.voice[0].requested_amount == 256);
 uint32_t saved=atomic_load(&fx.config[0]);
 assert(!bonsai_fx_set(&fx,0,(enum bonsai_fx_type)2,0));
 assert(!bonsai_fx_set(&fx,0,(enum bonsai_fx_type)3,256));
 assert(atomic_load(&fx.config[0])==saved);
 configure(0, BONSAI_FX_NONE, 256);
 assert(fx.voice[0].requested_amount == 0);
}
static void test_invalid_direct_configuration_fades_to_bypass(void)
{
 static const uint32_t invalid[]={2u<<16,(3u<<16)|256u,(1u<<16)|257u,UINT32_MAX};
 for(unsigned index=0;index<sizeof(invalid)/sizeof(invalid[0]);index++) {
  bonsai_fx_init(&fx);configure(0,BONSAI_FX_FILTER,128);warm(0);
  atomic_store(&fx.config[0],invalid[index]);bonsai_fx_begin_block(&fx);
  assert(fx.voice[0].requested_type==BONSAI_FX_NONE&&fx.voice[0].requested_amount==0);
  for(unsigned i=0;i<300;i++) {
   struct dd_frame input={INT16_MIN,INT16_MAX};
   struct dd_wide_frame out=bonsai_fx_process(&fx.voice[0],input);
   if(i>=255)assert(out.l==input.l&&out.r==input.r);
  }
  assert(fx.voice[0].type==BONSAI_FX_NONE);
 }
}
static void test_eight_filter_states_are_isolated(void)
{
 bonsai_fx_init(&fx);
 for(unsigned voice=0;voice<8;voice++){configure(voice,BONSAI_FX_FILTER,256);warm(voice);}
 for(unsigned i=0;i<4800;i++) {
  (void)bonsai_fx_process(&fx.voice[7],(struct dd_frame){18000,-12000});
  for(unsigned voice=0;voice<7;voice++) {
   struct dd_wide_frame out=bonsai_fx_process(&fx.voice[voice],silence);
   assert(out.l==0&&out.r==0);
   assert(fx.voice[voice].low_l==0&&fx.voice[voice].low_r==0);
  }
 }
 assert(fx.voice[7].low_l>0&&fx.voice[7].low_r<0);
}
static double response_power(unsigned amount,unsigned frequency)
{
 static const int16_t sine[48]={
  0,1566,3106,4592,6000,7305,8485,9520,10392,11087,11591,11897,
  12000,11897,11591,11087,10392,9520,8485,7305,6000,4592,3106,1566,
  0,-1566,-3106,-4592,-6000,-7305,-8485,-9520,-10392,-11087,-11591,-11897,
  -12000,-11897,-11591,-11087,-10392,-9520,-8485,-7305,-6000,-4592,-3106,-1566
 };
 bonsai_fx_init(&fx);configure(0,BONSAI_FX_FILTER,amount);warm(0);
 uint32_t phase=0,step=frequency*65536u/1000u;
 int64_t source_energy=0,output_energy=0;
 for(unsigned i=0;i<96000;i++) {
  unsigned index=phase>>16,next=(index+1)%48;
  int16_t sample=(int16_t)(sine[index]+((int64_t)sine[next]-sine[index])*(phase&65535u)/65536);
  struct dd_wide_frame out=bonsai_fx_process(&fx.voice[0],(struct dd_frame){sample,-sample});
  assert(abs(out.l+out.r)<=2); /* independent stereo, common phase */
  if(i>=48000){source_energy+=(int32_t)sample*sample;output_energy+=(int64_t)out.l*out.l;}
  phase=(phase+step)%(48u*65536u);
 }
 assert(fx.voice[0].clipped==0);
 return (double)output_energy/source_energy;
}
static void test_filter_response_and_stereo(void)
{
 double low=response_power(256,100),mid=response_power(256,1000);
 double high=response_power(256,12000),bright=response_power(256,20000);
 fprintf(stderr,"HPF power at max: 100Hz %.6f, 1kHz %.6f, 12kHz %.6f, 20kHz %.6f\n",low,mid,high,bright);
 assert(low<0.0003&&mid<0.025);
 assert(high>0.70&&high<0.80&&bright>0.96&&bright<=1.002);
 assert(response_power(1,100)>0.70); /* low endpoint leaves most 100Hz energy */
 for(unsigned amount=1;amount<=256;amount++) {
  bonsai_fx_init(&fx);configure(0,BONSAI_FX_FILTER,amount);warm(0);
  struct dd_wide_frame out={0,0};
  for(unsigned i=0;i<4800;i++)out=bonsai_fx_process(&fx.voice[0],(struct dd_frame){12000,-8000});
  assert(abs(out.l)<=1&&abs(out.r)<=1); /* DC removed independently */
  for(unsigned i=0;i<4800;i++) {
   int16_t sample=i&1?12000:-12000;
   out=bonsai_fx_process(&fx.voice[0],(struct dd_frame){sample,-sample});
   if(i>4000)assert(abs(out.l-sample)<=2&&abs(out.r+sample)<=2);
  }
 }
 for(unsigned i=0;i<4;i++)bonsai_fx_begin_block(&fx);
 assert(fx.voice[0].requested_type==BONSAI_FX_FILTER);
 configure(0,BONSAI_FX_FILTER,0);
 for(unsigned i=0;i<256;i++)(void)bonsai_fx_process(&fx.voice[0],silence);
 struct dd_frame input={INT16_MIN,INT16_MAX};
 struct dd_wide_frame bypass=bonsai_fx_process(&fx.voice[0],input);
 assert(bypass.l==input.l&&bypass.r==input.r);
}

static void test_full_scale_stability(void)
{
 for(unsigned amount=1;amount<=256;amount++) {
  bonsai_fx_init(&fx);configure(0,BONSAI_FX_FILTER,amount);warm(0);
  struct dd_wide_frame out={0,0};
  for(unsigned i=0;i<4800;i++)out=bonsai_fx_process(&fx.voice[0],(struct dd_frame){INT16_MAX,INT16_MIN});
  assert(abs(out.l)<=1&&abs(out.r)<=1);
  out=bonsai_fx_process(&fx.voice[0],(struct dd_frame){INT16_MIN,INT16_MAX});
  assert(out.l<INT16_MIN&&out.r>INT16_MAX); /* preserve transient headroom */
  assert(abs(out.l)<=65535&&abs(out.r)<=65535&&fx.voice[0].clipped==0);
  for(unsigned i=0;i<4800;i++) {
   int16_t sample=i&1?INT16_MIN:INT16_MAX;
   out=bonsai_fx_process(&fx.voice[0],(struct dd_frame){sample,(int16_t)(-sample-1)});
   assert(abs(out.l)<=65535&&abs(out.r)<=65535);
   assert(fx.voice[0].low_l>=INT16_MIN*256&&fx.voice[0].low_l<=INT16_MAX*256);
   assert(fx.voice[0].low_r>=INT16_MIN*256&&fx.voice[0].low_r<=INT16_MAX*256);
  }
  for(unsigned i=0;i<4800;i++)out=bonsai_fx_process(&fx.voice[0],silence);
  assert(abs(out.l)<=1&&abs(out.r)<=1&&fx.voice[0].clipped==0);
 }
}
static void test_song_reset_preserves_settings_only(void)
{
 bonsai_fx_init(&fx);configure(0,BONSAI_FX_FILTER,200);warm(0);
 configure(1,BONSAI_FX_FILTER,123);warm(1);
 for(unsigned i=0;i<4800;i++) {
  (void)bonsai_fx_process(&fx.voice[0],(struct dd_frame){18000,-12000});
  (void)bonsai_fx_process(&fx.voice[1],(struct dd_frame){7654,-4321});
 }
 uint32_t saved=atomic_load(&fx.config[0]);fx.voice[0].clipped=47;
 struct bonsai_fx_voice other=fx.voice[1];
 assert(fx.voice[0].low_l!=0&&fx.voice[0].filter_seeded);
 assert(!bonsai_fx_reset_voice(&fx,8));
 assert(bonsai_fx_reset_voice(&fx,0));
 assert(atomic_load(&fx.config[0])==saved);
 assert(fx.voice[0].clipped==47);
 assert(fx.voice[0].low_l==0&&fx.voice[0].low_r==0&&!fx.voice[0].filter_seeded);
 assert(!memcmp(&fx.voice[1],&other,sizeof(other)));
 assert(atomic_load(&fx.config[1])==((BONSAI_FX_FILTER<<16)|123));
 bonsai_fx_begin_block(&fx);
 for(unsigned i=0;i<4800;i++) {
  struct dd_wide_frame out=bonsai_fx_process(&fx.voice[0],silence);
  assert(out.l==0&&out.r==0); /* previous song's filter state cannot leak */
 }
 assert(fx.voice[0].type==BONSAI_FX_FILTER&&fx.voice[0].mix==256);
 assert(fx.voice[0].requested_amount==200);
}

static void test_buffer_matches_sample_processing(void)
{
 static const unsigned counts[]={0,1,7,12,63,64,65,255,256,257};
 static struct bonsai_fx reference;
 struct dd_wide_frame actual[257], expected[257];
 uint32_t random=0x51b08u;
 bonsai_fx_init(&fx);bonsai_fx_init(&reference);
 for(unsigned block=0;block<4000;block++) {
  unsigned n=counts[block%10];
  if(block%29==0) {
   unsigned type=(block/29)%2, amount=(block/116)%3==0?1:(block*17)%257;
   assert(bonsai_fx_set(&fx,0,(enum bonsai_fx_type)type,amount));
   assert(bonsai_fx_set(&reference,0,(enum bonsai_fx_type)type,amount));
  }
  if(block%113==0) {
   assert(bonsai_fx_set(&fx,0,BONSAI_FX_FILTER,0));
   assert(bonsai_fx_set(&reference,0,BONSAI_FX_FILTER,0));
  }
  bonsai_fx_begin_block(&fx);bonsai_fx_begin_block(&reference);
  for(unsigned i=0;i<n;i++) {
   random^=random<<13;random^=random>>17;random^=random<<5;
   actual[i]=(struct dd_wide_frame){(int16_t)random,(int16_t)(random>>16)};
   if(block%7==0) actual[i]=(struct dd_wide_frame){0,0};
   if(block%7==1) actual[i]=(struct dd_wide_frame){INT16_MIN,INT16_MAX};
   expected[i]=bonsai_fx_process(&reference.voice[0],(struct dd_frame){actual[i].l,actual[i].r});
  }
  bonsai_fx_process_buffer(&fx.voice[0],actual,n);
  assert(!memcmp(actual,expected,n*sizeof(*actual)));
  assert(!memcmp(&fx.voice[0],&reference.voice[0],sizeof(fx.voice[0])));
 }
}
static void test_steady_filter_all_coefficients_match(void)
{
 static struct bonsai_fx reference;
 struct dd_wide_frame actual[256],expected[256];
 for(unsigned amount=1;amount<=256;amount++) {
  bonsai_fx_init(&fx);bonsai_fx_init(&reference);
  assert(bonsai_fx_set(&fx,0,BONSAI_FX_FILTER,amount));
  assert(bonsai_fx_set(&reference,0,BONSAI_FX_FILTER,amount));
  bonsai_fx_begin_block(&fx);bonsai_fx_begin_block(&reference);
  for(unsigned i=0;i<256;i++) {
   struct dd_frame in={INT16_MIN,INT16_MAX};
   (void)bonsai_fx_process(&fx.voice[0],in);
   (void)bonsai_fx_process(&reference.voice[0],in);
  }
  for(unsigned block=0;block<16;block++) {
   for(unsigned i=0;i<256;i++) {
    int16_t left=(i&1)?INT16_MIN:INT16_MAX;
    actual[i]=(struct dd_wide_frame){left,(int16_t)(-left-1)};
    expected[i]=bonsai_fx_process(&reference.voice[0],(struct dd_frame){actual[i].l,actual[i].r});
   }
   bonsai_fx_process_buffer(&fx.voice[0],actual,256);
   assert(!memcmp(actual,expected,sizeof(actual)));
   assert(!memcmp(&fx.voice[0],&reference.voice[0],sizeof(fx.voice[0])));
  }
 }
}

static void test_filter_sweep_does_not_jump(void)
{
 for(unsigned direction=0;direction<2;direction++) {
  bonsai_fx_init(&fx);configure(0,BONSAI_FX_FILTER,direction?256:1);warm(0);
  configure(0,BONSAI_FX_FILTER,direction?1:256);
  unsigned steps=0;
  for(unsigned i=0;i<256;i++) {
   struct bonsai_fx_voice fixed=fx.voice[0];fixed.target_alpha=fixed.alpha;
   uint16_t before=fx.voice[0].alpha;
   struct dd_frame input={i&1?INT16_MIN:INT16_MAX,i&1?INT16_MAX:INT16_MIN};
   struct dd_wide_frame expected=bonsai_fx_process(&fixed,input);
   struct dd_wide_frame actual=bonsai_fx_process(&fx.voice[0],input);
   /* Isolate control-induced change from the signal's own transient. One
    * coefficient step can change beta by only 1/512 of a <=65535 residual. */
   assert(abs(actual.l-expected.l)<=128&&abs(actual.r-expected.r)<=128);
   assert(abs((int)fx.voice[0].alpha-before)<=1);
   if(fx.voice[0].alpha!=before)steps++;
  }
  assert(steps==185&&fx.voice[0].alpha==fx.voice[0].target_alpha);
 }
}
int main(void)
{
 test_filter_sweep_does_not_jump();
 test_bypass_and_bounds();test_filter_response_and_stereo();
 test_invalid_direct_configuration_fades_to_bypass();test_eight_filter_states_are_isolated();
 test_full_scale_stability();test_song_reset_preserves_settings_only();
 test_buffer_matches_sample_processing();test_steady_filter_all_coefficients_match();
 printf("PASS: eight-stem filter bank, stereo bypass/response, reset, bounded full-scale state and exact scalar/buffer processing (%zu bytes)\n",sizeof(fx));
}
