/* SPDX-License-Identifier: MIT */
#include "dual_capture.h"
void dd_capture_reset(struct dd_capture *c)
{
 atomic_store(&c->written,0);atomic_store(&c->read,0);c->primed=false;c->level_q8=768u*256;
}
bool dd_capture_push(struct dd_capture *c,const int16_t *stereo,uint32_t frames)
{
 uint32_t w=atomic_load(&c->written),r=atomic_load(&c->read);
 if(frames>DD_CAPTURE_FRAMES||w-r>DD_CAPTURE_FRAMES-frames) {c->overflows++;return false;}
 for(uint32_t i=0;i<frames;i++)c->ring[(w+i)%DD_CAPTURE_FRAMES]=(struct dd_frame){stereo[i*2],stereo[i*2+1]};
 atomic_store(&c->written,w+frames);return true;
}
uint32_t dd_capture_packet(struct dd_capture *c,int16_t out[98])
{
 uint32_t r=atomic_load(&c->read),available=atomic_load(&c->written)-r;
 if(!c->primed) {if(available<768)return 0;c->primed=true;}
 c->level_q8=(c->level_q8*63+available*256)/64;
 /* Asynchronous UAC2 allows 47/48/49 frames per 1ms packet. This follows
  * the device oscillator without deleting/repeating individual samples. */
 uint32_t frames=c->level_q8>(768+32)*256?49:c->level_q8<(768-32)*256?47:48;
 if(available<frames) {c->underruns++;c->primed=false;return 0;}
 for(uint32_t i=0;i<frames;i++){struct dd_frame f=c->ring[(r+i)%DD_CAPTURE_FRAMES];out[i*2]=f.l;out[i*2+1]=f.r;}
 atomic_store(&c->read,r+frames);return frames;
}
