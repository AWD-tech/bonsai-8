/* SPDX-License-Identifier: MIT */
#ifndef SP1_DUAL_CAPTURE_H
#define SP1_DUAL_CAPTURE_H
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "dual_engine.h"
#define DD_CAPTURE_FRAMES 2048u
struct dd_capture {
 struct dd_frame ring[DD_CAPTURE_FRAMES];
 _Atomic uint32_t written,read;
 uint32_t underruns,overflows,level_q8;
 bool primed;
};
/* Reset only with producer and consumer excluded. Thereafter single producer,
 * single consumer; host monitoring can never block the physical audio thread. */
void dd_capture_reset(struct dd_capture *c);
bool dd_capture_push(struct dd_capture *c,const int16_t *stereo,uint32_t frames);
uint32_t dd_capture_packet(struct dd_capture *c,int16_t out[98]);
#endif
