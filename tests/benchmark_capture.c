/* SPDX-License-Identifier: MIT */
/* Host CPU comparison only; this is not an nRF52840 cycle measurement. */
#include "dual_capture.h"
#include <stdio.h>
#include <time.h>

static struct dd_capture capture;
static int16_t input[2048],output[98];
static volatile uint32_t checksum;
static __attribute__((noinline)) bool scalar_push(struct dd_capture *c,const int16_t *in,uint32_t n)
{
 uint32_t w=atomic_load(&c->written),r=atomic_load(&c->read);
 if(n>DD_CAPTURE_FRAMES||w-r>DD_CAPTURE_FRAMES-n){c->overflows++;return false;}
 for(uint32_t i=0;i<n;i++)c->ring[(w+i)%DD_CAPTURE_FRAMES]=(struct dd_frame){in[2*i],in[2*i+1]};
 atomic_store(&c->written,w+n);return true;
}
static __attribute__((noinline)) uint32_t scalar_packet(struct dd_capture *c,int16_t out[98])
{
 uint32_t r=atomic_load(&c->read),available=atomic_load(&c->written)-r;
 if(!c->primed){if(available<768)return 0;c->primed=true;}
 c->level_q8=(c->level_q8*63+available*256)/64;
 uint32_t n=c->level_q8>800*256?49:c->level_q8<736*256?47:48;
 if(available<n){c->underruns++;c->primed=false;return 0;}
 for(uint32_t i=0;i<n;i++){struct dd_frame f=c->ring[(r+i)%DD_CAPTURE_FRAMES];out[2*i]=f.l;out[2*i+1]=f.r;}
 atomic_store(&c->read,r+n);return n;
}
static double run(bool (*push)(struct dd_capture *,const int16_t *,uint32_t),
                  uint32_t (*packet)(struct dd_capture *,int16_t *))
{
 dd_capture_reset(&capture);push(&capture,input,1024);
 clock_t start=clock();
 for(unsigned i=0;i<1000000;i++){
  if(!push(&capture,input,256))return -1;
  while(atomic_load(&capture.written)-atomic_load(&capture.read)>1024){
   if(!packet(&capture,output))return -1;
   checksum+=output[0];
  }
 }
 return (double)(clock()-start)/CLOCKS_PER_SEC;
}
int main(void)
{
 for(unsigned i=0;i<2048;i++)input[i]=(int16_t)(i*7919u);
 double scalar=run(scalar_push,scalar_packet),bulk=run(dd_capture_push,dd_capture_packet);
 printf("Host CPU seconds, 1M x 256 frames with packet draining: scalar %.6f, bulk %.6f, ratio %.3f\n",scalar,bulk,bulk/scalar);
 return scalar<0||bulk<0;
}
