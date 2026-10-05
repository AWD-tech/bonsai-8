#include "dual_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
static struct dd_engine engine;
static struct dd_frame frames[DD_RING];
static int16_t out[(DD_RING+1)*2];
static void fill(unsigned k,unsigned s,int16_t value,unsigned n)
{
 for(unsigned i=0;i<n;i++) frames[i]=(struct dd_frame){value,-value};
 assert(dd_push(&engine.deck[k],s,frames,n));
}
static void test_eight_stems(void)
{
 dd_init(&engine);atomic_store(&engine.master,256);
 for(unsigned k=0;k<2;k++) {
  dd_reset_deck(&engine.deck[k],15);atomic_store(&engine.deck[k].playing,1);
  for(unsigned s=0;s<4;s++) {atomic_store(&engine.deck[k].voice[s].gain,256);fill(k,s,(k*4+s+1)*1000,DD_RING);}
 }
 dd_render(&engine,out,512);
 assert(out[1022]==9000&&out[1023]==-9000);
 assert(atomic_load(&engine.deck[0].read)==256&&atomic_load(&engine.deck[1].read)==256);
 atomic_store(&engine.deck[0].voice[0].gain,0);dd_render(&engine,out,512);
 assert(out[1022]==8750);
 atomic_store(&engine.deck[1].mute_mask,15);dd_render(&engine,out,512);
 assert(out[1022]==2250);
 atomic_store(&engine.deck[0].playing,0);uint32_t paused=atomic_load(&engine.deck[0].read);
 dd_render(&engine,out,512);assert(out[1022]==0);
 assert(atomic_load(&engine.deck[0].read)==paused);
 assert(atomic_load(&engine.deck[1].read)==1024);
}
static void test_starvation_holds_deck(void)
{
 dd_init(&engine);dd_reset_deck(&engine.deck[0],15);atomic_store(&engine.deck[0].playing,1);
 for(unsigned s=0;s<3;s++) fill(0,s,2000,1024);
 dd_render(&engine,out,512);
 assert(atomic_load(&engine.deck[0].read)==0&&engine.deck[0].underruns==1);
 fill(0,3,2000,1024);dd_render(&engine,out,512);
 assert(atomic_load(&engine.deck[0].read)==256&&!engine.deck[0].starved);
 assert(engine.deck[0].underruns==1);
}
static void test_wrap_speed_and_bounds(void)
{
 dd_init(&engine);struct dd_deck *d=&engine.deck[0];dd_reset_deck(d,1);
 atomic_store(&d->read,UINT32_MAX-100);atomic_store(&d->voice[0].written,UINT32_MAX-100);
 fill(0,0,2000,DD_RING);assert(dd_room(d,0)==0);assert(!dd_push(d,0,frames,1));
 atomic_store(&d->playing,1);atomic_store(&d->speed,81920);dd_render(&engine,out,512);
 assert(atomic_load(&d->read)==219&&d->fraction==0);
 assert(dd_room(d,0)==320);assert(!dd_push(d,4,frames,1));
 atomic_store(&d->speed,0);dd_render(&engine,out,512);assert(atomic_load(&d->read)==347);
}
static void test_independent_source_clock(void)
{
 dd_init(&engine);
 for(unsigned k=0;k<2;k++)assert(atomic_load(&engine.deck[k].source_clock)==0);
 struct dd_deck *d=&engine.deck[0];dd_reset_deck(d,1);
 uint32_t first=UINT32_MAX-63u;
 atomic_store(&d->read,first);atomic_store(&d->voice[0].written,first);
 atomic_store(&d->source_clock,UINT32_MAX-19u);
 fill(0,0,2000,DD_RING);atomic_store(&d->playing,1);atomic_store(&d->speed,81920);
 d->envelope=256;d->voice[0].current_gain=256;
 dd_render(&engine,out,512); /* steady path:320native frames, independent wraps */
 assert(atomic_load(&d->read)==256&&atomic_load(&d->source_clock)==300);
 dd_render(&engine,out,0);assert(atomic_load(&d->source_clock)==300);
 atomic_store(&d->playing,0);
 dd_render(&engine,out,512);dd_render(&engine,out,512);
 assert(atomic_load(&d->read)==256&&atomic_load(&d->source_clock)==300);
 atomic_store(&d->playing,1);atomic_store(&d->mute_mask,1);
 dd_render(&engine,out,512); /* silent but running still consumes source */
 assert(atomic_load(&d->source_clock)==620&&atomic_load(&d->read)==576);
 atomic_store(&d->voice[0].written,576);uint32_t errors=d->underruns;
 dd_render(&engine,out,512);dd_render(&engine,out,512);
 assert(d->starved&&d->underruns==errors+1);
 assert(atomic_load(&d->source_clock)==620&&atomic_load(&d->read)==576);
 fill(0,0,2000,1024);dd_render(&engine,out,512);
 assert(!d->starved&&atomic_load(&d->source_clock)==940&&atomic_load(&d->read)==896);
 dd_reset_deck(d,1);
 assert(atomic_load(&d->source_clock)==0&&atomic_load(&d->read)==0);
 assert(d->underruns==errors+1); /* cueing never erases diagnostic history */
}
static void test_pickup(void)
{
 struct dd_pickup p={-1,true};
 assert(!dd_pickup_move(&p,64,200));assert(!dd_pickup_move(&p,64,100));
 assert(dd_pickup_move(&p,64,20));assert(dd_pickup_move(&p,64,240));
 p=(struct dd_pickup){-1,true};assert(dd_pickup_move(&p,0,0));
 p=(struct dd_pickup){-1,true};assert(!dd_pickup_move(&p,256,0));assert(dd_pickup_move(&p,256,251));
}
static void test_codec(void)
{
 uint8_t b[512]={0};struct dd_frame decoded[DD_MAX_DECODE];
 const int vals[4]={-8192,8191,-1,1};memcpy(b,"P14S",4);b[11]=0x5b;b[12]=24;b[13]=1;
 for(unsigned g=0;g<70;g++) {
  uint64_t bits=0;for(unsigned i=0;i<4;i++) bits=(bits<<14)|((uint32_t)vals[i]&16383);
  for(unsigned i=0;i<7;i++) b[16+g*7+i]=(uint8_t)(bits>>(48-i*8));
 }
 for(unsigned sh=0;sh<4;sh++) {
  b[14]=sh;assert(dd_decode(b,DD_P14S,decoded)==140);
  assert(decoded[0].l==(-32768>>sh)&&decoded[0].r==(32764>>sh));
  assert(decoded[139].l==(-4>>sh)&&decoded[139].r==(4>>sh));
 }
 b[11]=0;assert(!dd_decode(b,DD_P14S,decoded));b[11]=0x5b;b[14]=4;assert(!dd_decode(b,DD_P14S,decoded));
 memset(b,0,sizeof(b));memcpy(b,"P16M",4);b[11]=0x5b;b[12]=240;b[13]=1;
 b[16]=0;b[17]=128;b[510]=255;b[511]=127;
 assert(dd_decode(b,DD_P16M,decoded)==248);assert(decoded[0].l==-32768&&decoded[247].r==32767);
 assert(!dd_decode(b,4,decoded));
}
static void test_saturation(void)
{
 dd_init(&engine);atomic_store(&engine.master,256);
 for(unsigned k=0;k<2;k++) {
  dd_reset_deck(&engine.deck[k],15);atomic_store(&engine.deck[k].playing,1);
  for(unsigned s=0;s<4;s++) {atomic_store(&engine.deck[k].voice[s].gain,256);fill(k,s,32767,1024);}
 }
 dd_render(&engine,out,512);assert(out[1022]==32767&&out[1023]==-32768&&engine.clips>0);
}
static void test_interpolation_full_range(void)
{
 /* Exercise the render call, not a separate approximation. Four identical
  * unity-gain stems cancel the mixer's fixed headroom, exposing every LSB. */
 const int16_t pairs[][2]={{INT16_MIN,INT16_MAX},{INT16_MAX,INT16_MIN},
  {-1,1},{1,-1},{-23177,7654},{12345,-30123},{0,0},{32767,32767}};
 for(unsigned p=0;p<sizeof(pairs)/sizeof(pairs[0]);p++) {
  dd_init(&engine);atomic_store(&engine.master,256);
  struct dd_deck *d=&engine.deck[0];dd_reset_deck(d,15);
  atomic_store(&d->playing,1);d->envelope=256;
  for(unsigned s=0;s<4;s++) {
   frames[0]=(struct dd_frame){pairs[p][0],pairs[p][1]};
   frames[1]=(struct dd_frame){pairs[p][1],pairs[p][0]};
   assert(dd_push(d,s,frames,2));d->voice[s].current_gain=256;
  }
  for(uint32_t f=0;f<65536;f++) {
   atomic_store(&d->read,0);d->fraction=f;
   dd_render(&engine,out,1);
   int32_t delta=(int32_t)pairs[p][1]-pairs[p][0];
   assert(out[0]==pairs[p][0]+(int64_t)delta*f/65536);
   assert(out[1]==pairs[p][1]-(int64_t)delta*f/65536);
  }
 }
}
/* Deliberately simple 64-bit sample-at-a-time oracle. Unlike the production
 * renderer, it has no steady-block shortcut. It specifies exact rounding,
 * gain/envelope ramp ordering and starvation recovery for optimisation tests. */
static uint32_t reference_bound(uint32_t n,uint32_t lo,uint32_t hi)
{ return n<lo?lo:n>hi?hi:n; }
static int16_t reference_clamp(int32_t n,uint32_t *clips)
{
 if(n>INT16_MAX) {++*clips;return INT16_MAX;}
 if(n<INT16_MIN) {++*clips;return INT16_MIN;}
 return (int16_t)n;
}
static int32_t reference_interpolate(int16_t a,int16_t b,uint32_t fraction)
{ return a+(int64_t)((int32_t)b-a)*fraction/65536; }
static void reference_render(struct dd_engine *e,int16_t *out,uint32_t count)
{
 uint32_t pos[2],available[2],step[2]; bool play[2],any[2]={false,false};
 uint16_t gain[2][4]; uint32_t master=reference_bound(atomic_load(&e->master),0,256);
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&e->deck[k];
  pos[k]=atomic_load_explicit(&d->read,memory_order_relaxed); available[k]=DD_RING;
  step[k]=reference_bound(atomic_load(&d->speed),32768,81920)/2;
  play[k]=atomic_load(&d->playing)!=0; uint32_t mute=atomic_load(&d->mute_mask);
  for(unsigned s=0;s<4;s++) {
   struct dd_voice *v=&d->voice[s];
   gain[k][s]=(mute&(1u<<s))?0:reference_bound(atomic_load(&v->gain),0,256);
   if(!v->present) continue;
   any[k]=true;
   uint32_t n=atomic_load_explicit(&v->written,memory_order_acquire)-pos[k];
   if(n>DD_RING) n=0;
   if(n<available[k]) available[k]=n;
  }
 }
 for(uint32_t f=0;f<count;f++) {
  int32_t ml=0,mr=0;
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&e->deck[k];
   bool buffered=available[k]>=(d->starved?512u:2u);
   if(play[k]&&any[k]&&!buffered) {
    if(!d->starved) {++d->underruns;d->starved=true;}
   } else if(buffered) d->starved=false;
   bool running=play[k]&&any[k]&&buffered;
   if(running&&d->envelope<256) ++d->envelope;
   if(!running&&d->envelope) --d->envelope;
   int32_t dl=d->last.l,dr=d->last.r;
   if(running) {
    dl=dr=0;
    for(unsigned s=0;s<4;s++) {
     struct dd_voice *v=&d->voice[s];
     if(!v->present) continue;
     if(v->current_gain<gain[k][s]) ++v->current_gain;
     else if(v->current_gain>gain[k][s]) --v->current_gain;
     /* Keep filling/advancing every stem so it can rejoin in phase. A silent
      * voice needs no sample interpolation once its gain ramp reaches zero. */
     if(!v->current_gain) continue;
     struct dd_frame a=v->ring[pos[k]&(DD_RING-1)],b=v->ring[(pos[k]+1)&(DD_RING-1)];
     int32_t l=reference_interpolate(a.l,b.l,d->fraction);
     int32_t r=reference_interpolate(a.r,b.r,d->fraction);
     dl+=l*v->current_gain; dr+=r*v->current_gain;
    }
    /* 12dB fixed headroom, then saturate the final two-deck bus. */
    dl/=1024; dr/=1024; d->last=(struct dd_wide_frame){dl,dr};
    d->fraction+=step[k]; uint32_t advance=d->fraction>>16; d->fraction&=65535;
    pos[k]+=advance; available[k]-=advance;
   }
   ml+=dl*d->envelope/256; mr+=dr*d->envelope/256;
  }
  out[2*f]=reference_clamp(ml*(int32_t)master/256,&e->clips);
  out[2*f+1]=reference_clamp(mr*(int32_t)master/256,&e->clips);
 }
 for(unsigned k=0;k<2;k++) atomic_store_explicit(&e->deck[k].read,pos[k],memory_order_release);
}
static struct dd_engine reference_engine;
static int16_t reference_out[(DD_RING+1)*2];
static uint32_t random_state=0x8b0a51u;
static uint32_t random_u32(void)
{ random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static void test_render_differential(void)
{
 static const uint32_t counts[]={0,1,2,31,32,33,255,256,257,512,4096,4097};
 static const uint32_t rates[]={0,32768,65536,81920,UINT32_MAX,73531};
 for(unsigned trial=0;trial<2400;trial++) {
  dd_init(&engine);atomic_store(&engine.master,random_u32()%320);
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&engine.deck[k];bool steady=trial%3!=0;
   dd_reset_deck(d,steady?15:random_u32()&15);
   uint32_t pos=trial%2?UINT32_MAX-(random_u32()%DD_RING):random_u32();
   atomic_store(&d->read,pos);atomic_store(&d->source_clock,random_u32());atomic_store(&d->speed,rates[trial%6]);
   atomic_store(&d->playing,steady?1:random_u32()%2);
   atomic_store(&d->mute_mask,steady?0:random_u32()&15);
   d->fraction=trial%4==0?0:trial%4==1?32768:random_u32()&65535;
   d->envelope=steady?256:random_u32()%257;d->starved=!steady&&(random_u32()%2);
   d->last=(struct dd_wide_frame){(int16_t)random_u32(),(int16_t)random_u32()};
   for(unsigned s=0;s<4;s++) {
    struct dd_voice *v=&d->voice[s];uint32_t gain=random_u32()%257;
    atomic_store(&v->gain,gain);v->current_gain=steady?gain:random_u32()%257;
    uint32_t avail=steady?DD_RING:random_u32()%700;
    if(steady&&trial%4>=2) {
     uint32_t step=reference_bound(atomic_load(&d->speed),32768,81920)/2;
     unsigned first_count=counts[trial%12];
     avail=first_count?2+((d->fraction+(first_count-1)*step)>>16):2;
     if(trial%4==3) --avail; /* Exactly enough data, then one frame too few. */
    }
    atomic_store(&v->written,pos+avail);
    for(unsigned i=0;i<DD_RING;i++) v->ring[i]=(struct dd_frame){(int16_t)random_u32(),(int16_t)random_u32()};
   }
  }
  memcpy(&reference_engine,&engine,sizeof(engine));
  for(unsigned pass=0;pass<3;pass++) {
   unsigned n=counts[(trial+pass)%12];
   uint32_t clocks[2],reads[2];
   for(unsigned k=0;k<2;k++) {
    clocks[k]=atomic_load(&engine.deck[k].source_clock);
    reads[k]=atomic_load(&reference_engine.deck[k].read);
   }
   dd_render(&engine,out,n);reference_render(&reference_engine,reference_out,n);
   assert(!memcmp(out,reference_out,n*2*sizeof(*out)));
   assert(engine.clips==reference_engine.clips);
   for(unsigned k=0;k<2;k++) {
    struct dd_deck *a=&engine.deck[k],*b=&reference_engine.deck[k];
    assert(atomic_load(&a->read)==atomic_load(&b->read));
    assert(atomic_load(&a->source_clock)==clocks[k]+atomic_load(&b->read)-reads[k]);
    assert(a->fraction==b->fraction&&a->underruns==b->underruns);
    assert(a->starved==b->starved&&a->envelope==b->envelope);
    assert(a->last.l==b->last.l&&a->last.r==b->last.r);
    for(unsigned s=0;s<4;s++) assert(a->voice[s].current_gain==b->voice[s].current_gain);
   }
  }
 }
}
static void compare_render_state(void)
{
 assert(engine.clips==reference_engine.clips);
 for(unsigned k=0;k<2;k++) {
  const struct dd_deck *a=&engine.deck[k],*b=&reference_engine.deck[k];
  assert(atomic_load(&a->read)==atomic_load(&b->read));
  assert(a->fraction==b->fraction&&a->underruns==b->underruns);
  assert(a->starved==b->starved&&a->envelope==b->envelope);
  assert(a->last.l==b->last.l&&a->last.r==b->last.r);
  for(unsigned s=0;s<4;s++) assert(a->voice[s].current_gain==b->voice[s].current_gain);
 }
}
static void test_final_bus_headroom_and_pause(void)
{
 /* Four correlated full-scale sources on each deck expose both intermediate
  * sums. The two-deck bus stays wide until master gain and final saturation. */
 for(unsigned mode=0;mode<3;mode++) {
  dd_init(&engine);unsigned master=mode==0?128:256;atomic_store(&engine.master,master);
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&engine.deck[k];dd_reset_deck(d,15);
   atomic_store(&d->playing,1);d->envelope=256;
   struct dd_frame input=mode==2&&k==1?(struct dd_frame){INT16_MIN,INT16_MAX}:
                                               (struct dd_frame){INT16_MAX,INT16_MIN};
   for(unsigned i=0;i<DD_RING;i++)frames[i]=input;
   for(unsigned s=0;s<4;s++) {
    atomic_store(&d->voice[s].gain,256);d->voice[s].current_gain=256;
    assert(dd_push(d,s,frames,DD_RING));
   }
  }
  memcpy(&reference_engine,&engine,sizeof(engine));
  dd_render(&engine,out,256);reference_render(&reference_engine,reference_out,256);
  assert(!memcmp(out,reference_out,512*sizeof(*out)));compare_render_state();
  assert(engine.deck[0].last.l==INT16_MAX&&engine.deck[0].last.r==INT16_MIN);
  if(mode==0) {
   assert(out[0]==INT16_MAX&&out[1]==INT16_MIN&&!engine.clips);
  } else if(mode==1) {
   assert(out[0]==INT16_MAX&&out[1]==INT16_MIN&&engine.clips==512);
  } else assert(out[0]==-1&&out[1]==-1&&!engine.clips);
  /* Pausing fades from the previous sample, then must preserve B's playhead
   * and last frame during subsequent steady, A-only callbacks. */
  atomic_store(&engine.deck[1].playing,0);atomic_store(&reference_engine.deck[1].playing,0);
  uint32_t held=atomic_load(&engine.deck[1].read);
  for(unsigned block=0;block<3;block++) {
   dd_render(&engine,out,256);reference_render(&reference_engine,reference_out,256);
   assert(!memcmp(out,reference_out,512*sizeof(*out)));compare_render_state();
   assert(atomic_load(&engine.deck[1].read)==held);
  }
 }
}
static void test_master_changes_preserve_transport(void)
{
 dd_init(&engine);
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&engine.deck[k];dd_reset_deck(d,15);atomic_store(&d->playing,1);d->envelope=256;
  for(unsigned s=0;s<4;s++) {
   atomic_store(&d->voice[s].gain,256);d->voice[s].current_gain=256;fill(k,s,8000,DD_RING);
  }
 }
 memcpy(&reference_engine,&engine,sizeof(engine));
 const unsigned master[]={128,120,8,0,256,UINT32_MAX};
 for(unsigned m=0;m<sizeof(master)/sizeof(master[0]);m++) {
  atomic_store(&engine.master,master[m]);atomic_store(&reference_engine.master,master[m]);
  dd_render(&engine,out,256);reference_render(&reference_engine,reference_out,256);
  assert(!memcmp(out,reference_out,512*sizeof(*out)));compare_render_state();
  unsigned expected=16000*reference_bound(master[m],0,256)/256;
  for(unsigned f=0;f<256;f++)assert(out[f*2]==(int)expected&&out[f*2+1]==-(int)expected);
  assert(!engine.deck[0].underruns&&!engine.deck[1].underruns&&!engine.clips);
 }
}
static void refill_stretch_rings(void)
{
 for(unsigned k=0;k<2;k++)for(unsigned s=0;s<4;s++) {
  struct dd_deck *d=&engine.deck[k];
  if(!d->voice[s].present)continue;
  while(dd_room(d,s)>=248) {
   uint32_t p=atomic_load(&d->voice[s].written);
   for(unsigned i=0;i<248;i++) {
    int16_t l=(int16_t)(((p+i)*37+s*131+k*311)%24001-12000);
    frames[i]=(struct dd_frame){l,(int16_t)-l};
   }
   assert(dd_push(d,s,frames,248));
  }
 }
}
static void service_stretch(void)
{
 refill_stretch_rings();
 for(unsigned turn=0;turn<64;turn++) {
  uint32_t used=dd_stretch_service(&engine,1024);assert(used<=1024);
  if(!used)break;
 }
}
static void test_stretch_finite_rings_and_coalescing(void)
{
 dd_init(&engine);
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&engine.deck[k];dd_reset_deck(d,15);dd_set_stretch(d,true);
  for(unsigned s=0;s<4;s++)atomic_store(&d->voice[s].gain,256);
  atomic_store(&d->playing,1);
 }
 /* Untouched1x remains the original renderer and performs no worker work. */
 refill_stretch_rings();assert(!dd_stretch_service(&engine,1024));dd_render(&engine,out,513);
 assert(atomic_load(&engine.deck[0].source_clock)==256);
 assert(engine.deck[0].fraction==32768); /* deliberately enter from dry midpoint */
 assert(!atomic_load(&engine.deck[0].stretch.active));
 assert(dd_set_tempo(&engine.deck[0],63570)&&dd_set_tempo(&engine.deck[1],67502));service_stretch();
 for(unsigned block=0;block<1200;block++) {
  /* Updating requested tempo faster than a grain must never cancel its plan. */
  assert(dd_set_tempo(&engine.deck[0],63570+(block&1)));
  assert(dd_set_tempo(&engine.deck[1],67502+(block&1)));
  service_stretch();dd_render(&engine,out,256);
  assert(engine.deck[0].fraction==0&&engine.deck[1].fraction==0);
  for(unsigned k=0;k<2;k++) {
   struct dd_deck *d=&engine.deck[k];struct bs_state *s=&d->stretch;
   assert(!d->starved&&!d->underruns);assert(d->fraction==0||d->fraction==32768);
   uint64_t phase=(uint64_t)atomic_load(&s->fraction)*2+
     ((uint64_t)atomic_load(&s->cursor)*2+(d->fraction==32768))*bs_actual(s);
   uint32_t expected=atomic_load(&s->nominal)+(uint32_t)(phase>>17);
   assert(atomic_load(&d->source_clock)==expected);
   assert(atomic_load(&d->voice[0].written)-atomic_load(&d->read)<=DD_RING);
  }
  if(block==450)atomic_store(&engine.deck[0].voice[0].gain,0);
  if(block==454)assert(!engine.deck[0].voice[0].current_gain);
 }
 uint32_t paused=atomic_load(&engine.deck[1].source_clock);
 atomic_store(&engine.deck[1].playing,0);service_stretch();dd_render(&engine,out,512);
 assert(atomic_load(&engine.deck[1].source_clock)==paused);
 /* Resetting the current worker's deck to an empty slot must not strand B. */
 struct dd_deck *a=&engine.deck[0],*b=&engine.deck[1];
 atomic_store(&a->stretch.ready,false);atomic_store(&a->stretch.cursor,BS_OVERLAP);
 engine.stretch_work.owner=NULL;refill_stretch_rings();
 assert(dd_stretch_service(&engine,64)>0&&engine.stretch_work.owner==&a->stretch);
 dd_reset_deck(a,0);atomic_store(&b->stretch.ready,false);atomic_store(&b->stretch.cursor,BS_OVERLAP);
 service_stretch();assert(engine.stretch_work.owner!=&a->stretch);
 assert(atomic_load(&b->stretch.ready));
}
int main(void)
{
 test_eight_stems();test_starvation_holds_deck();test_wrap_speed_and_bounds();test_independent_source_clock();
 test_pickup();test_codec();test_saturation();test_interpolation_full_range();test_render_differential();
 test_final_bus_headroom_and_pause();test_master_changes_preserve_transport();
 test_stretch_finite_rings_and_coalescing();
 puts("PASS: eight distinct stems, independent decks, mute/gain ramps, starvation sync, ring wrap, speed bounds, pickup, codecs, full-range interpolation, final-bus clipping, master changes and independent source/reclaim clocks");
}
