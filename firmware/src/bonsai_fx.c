/* SPDX-License-Identifier: MIT */
#include "bonsai_fx.h"
#include <string.h>

_Static_assert(sizeof(struct bonsai_fx) == 224u,
               "Eight-stem filter bank exceeds its fixed 224-byte RAM budget");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "FX controls require lock-free atomics");

void bonsai_fx_init(struct bonsai_fx *fx)
{
 memset(fx->voice, 0, sizeof(fx->voice));
 for (unsigned i = 0; i < BONSAI_FX_VOICES; ++i)
  atomic_init(&fx->config[i], 0);
}
bool bonsai_fx_reset_voice(struct bonsai_fx *fx, unsigned voice)
{
 if (voice >= BONSAI_FX_VOICES) return false;
 struct bonsai_fx_voice *v = &fx->voice[voice];
 v->type = v->requested_type = BONSAI_FX_NONE;
 v->requested_amount = v->mix = v->alpha = v->target_alpha = 0;
 v->low_l = v->low_r = 0;
 v->filter_seeded = false;
 return true;
}
bool bonsai_fx_set(struct bonsai_fx *fx, unsigned voice,
                   enum bonsai_fx_type type, unsigned amount)
{
 if (voice >= BONSAI_FX_VOICES || (unsigned)type > BONSAI_FX_FILTER) return false;
 if (amount > 256u) amount = 256u;
 if (type == BONSAI_FX_NONE) amount = 0;
 atomic_store_explicit(&fx->config[voice], ((uint32_t)type << 16) | amount,
                       memory_order_release);
 return true;
}
void bonsai_fx_begin_block(struct bonsai_fx *fx)
{
 /* Bilinear, Nyquist-normalized one-pole high-pass coefficients. Seventeen
  * log-spaced cutoff anchors cover 60 Hz -> 8 kHz; Q8 endpoints are 59.9 Hz
  * and 7978 Hz. The lower bound retains useful bass resolution in 32-bit DSP. */
 static const uint16_t alpha[17] = {
  2,3,4,5,7,9,12,17,22,30,40,52,69,90,116,148,187
 };
 for (unsigned i = 0; i < BONSAI_FX_VOICES; ++i) {
  struct bonsai_fx_voice *v = &fx->voice[i];
  uint32_t config = atomic_load_explicit(&fx->config[i], memory_order_acquire);
  unsigned amount = config & 0xffffu, type = config >> 16;
  /* Invalid directly-written/corrupted configurations fail closed to bypass.
   * The public setter rejects retired IDs 2/3 without changing any setting. */
  if (type > BONSAI_FX_FILTER || amount > 256u) { type = BONSAI_FX_NONE; amount = 0; }
  v->requested_type = amount ? (uint8_t)type : BONSAI_FX_NONE;
  v->requested_amount = (uint16_t)amount;
  if (v->requested_type == BONSAI_FX_FILTER) {
   unsigned index = amount / 16u, part = amount % 16u;
   v->target_alpha = index == 16u ? alpha[16] :
    (uint16_t)((alpha[index] * (16u - part) + alpha[index + 1u] * part) / 16u);
  }
 }
}

static void activate(struct bonsai_fx_voice *v)
{
 v->type = v->requested_type;
 v->mix = 0;
 if (v->type == BONSAI_FX_FILTER) v->alpha = v->target_alpha;
 v->filter_seeded = false;
}
static int32_t lowpass(int32_t previous, int16_t sample, uint16_t alpha)
{
 int32_t difference = (int32_t)sample * 256 - previous;
 uint32_t magnitude = (uint32_t)(difference < 0 ? -difference : difference);
 /* Q8 state retains quiet-signal precision. The unsigned product fits:
  * maximum magnitude=65535*256, coefficient<=232. No 64-bit DSP. */
 int32_t step = (int32_t)((magnitude * alpha) >> 8);
 return previous + (difference < 0 ? -step : step);
}
static int32_t lowpass_small_alpha(int32_t previous,int16_t sample,uint16_t alpha)
{
 /* The state stays inside int16 * 256. For alpha <= 128 the signed product
  * is bounded by 65535 * 256 * 128 = 2147450880, below INT32_MAX.
  * C's division truncates toward zero, exactly matching the magnitude path.
  * Hoisting the coefficient bound outside a steady block saves sign/absolute
  * reconstruction on both channels without changing a sample or tone. */
 int32_t difference=(int32_t)sample*256-previous;
 return previous+difference*alpha/256;
}
static void advance_filter(struct bonsai_fx_voice *v)
{
 /* One Q8 coefficient step per sample bounds a full-range sweep to 3.86 ms.
  * The steady buffer path hoists this check once it reaches its target. */
 if (v->alpha < v->target_alpha) ++v->alpha;
 else if (v->alpha > v->target_alpha) --v->alpha;
}
static int32_t highpass(int16_t input,int32_t previous,uint16_t alpha)
{
 /* beta=(1+p)/2=(512-alpha)/512 gives unity Nyquist gain, without resonance
  * or a blanket wet attenuation. Difference <=65535; product <=33422850.
  * Preserve wide polarity transients until stem gain and final bus limiting. */
 return ((int32_t)input-previous/256)*(512-alpha)/512;
}
struct dd_wide_frame bonsai_fx_process(struct bonsai_fx_voice *v,
                                  struct dd_frame input)
{
 if (v->type != v->requested_type && !v->mix) activate(v);
 if (v->type == BONSAI_FX_NONE) return (struct dd_wide_frame){input.l,input.r};
 unsigned target = v->type != v->requested_type ? 0 : 256;
 if (v->mix < target) ++v->mix;
 else if (v->mix > target) --v->mix;
 advance_filter(v);
 if (!v->filter_seeded) {
  v->low_l = (int32_t)input.l * 256;
  v->low_r = (int32_t)input.r * 256;
  v->filter_seeded = true;
 }
 int32_t hl=highpass(input.l,v->low_l,v->alpha);
 int32_t hr=highpass(input.r,v->low_r,v->alpha);
 v->low_l = lowpass(v->low_l, input.l, v->alpha);
 v->low_r = lowpass(v->low_r, input.r, v->alpha);
 return (struct dd_wide_frame){
  input.l+(hl-input.l)*v->mix/256,
  input.r+(hr-input.r)*v->mix/256
 };
}

void bonsai_fx_process_buffer(struct bonsai_fx_voice *v,
                              struct dd_wide_frame *frames, uint32_t count)
{
 uint32_t i=0;
 for(;i<count;i++) {
  if(v->type==v->requested_type) {
   if(v->type==BONSAI_FX_NONE) return;
   if(v->mix==256&&v->filter_seeded) break;
  }
  frames[i]=bonsai_fx_process(v,(struct dd_frame){frames[i].l,frames[i].r});
 }
 if(i==count) return;
 int32_t l=v->low_l,r=v->low_r;
 for(;i<count&&v->alpha!=v->target_alpha;i++) {
  advance_filter(v);
  struct dd_frame input={frames[i].l,frames[i].r};
  frames[i]=(struct dd_wide_frame){highpass(input.l,l,v->alpha),highpass(input.r,r,v->alpha)};
  l=lowpass(l,input.l,v->alpha);r=lowpass(r,input.r,v->alpha);
 }
 uint16_t alpha=v->alpha;
 if(alpha<=128u) {
  for(;i<count;i++) {
   struct dd_frame input={frames[i].l,frames[i].r};
   frames[i]=(struct dd_wide_frame){highpass(input.l,l,alpha),highpass(input.r,r,alpha)};
   l=lowpass_small_alpha(l,input.l,alpha);r=lowpass_small_alpha(r,input.r,alpha);
  }
 } else {
  for(;i<count;i++) {
   struct dd_frame input={frames[i].l,frames[i].r};
   frames[i]=(struct dd_wide_frame){highpass(input.l,l,alpha),highpass(input.r,r,alpha)};
   l=lowpass(l,input.l,alpha);r=lowpass(r,input.r,alpha);
  }
 }
 v->low_l=l;v->low_r=r;
}
