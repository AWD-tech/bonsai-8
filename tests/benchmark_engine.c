/* Host-only relative timing probe. This cannot establish SP-1 deadlines. */
#include "dual_engine.h"
#include "bonsai_fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static struct dd_engine engine;
static struct bonsai_fx effects;
static int16_t output[512];
int main(int argc, char **argv)
{
 unsigned blocks=argc>1?(unsigned)strtoul(argv[1],NULL,10):100000;
 unsigned speed=argc>2?(unsigned)strtoul(argv[2],NULL,10):65536;
 unsigned effect=argc>3?(unsigned)strtoul(argv[3],NULL,10):0;
 dd_init(&engine);atomic_store(&engine.master,128);
 if(effect) {
  if(effect>BONSAI_FX_REVERB) {fputs("effect must be 0..3\n",stderr);return 2;}
  bonsai_fx_init(&effects);engine.fx=&effects;
  for(unsigned i=0;i<8;i++) bonsai_fx_set(&effects,i,(enum bonsai_fx_type)effect,128);
 }
 for(unsigned k=0;k<2;k++) {
  struct dd_deck *d=&engine.deck[k];dd_reset_deck(d,15);
  atomic_store(&d->playing,1);atomic_store(&d->speed,speed);d->envelope=256;
  for(unsigned s=0;s<4;s++) {
   struct dd_voice *v=&d->voice[s];
   atomic_store(&v->gain,256-s*19);v->current_gain=256-s*19;
   for(unsigned i=0;i<DD_RING;i++) v->ring[i]=(struct dd_frame){(int16_t)(i*37+s*11),(int16_t)(i*31-s*7)};
  }
 }
 clock_t start=clock();uint32_t checksum=0;
 for(unsigned i=0;i<blocks;i++) {
  for(unsigned k=0;k<2;k++) for(unsigned s=0;s<4;s++)
   atomic_store(&engine.deck[k].voice[s].written,atomic_load(&engine.deck[k].read)+DD_RING);
  dd_render(&engine,output,256);checksum+=(uint16_t)output[511];
 }
 printf("host-only: %u blocks, speed %u, effect %u, %.6f CPU seconds, checksum %u\n",blocks,speed,effect,(double)(clock()-start)/CLOCKS_PER_SEC,checksum);
}
