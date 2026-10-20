/* SPDX-License-Identifier: MIT
 * Host-only validation of the production sync -> engine -> WSOLA path.
 * No device, storage, serial, wall-clock scheduling, or firmware modification.
 */
#include "dual_engine.h"
#include "bonsai_sync.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_RATE 48000u
#define NATIVE_RATE 24000u
#define BLOCK 256u
#define FFT_N 131072u
#define PI 3.14159265358979323846

static struct dd_engine engine;
static struct dd_frame wave[2][NATIVE_RATE],input[DD_RING];
static int16_t output[BLOCK*2],captured[FFT_N*2];
static double real[FFT_N],imaginary[FFT_N];
static uint64_t charged_work;
static unsigned max_calls;
static uint32_t output_clock,loop_position[2],loop_length[2];
static uint32_t pulse_period[2];
static uint32_t pulse_width[2],pulse_anchor[2];
static bool pulse_background;

static void sine_wave(unsigned deck,double left,double right,bool inverted)
{
 for(unsigned i=0;i<NATIVE_RATE;i++) {
  int16_t l=(int16_t)lround(10000*sin(2*PI*left*i/NATIVE_RATE));
  int16_t r=inverted?(int16_t)-l:(int16_t)lround(10000*sin(2*PI*right*i/NATIVE_RATE));
  wave[deck][i]=(struct dd_frame){l,r};
 }
}

static void setup(unsigned decks)
{
 dd_init(&engine);atomic_store(&engine.master,decks==2?128:256);
 output_clock=0;charged_work=0;max_calls=0;
 pulse_period[0]=pulse_period[1]=0;
 pulse_width[0]=pulse_width[1]=48;pulse_anchor[0]=pulse_anchor[1]=0;
 pulse_background=false;
 for(unsigned k=0;k<decks;k++) {
  struct dd_deck *d=&engine.deck[k];
  dd_reset_deck(d,15);dd_set_stretch(d,true);atomic_store(&d->playing,1);
  loop_position[k]=0;loop_length[k]=1200000;
  for(unsigned stem=0;stem<4;stem++)atomic_store(&d->voice[stem].gain,256);
 }
}

/* Same bounded ring capacity and eight-sector P16M refill threshold as the
 * adapter. One fullest voice is refilled per worker iteration, then at most
 * 1024 charged channel operations are allowed for the production worker.
 * Up to 64 iterations per render callback is deliberately a generous host
 * schedule. Success does NOT imply the MCU can execute these in real time.
 */
static bool refill_one(void)
{
 unsigned best=8;uint32_t largest=0;
 for(unsigned k=0;k<2;k++)for(unsigned stem=0;stem<4;stem++) {
  struct dd_deck *d=&engine.deck[k];if(!d->voice[stem].present)continue;
  uint32_t room=dd_room(d,stem);
  if(room>=8*248&&room>largest){largest=room;best=k*4+stem;}
 }
 if(best==8)return false;
 unsigned k=best/4,stem=best%4;
 struct dd_deck *d=&engine.deck[k];uint32_t written=atomic_load(&d->voice[stem].written);
 for(unsigned i=0;i<8*248;i++) {
  if(pulse_period[k]) {
   unsigned position=((written+i)%pulse_period[k]+pulse_period[k]-pulse_anchor[k])%pulse_period[k];
   int16_t amplitude=position<pulse_width[k]?10000:0;
   input[i]=pulse_background?wave[k][(written+i)%NATIVE_RATE]:(struct dd_frame){0,0};
   if(k)input[i].r+=amplitude;else input[i].l+=amplitude;
  } else input[i]=wave[k][(written+i)%NATIVE_RATE];
 }
 assert(dd_push(d,stem,input,8*248));return true;
}

static void service(void)
{
 unsigned calls=0;
 for(;calls<64;calls++) {
  bool filled=refill_one();uint32_t used=dd_stretch_service(&engine,1024);
  assert(used<=1024);charged_work+=used;if(!used&&!filled)break;
 }
 if(calls>max_calls)max_calls=calls;
}

static void render(unsigned count)
{
 assert(count<=BLOCK);service();uint32_t before[2];
 for(unsigned k=0;k<2;k++)before[k]=atomic_load(&engine.deck[k].source_clock);
 dd_render(&engine,output,count);output_clock+=count;
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&engine.deck[k];if(!d->voice[0].present)continue;
  assert(!d->starved&&!d->underruns);
  uint32_t after=atomic_load(&d->source_clock),advance=after-before[k];
  assert(advance<=count); /* no seek or backward source-clock jump */
  loop_position[k]=(loop_position[k]+advance)%loop_length[k];
  for(unsigned stem=0;stem<4;stem++) {
   assert(atomic_load(&d->voice[stem].written)-atomic_load(&d->read)<=DD_RING);
  }
 }
 assert(!engine.clips);
}

static void observe(struct bonsai_sync *sync)
{
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&engine.deck[k];
  struct bonsai_sync_position p={k+1,atomic_load(&d->source_clock),loop_position[k],
   output_clock,dd_tempo_actual(d),atomic_load(&d->playing)&&!d->starved};
  assert(bonsai_sync_observe(sync,k,&p)==BONSAI_SYNC_OK);
 }
}

static void fft(void)
{
 for(unsigned i=1,j=0;i<FFT_N;i++) {
  unsigned bit=FFT_N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
  if(i<j){double t=real[i];real[i]=real[j];real[j]=t;t=imaginary[i];imaginary[i]=imaginary[j];imaginary[j]=t;}
 }
 for(unsigned size=2;size<=FFT_N;size<<=1) {
  double angle=-2*PI/size,step_r=cos(angle),step_i=sin(angle);
  for(unsigned first=0;first<FFT_N;first+=size) {
   double wr=1,wi=0;
   for(unsigned j=0;j<size/2;j++) {
    unsigned a=first+j,b=a+size/2;
    double r=real[b]*wr-imaginary[b]*wi,i=real[b]*wi+imaginary[b]*wr;
    real[b]=real[a]-r;imaginary[b]=imaginary[a]-i;real[a]+=r;imaginary[a]+=i;
    double next=wr*step_r-wi*step_i;wi=wr*step_i+wi*step_r;wr=next;
   }
  }
 }
}

struct spectrum {double peak_hz,rms_ratio,near_energy;};
static struct spectrum spectrum(unsigned channel,double expected)
{
 double squared=0;
 for(unsigned i=0;i<FFT_N;i++) {
  double x=captured[i*2+channel];squared+=x*x;
  real[i]=x*(.5-.5*cos(2*PI*i/(FFT_N-1)));imaginary[i]=0;
 }
 fft();unsigned peak=1;double highest=0,total=0,near=0;
 for(unsigned i=1;i<FFT_N/2;i++) {
  double energy=real[i]*real[i]+imaginary[i]*imaginary[i];
  double hz=(double)i*OUTPUT_RATE/FFT_N;total+=energy;
  if(fabs(hz-expected)<=fmax(2.,expected*.02))near+=energy;
  if(energy>highest){highest=energy;peak=i;}
 }
 double a=log(fmax(1e-30,real[peak-1]*real[peak-1]+imaginary[peak-1]*imaginary[peak-1]));
 double b=log(fmax(1e-30,highest));
 double c=log(fmax(1e-30,real[peak+1]*real[peak+1]+imaginary[peak+1]*imaginary[peak+1]));
 double offset=.5*(a-c)/(a-2*b+c);
 return (struct spectrum){(peak+offset)*OUTPUT_RATE/FFT_N,sqrt(squared/FFT_N)/(10000/sqrt(2.)),near/total};
}

static unsigned quality(void)
{
 static const uint32_t rates[]={32768,49152,63570,65536,67502,73728,81920};
 static const unsigned frequencies[]={20,30,40,55,80,110,220,440,880,1500,3000};
 unsigned failures=0;
 puts("tone_hz,ratio,peak_hz,rms_ratio,energy_within_2pct_or_2hz,screen");
 for(unsigned f=0;f<sizeof(frequencies)/sizeof(*frequencies);f++)for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);r++) {
  sine_wave(0,frequencies[f],frequencies[f],true);setup(1);assert(dd_set_tempo(&engine.deck[0],rates[r]));
  unsigned warmup=OUTPUT_RATE;
  while(warmup){unsigned n=warmup>BLOCK?BLOCK:warmup;render(n);warmup-=n;}
  for(unsigned i=0;i<FFT_N;i+=BLOCK) {
   render(BLOCK);memcpy(captured+i*2,output,sizeof(output));
   for(unsigned j=0;j<BLOCK;j++)assert(output[j*2]==-output[j*2+1]);
  }
  struct spectrum s=spectrum(0,frequencies[f]);
  /* Declared synthetic screening thresholds, not musical certification:
   * <=1% pitch error, <=3.1 dB level loss, >=90% energy near the original tone.
   * A failed screen is a nonzero process exit, never a decorative warning. */
  bool pass=fabs(s.peak_hz/frequencies[f]-1)<=.01&&s.rms_ratio>=.7&&s.rms_ratio<=1.1&&s.near_energy>=.9;
  if(!pass)failures++;
  printf("%u,%.6f,%.3f,%.4f,%.4f,%s\n",frequencies[f],rates[r]/65536.,s.peak_hz,s.rms_ratio,s.near_energy,pass?"PASS":"FAIL");
 }
 /* Independent content on L/R challenges the shared grain choice; pure
  * antiphase above alone could hide accidental mono analysis/cancellation. */
 const struct {unsigned left,right;double left_gain,right_gain;} stereo[]={
  {55,220,1,1},{55,263,1,1},{263,55,1,1},{55,263,1,.25}
 };
 for(unsigned p=0;p<sizeof(stereo)/sizeof(*stereo);p++)for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);r++) {
  sine_wave(0,stereo[p].left,stereo[p].right,false);
  for(unsigned i=0;i<NATIVE_RATE;i++) {
   wave[0][i].l=(int16_t)lround(wave[0][i].l*stereo[p].left_gain);
   wave[0][i].r=(int16_t)lround(wave[0][i].r*stereo[p].right_gain);
  }
  setup(1);assert(dd_set_tempo(&engine.deck[0],rates[r]));
  for(unsigned n=0;n<OUTPUT_RATE;n+=BLOCK)render(BLOCK);
  for(unsigned i=0;i<FFT_N;i+=BLOCK){render(BLOCK);memcpy(captured+i*2,output,sizeof(output));}
  struct spectrum left=spectrum(0,stereo[p].left),right=spectrum(1,stereo[p].right);
  left.rms_ratio/=stereo[p].left_gain;right.rms_ratio/=stereo[p].right_gain;
  bool pass=fabs(left.peak_hz/stereo[p].left-1)<=.01&&fabs(right.peak_hz/stereo[p].right-1)<=.01&&
   left.rms_ratio>=.7&&right.rms_ratio>=.7&&left.rms_ratio<=1.1&&right.rms_ratio<=1.1&&
   left.near_energy>=.9&&right.near_energy>=.9;
  if(!pass)failures++;
  printf("stereo%u/%u-gain%.2f/%.2f,%.6f,%.3f/%.3f,%.4f/%.4f,%.4f/%.4f,%s\n",
   stereo[p].left,stereo[p].right,stereo[p].left_gain,stereo[p].right_gain,rates[r]/65536.,
   left.peak_hz,right.peak_hz,left.rms_ratio,right.rms_ratio,left.near_energy,right.near_energy,pass?"PASS":"FAIL");
 }
 for(unsigned side=0;side<2;side++) {
  sine_wave(0,side?0:55,side?55:0,false);setup(1);assert(dd_set_tempo(&engine.deck[0],49152));
  for(unsigned n=0;n<3*OUTPUT_RATE;n+=BLOCK) {
   render(BLOCK);for(unsigned i=0;i<BLOCK;i++)assert(!output[i*2+(side?0:1)]);
  }
 }
 printf("QUALITY: %u synthetic screening failures; antiphase exact in all %zu mono-tone cases; left/right silence isolation exact\n",failures,
  sizeof(frequencies)/sizeof(*frequencies)*sizeof(rates)/sizeof(*rates));
 return failures;
}

static void tapped_follow(void)
{
 sine_wave(0,80,80,true);sine_wave(1,110,110,true);setup(2);
 struct bonsai_sync sync;assert(bonsai_sync_init(&sync,32768,81920));
 for(unsigned k=0;k<2;k++)assert(bonsai_sync_song(&sync,k,k+1,loop_length[k]));
 /* Four beat taps read actual source clocks from the dry renderer. */
 for(unsigned elapsed=0;elapsed<=72000;elapsed+=32) {
  observe(&sync);
  if(elapsed%24000==0)assert(bonsai_sync_tap(&sync,0)==(elapsed<72000?BONSAI_SYNC_PENDING:BONSAI_SYNC_OK));
  if(elapsed<=57600&&elapsed%19200==0)assert(bonsai_sync_tap(&sync,1)==(elapsed<57600?BONSAI_SYNC_PENDING:BONSAI_SYNC_OK));
  if(elapsed<72000)render(32);
 }
 assert(sync.deck[0].bpm_milli==120000&&sync.deck[1].bpm_milli==150000);
 assert(bonsai_sync_follow(&sync,1)==BONSAI_SYNC_OK);
 double first_lock=-1,last_error=0,max_final_error=0;bool changed=false,nudged=false;
 uint32_t start=output_clock;
 for(unsigned block=0;block<60*OUTPUT_RATE/BLOCK;block++) {
  observe(&sync);double elapsed=(output_clock-start)/(double)OUTPUT_RATE;
  if(elapsed>=30&&!changed){assert(dd_set_tempo(&engine.deck[0],61440));changed=true;}
  if(elapsed>=45&&!nudged){assert(bonsai_sync_nudge(&sync,1,1)==BONSAI_SYNC_OK);nudged=true;}
  struct bonsai_sync_request request=bonsai_sync_step(&sync);assert(request.apply);
  assert(dd_set_tempo(&engine.deck[1],request.tempo_q16));
  last_error=fabs(request.phase_error_q32/4294967296.);
  if(request.mode==BONSAI_SYNC_LOCKED&&first_lock<0)first_lock=elapsed;
  if(elapsed>=55&&last_error>max_final_error)max_final_error=last_error;
  render(BLOCK);
 }
 assert(first_lock>=0&&first_lock<20);assert(last_error<=1./64+.001);
 assert(max_final_error<=1./64+.001);assert(sync.deck[1].mode==BONSAI_SYNC_LOCKED);
 assert(dd_tempo_actual(&engine.deck[1])==49152);
 printf("FOLLOW: four real-engine taps=120/150 BPM, lock=%.3fs, final/max-last-5s phase=%.6f/%.6f beats; master step + nudge recovered; %llu work units, max %u worker calls/callback\n",
  first_lock,last_error,max_final_error,(unsigned long long)charged_work,max_calls);
}

static void sustained_streaming(void)
{
 sine_wave(0,55,220,false);sine_wave(1,80,440,false);setup(2);
 static const uint32_t rates[]={32768,81920,49152,73728,50001,65536,63570,67502};
 uint64_t start[2]={0,0};unsigned changes=0;
 /* 10 simulated minutes, 8 live stems, wraps of physical rings, recurring
  * rate changes, mute/unmute; bounded producer and one shared worker. */
 for(unsigned block=0;block<600*OUTPUT_RATE/BLOCK;block++) {
  if(block%1875==0) {
   for(unsigned k=0;k<2;k++)assert(dd_set_tempo(&engine.deck[k],rates[(changes+k*3)%8]));
   changes++;
  }
  if(block%1500==500)atomic_store(&engine.deck[0].mute_mask,5);
  if(block%1500==700)atomic_store(&engine.deck[0].mute_mask,0);
  render(BLOCK);
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&engine.deck[k];uint32_t now=atomic_load(&d->source_clock);
   assert(now>=start[k]);start[k]=now;
  }
 }
 printf("STREAM: 600 simulated seconds, 8 stems, %u paired tempo changes, zero underruns/clips, 4096-frame rings; %llu work units, max %u worker calls/callback\n",
  changes,(unsigned long long)charged_work,max_calls);
}

static void range_matrix(void)
{
 const uint32_t bpms[]={20000,60000,90000,120000,150000,200000,300000};
 const uint32_t rates[]={32768,49152,65536,73728,81920};unsigned accepted=0,rejected=0;
 for(unsigned a=0;a<7;a++)for(unsigned b=0;b<7;b++)for(unsigned t=0;t<5;t++) {
  struct bonsai_sync sync;assert(bonsai_sync_init(&sync,32768,81920));
  for(unsigned k=0;k<2;k++) {
   assert(bonsai_sync_song(&sync,k,k+1,1200000));
   assert(bonsai_sync_set_grid(&sync,k,k+1,k?bpms[b]:bpms[a],0));
   struct bonsai_sync_position p={k+1,0,0,0,k?65536:rates[t],true};
   assert(bonsai_sync_observe(&sync,k,&p)==BONSAI_SYNC_OK);
  }
  uint64_t expected=((uint64_t)bpms[a]*rates[t]+bpms[b]/2)/bpms[b];
  enum bonsai_sync_result result=bonsai_sync_follow(&sync,1);
  if(expected>=32768&&expected<=81920) {
   assert(result==BONSAI_SYNC_OK);struct bonsai_sync_request r=bonsai_sync_step(&sync);
   assert(r.apply&&r.tempo_q16==expected&&r.mode==BONSAI_SYNC_LOCKED);accepted++;
  } else {assert(result==BONSAI_SYNC_RANGE&&!bonsai_sync_step(&sync).apply);rejected++;}
 }
 printf("RANGE: 245 BPM/master-tempo combinations; %u accepted exactly, %u rejected without clamped success\n",accepted,rejected);
}

struct pulse_case {
 const char *name;
 uint32_t master_period,follower_period,width,master_anchor,follower_anchor;
};
static unsigned pulse_phase_case(const struct pulse_case *test)
{
 setup(2);pulse_period[0]=test->master_period;pulse_period[1]=test->follower_period;
 pulse_width[0]=pulse_width[1]=test->width;
 pulse_anchor[0]=test->master_anchor;pulse_anchor[1]=test->follower_anchor;
 struct bonsai_sync sync;assert(bonsai_sync_init(&sync,32768,81920));
 for(unsigned k=0;k<2;k++) {
  assert(bonsai_sync_song(&sync,k,k+1,loop_length[k]));
  uint32_t bpm=(1440000000u+pulse_period[k]/2)/pulse_period[k];
  assert(bonsai_sync_set_grid(&sync,k,k+1,bpm,pulse_anchor[k]));
 }
 observe(&sync);assert(bonsai_sync_follow(&sync,1)==BONSAI_SYNC_OK);
 bool high=false;unsigned pulses=0,short_gaps=0,previous_onset=0;
 double maximum_ms=0,sum_ms=0,max_nominal=0;
 for(unsigned block=0;block<40*OUTPUT_RATE/BLOCK;block++) {
  observe(&sync);struct bonsai_sync_request request=bonsai_sync_step(&sync);assert(request.apply);
  assert(dd_set_tempo(&engine.deck[1],request.tempo_q16));
  unsigned start=output_clock;render(BLOCK);
  if(start<10*OUTPUT_RATE)continue;
  double nominal=fabs(request.phase_error_q32/4294967296.);if(nominal>max_nominal)max_nominal=nominal;
  for(unsigned i=0;i<BLOCK;i++) {
   bool current=output[i*2+1]>1000;
   if(current&&!high) {
    /* The dry master supplies an exact independent audible-onset oracle. */
    uint32_t period=2*test->master_period,anchor=2*test->master_anchor;
    int phase=(int)(((start+i)%period+period-anchor)%period);
    if(phase>(int)period/2)phase-=(int)period;
    if(pulses&&start+i-previous_onset<OUTPUT_RATE/20)short_gaps++;
    previous_onset=start+i;
    double ms=fabs(phase/48.);sum_ms+=ms;if(ms>maximum_ms)maximum_ms=ms;pulses++;
   }
   high=current;
  }
 }
 /* Keep the original 1/64-beat lock tolerance; vary content, never the gate. */
 double expected=30.*NATIVE_RATE/test->master_period;
 double allowed_ms=test->master_period/24./64;
 bool pass=fabs(pulses-expected)<=1&&short_gaps==0&&maximum_ms<=allowed_ms;
 printf("PULSE %s: ratio %.6f, width %u, anchors %u/%u, last 30s: %u onsets (expected %.2f), %u gaps below 50ms, mean/max audible phase %.3f/%.3f ms, max nominal %.6f beats; audible <=1/64-beat screen %s\n",
  test->name,test->follower_period/(double)test->master_period,test->width,test->master_anchor,test->follower_anchor,
  pulses,expected,short_gaps,pulses?sum_ms/pulses:0,maximum_ms,max_nominal,pass?"PASS":"FAIL");
 return pass?0:1;
}

static unsigned pulse_phase(void)
{
 const struct pulse_case cases[]={
  {"original",12000,9600,48,0,0},
  {"half-shifted",12000,6000,24,1200,600},
  {"slow-wide",12000,7200,96,6000,3600},
  {"fast-wide",12000,15000,192,9000,11250},
  {"fractional-fast",12000,13500,48,3000,3375},
  {"one-sample",12000,9600,1,512,410},
  {"90-to120-bpm",16000,12000,48,4000,3000}
 };
 unsigned failures=0;
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)failures+=pulse_phase_case(&cases[i]);
 return failures;
}

static unsigned transient_background_guard(void)
{
 unsigned failures=0;const uint32_t ratios[]={49152,81920};
 for(unsigned r=0;r<2;r++) {
  sine_wave(0,220,220,true);
  for(unsigned i=0;i<NATIVE_RATE;i++){wave[0][i].l/=10;wave[0][i].r/=10;}
  setup(1);pulse_period[0]=12000;pulse_background=true;
  assert(dd_set_tempo(&engine.deck[0],ratios[r]));
  for(unsigned i=0;i<OUTPUT_RATE;i+=BLOCK) {
   render(BLOCK);assert(!engine.stretch_work.transient);
  }
  for(unsigned i=0;i<FFT_N;i+=BLOCK) {
   render(BLOCK);assert(!engine.stretch_work.transient);
   memcpy(captured+i*2,output,sizeof(output));
  }
  /* Right contains only the quiet sustained tone; the loud pulse is left.
   * Its tail must retain ordinary overlap, preserving the background tone. */
  struct spectrum tone=spectrum(1,220);tone.rms_ratio*=10;
  bool pass=fabs(tone.peak_hz/220-1)<=.01&&tone.rms_ratio>=.7&&tone.rms_ratio<=1.1&&tone.near_energy>=.9;
  if(!pass)failures++;
  printf("BACKGROUND: 10:1 pulse/tone, ratio %.6f, peak %.3fHz, RMS %.4f, near-energy %.4f, no transient-tail replacement, %s\n",
   ratios[r]/65536.,tone.peak_hz,tone.rms_ratio,tone.near_energy,pass?"PASS":"FAIL");
 }
 return failures;
}

int main(int argc,char **argv)
{
 bool only_quality=argc==2&&!strcmp(argv[1],"--quality");
 bool only_functional=argc==2&&!strcmp(argv[1],"--functional");
 if(argc>2||(argc==2&&!only_quality&&!only_functional))return 2;
 setvbuf(stdout,NULL,_IOLBF,0);
 if(!only_quality){range_matrix();tapped_follow();sustained_streaming();}
 if(only_functional)return 0;
 unsigned failures=pulse_phase();failures+=transient_background_guard();failures+=quality();return failures?1:0;
}
