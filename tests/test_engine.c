#include "dual_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static struct dd_engine engine;
static struct dd_frame frames[DD_RING];
static int16_t out[1024];
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
int main(void)
{
 test_eight_stems();test_starvation_holds_deck();test_wrap_speed_and_bounds();
 test_pickup();test_codec();test_saturation();
 puts("PASS: eight distinct stems, independent decks, mute/gain ramps, starvation sync, ring wrap, speed bounds, pickup, codecs, clipping");
}
