/* SPDX-License-Identifier: MIT */
#include "dual_capture.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(struct dd_frame) == 2u * sizeof(int16_t) &&
               offsetof(struct dd_frame, r) == sizeof(int16_t),
               "Capture copies require contiguous interleaved stereo frames");
static inline void copy_frames(void *destination,const void *source,uint32_t frames)
{
 unsigned char *out=destination;
 const unsigned char *in=source;
 /* Fixed-size copies compile to word loads/stores on the Cortex-M4, without
  * pointer alias/alignment assumptions. The linked size-optimized memcpy is
  * a byte loop, so a variable-size libc copy costs more than the old loop. */
 for(uint32_t i=0;i<frames;i++)
  memcpy(out+i*sizeof(struct dd_frame),in+i*sizeof(struct dd_frame),sizeof(struct dd_frame));
}
void dd_capture_reset(struct dd_capture *c)
{
 atomic_store(&c->written,0);atomic_store(&c->read,0);c->primed=false;c->level_q8=768u*256;
}
bool dd_capture_push(struct dd_capture *c,const int16_t *stereo,uint32_t frames)
{
 uint32_t w=atomic_load(&c->written),r=atomic_load(&c->read);
 if(frames>DD_CAPTURE_FRAMES||w-r>DD_CAPTURE_FRAMES-frames) {c->overflows++;return false;}
 /* Publish only after both spans have been copied. A ring wrap needs at most
  * two copies; this also keeps the producer's IRQ-excluded section bounded
  * without per-frame wrap/index arithmetic. Zero frames accepts a null input. */
 if(frames) {
  uint32_t position=w%DD_CAPTURE_FRAMES,first=DD_CAPTURE_FRAMES-position;
  if(first>frames)first=frames;
  copy_frames(&c->ring[position],stereo,first);
  if(first<frames)copy_frames(c->ring,stereo+first*2,frames-first);
 }
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
 uint32_t position=r%DD_CAPTURE_FRAMES,first=DD_CAPTURE_FRAMES-position;
 if(first>frames)first=frames;
 copy_frames(out,&c->ring[position],first);
 if(first<frames)copy_frames(out+first*2,c->ring,frames-first);
 atomic_store(&c->read,r+frames);return frames;
}
