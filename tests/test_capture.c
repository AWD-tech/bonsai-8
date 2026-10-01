#include "dual_capture.h"
#include <assert.h>
#include <stdio.h>
static struct dd_capture c;
static int16_t samples[4096],packet[98];
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
 puts("PASS: USB capture preserves stereo samples, frame order, bounds, underflow, overflow and counter wrap");
}
