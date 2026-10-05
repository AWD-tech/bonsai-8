#include "dual_capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct dd_capture c;
static struct dd_capture reference;
static int16_t samples[4096],packet[98];
/* Independent scalar model of the original producer/packet algorithm. This
 * detects changes to packet sizing, clock adaptation or ring publication as
 * well as copying at the physical ring and uint32 counter wrap boundaries. */
static bool reference_push(struct dd_capture *d,const int16_t *in,uint32_t n)
{
 uint32_t w=atomic_load(&d->written),r=atomic_load(&d->read);
 if(n>DD_CAPTURE_FRAMES||w-r>DD_CAPTURE_FRAMES-n){d->overflows++;return false;}
 for(uint32_t i=0;i<n;i++)d->ring[(w+i)%DD_CAPTURE_FRAMES]=(struct dd_frame){in[2*i],in[2*i+1]};
 atomic_store(&d->written,w+n);return true;
}
static uint32_t reference_packet(struct dd_capture *d,int16_t out[98])
{
 uint32_t r=atomic_load(&d->read),available=atomic_load(&d->written)-r;
 if(!d->primed){if(available<768)return 0;d->primed=true;}
 d->level_q8=(d->level_q8*63+available*256)/64;
 uint32_t n=d->level_q8>800*256?49:d->level_q8<736*256?47:48;
 if(available<n){d->underruns++;d->primed=false;return 0;}
 for(uint32_t i=0;i<n;i++){struct dd_frame f=d->ring[(r+i)%DD_CAPTURE_FRAMES];out[2*i]=f.l;out[2*i+1]=f.r;}
 atomic_store(&d->read,r+n);return n;
}
static void same_state(void)
{
 assert(atomic_load(&c.written)==atomic_load(&reference.written));
 assert(atomic_load(&c.read)==atomic_load(&reference.read));
 assert(c.underruns==reference.underruns&&c.overflows==reference.overflows);
 assert(c.primed==reference.primed&&c.level_q8==reference.level_q8);
 assert(!memcmp(c.ring,reference.ring,sizeof(c.ring)));
}
static void differential(void)
{
 uint32_t random=0x81a39fbdu;
 static const uint32_t starts[]={0,2016,2047,0xfffffc00u,0xfffffff0u};
 for(unsigned s=0;s<sizeof(starts)/sizeof(starts[0]);s++){
  dd_capture_reset(&c);dd_capture_reset(&reference);
  memset(c.ring,0,sizeof(c.ring));memset(reference.ring,0,sizeof(reference.ring));
  c.underruns=reference.underruns=0;c.overflows=reference.overflows=0;
  atomic_store(&c.read,starts[s]);atomic_store(&c.written,starts[s]);
  atomic_store(&reference.read,starts[s]);atomic_store(&reference.written,starts[s]);
  assert(dd_capture_push(&c,NULL,0));assert(reference_push(&reference,NULL,0));
  assert(!dd_capture_push(&c,NULL,DD_CAPTURE_FRAMES+1));
  assert(!reference_push(&reference,NULL,DD_CAPTURE_FRAMES+1));same_state();
  for(unsigned i=0;i<20000;i++){
   random=random*1664525u+1013904223u;
   if((random>>28)<3){
    uint32_t n=(random>>8)%2049;
    for(uint32_t j=0;j<n*2;j++)samples[j]=(int16_t)(random+j*7919u);
    assert(dd_capture_push(&c,samples,n)==reference_push(&reference,samples,n));
   }else{
    int16_t expected[98];memset(packet,0x5a,sizeof(packet));memset(expected,0x5a,sizeof(expected));
    assert(dd_capture_packet(&c,packet)==reference_packet(&reference,expected));
    assert(!memcmp(packet,expected,sizeof(packet)));
   }
   same_state();
  }
 }
}
int main(void)
{
 for(unsigned i=0;i<2048;i++){samples[i*2]=i;samples[i*2+1]=-(int)i;}
 dd_capture_reset(&c);assert(dd_capture_packet(&c,packet)==0&&c.underruns==0);
 assert(dd_capture_push(&c,samples,1024));unsigned consumed=0;
 for(unsigned i=0;i<12;i++){
  unsigned n=dd_capture_packet(&c,packet);assert(n>=47&&n<=49);
  for(unsigned j=0;j<n;j++){assert(packet[j*2]==(int)(consumed+j));assert(packet[j*2+1]==-(int)(consumed+j));}consumed+=n;
 }
 while(dd_capture_packet(&c,packet)) {}
 assert(c.underruns==1);
 dd_capture_reset(&c);assert(dd_capture_push(&c,samples,2048));assert(!dd_capture_push(&c,samples,1));assert(c.overflows==1);
 dd_capture_reset(&c);atomic_store(&c.written,0xfffffff0);atomic_store(&c.read,0xfffffff0);
 assert(dd_capture_push(&c,samples,1024));assert(dd_capture_packet(&c,packet)>=47&&packet[0]==0);
 differential();
 puts("PASS: USB capture exact scalar differential, stereo/frame order, bounds, underflow, overflow, ring/counter wraps and zero-length input");
}
