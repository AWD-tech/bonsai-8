/* SPDX-License-Identifier: MIT */
#include "bonsai_fx.h"
#include <limits.h>
#include <string.h>

_Static_assert(sizeof(struct bonsai_fx) <= 18u * 1024u,
               "Eight-stem effects exceed their fixed 18 KiB RAM budget");
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
 v->requested_amount = v->mix = v->alpha = v->target_alpha = v->clear_index = 0;
 v->low_l = v->low_r = v->input_sum = v->weighted_sum = v->previous_sum = 0;
 v->wet_previous = v->wet_next = 0;
 v->phase = 0; v->filter_seeded = false;
 for (unsigned i = 0; i < 6; ++i) v->position[i] = 0;
 return true;
}
bool bonsai_fx_set(struct bonsai_fx *fx, unsigned voice,
                   enum bonsai_fx_type type, unsigned amount)
{
 if (voice >= BONSAI_FX_VOICES || (unsigned)type > BONSAI_FX_REVERB) return false;
 if (amount > 256u) amount = 256u;
 if (type == BONSAI_FX_NONE) amount = 0;
 atomic_store_explicit(&fx->config[voice], ((uint32_t)type << 16) | amount,
                       memory_order_release);
 return true;
}
void bonsai_fx_begin_block(struct bonsai_fx *fx)
{
 /* Q8 one-pole coefficients for logarithmic 18 kHz -> 160 Hz cutoff.
  * Quantization makes the lowest setting approximately 151 Hz. */
 static const uint16_t alpha[17] = {
  232,212,187,159,132,107,85,66,51,39,30,22,17,13,9,7,5
 };
 for (unsigned i = 0; i < BONSAI_FX_VOICES; ++i) {
  struct bonsai_fx_voice *v = &fx->voice[i];
  uint32_t config = atomic_load_explicit(&fx->config[i], memory_order_acquire);
  unsigned amount = config & 0xffffu;
  v->requested_type = amount ? (uint8_t)(config >> 16) : BONSAI_FX_NONE;
  v->requested_amount = (uint16_t)amount;
  if (v->requested_type == BONSAI_FX_FILTER) {
   unsigned index = amount / 16u, part = amount % 16u;
   v->target_alpha = index == 16u ? alpha[16] :
    (uint16_t)((alpha[index] * (16u - part) + alpha[index + 1u] * part) / 16u);
  }
 }
}

static int16_t limit(int32_t sample, struct bonsai_fx_voice *v)
{
 if (sample > INT16_MAX) { ++v->clipped; return INT16_MAX; }
 if (sample < INT16_MIN) { ++v->clipped; return INT16_MIN; }
 return (int16_t)sample;
}
static void activate(struct bonsai_fx_voice *v)
{
 v->type = v->requested_type;
 v->mix = 0;
 v->clear_index = 0;
 v->input_sum = v->weighted_sum = v->previous_sum = 0;
 v->phase = 0;
 if (v->type == BONSAI_FX_FILTER) v->alpha = v->target_alpha;
 v->wet_previous = v->wet_next = 0;
 v->filter_seeded = false;
 for (unsigned i = 0; i < 6; ++i) v->position[i] = 0;
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
static int16_t echo_tick(struct bonsai_fx_voice *v, int16_t input)
{
 unsigned p = v->position[0];
 int16_t delayed = v->history[p];
 v->history[p] = (int16_t)(((int32_t)input + delayed) / 2);
 v->position[0] = p == 999u ? 0 : (uint16_t)(p + 1u);
 return delayed;
}
static int16_t reverb_tick(struct bonsai_fx_voice *v, int16_t input)
{
 static const uint16_t lengths[6] = {149,211,263,293,37,59};
 unsigned offset = 0;
 int32_t sum = 0;
 for (unsigned i = 0; i < 4; ++i) {
  unsigned p = offset + v->position[i];
  int16_t delayed = v->history[p];
  v->history[p] = (int16_t)(((int32_t)input + (int32_t)delayed * 3) / 4);
  sum += delayed;
  if (++v->position[i] == lengths[i]) v->position[i] = 0;
  offset += lengths[i];
 }
 int32_t wet = sum / 4;
 for (unsigned i = 4; i < 6; ++i) {
  unsigned p;
  /* Allpass state needs 1.5x DC headroom. Store each state in two 16-bit
   * history words instead of clipping it before the return signal is mixed.
   * Packing avoids alignment/alias assumptions and stays in the 18 KiB bank. */
  p = offset + 2u * v->position[i];
  int32_t delayed = (int32_t)((uint32_t)(uint16_t)v->history[p] |
                            (uint32_t)(uint16_t)v->history[p + 1u] << 16);
  int32_t next = delayed - wet / 2;
  uint32_t state = (uint32_t)(wet + next / 2);
  v->history[p] = (int16_t)(uint16_t)state;
  v->history[p + 1u] = (int16_t)(uint16_t)(state >> 16);
  wet = next;
  if (++v->position[i] == lengths[i]) v->position[i] = 0;
  offset += 2u * lengths[i];
 }
 return limit(wet, v);
}

static void advance_filter(struct bonsai_fx_voice *v)
{
 /* One Q8 coefficient step per sample bounds a full-range sweep to 4.73 ms.
  * The steady buffer path hoists this check once it reaches its target. */
 if (v->alpha < v->target_alpha) ++v->alpha;
 else if (v->alpha > v->target_alpha) --v->alpha;
}
static inline __attribute__((always_inline)) int32_t
delay_return(struct bonsai_fx_voice *v, struct dd_frame input)
{
 /* Cascaded box filters form a triangular 23-tap low-pass. Weighted
  * block sums are equivalent to a rolling input history, with only two
  * extra accumulators and no per-sample ring-buffer reads or writes. */
 int16_t mono = (int16_t)(((int32_t)input.l + input.r) / 2);
 v->input_sum += mono;
 v->weighted_sum += v->input_sum;
 if (++v->phase == BONSAI_FX_DECIMATION) {
  int16_t filtered = (int16_t)((v->weighted_sum + v->previous_sum) /
   (int)(BONSAI_FX_DECIMATION * BONSAI_FX_DECIMATION));
  v->previous_sum = v->input_sum * (int)BONSAI_FX_DECIMATION - v->weighted_sum;
  v->input_sum = v->weighted_sum = 0; v->phase = 0;
  v->wet_previous = v->wet_next;
  v->wet_next = v->type == BONSAI_FX_ECHO ? echo_tick(v, filtered) :
                                                     reverb_tick(v, filtered);
 }
 return v->wet_previous +
  ((int32_t)v->wet_next - v->wet_previous) * v->phase / (int)BONSAI_FX_DECIMATION;
}
static struct dd_frame blend_wet(struct dd_frame dry, int32_t wet, unsigned mix)
{
 /* Convex dry/wet gains sum to one. Wet <= int16 range by construction,
  * so hot stems cannot saturate before their gain/fader is applied. Q9
  * mixing retains every step of the 0..256 control without a variable divide. */
 if (mix == 256u)
  return (struct dd_frame){(int16_t)(((int32_t)dry.l + wet) / 2),
                          (int16_t)(((int32_t)dry.r + wet) / 2)};
 return (struct dd_frame){
  (int16_t)(dry.l + (wet - dry.l) * (int32_t)mix / 512),
  (int16_t)(dry.r + (wet - dry.r) * (int32_t)mix / 512)
 };
}

struct dd_frame bonsai_fx_process(struct bonsai_fx_voice *v,
                                  struct dd_frame input)
{
 if (v->type != v->requested_type && !v->mix) activate(v);
 if (v->type == BONSAI_FX_NONE) return input;
 if (v->type != BONSAI_FX_FILTER && v->clear_index < BONSAI_FX_HISTORY) {
  /* One bounded write per voice/sample, never memset a whole delay line in
   * the audio callback. Cleared history cannot leak from a previous effect. */
  v->history[v->clear_index++] = 0;
  return input;
 }
 unsigned target = v->type != v->requested_type ? 0 :
                   v->type == BONSAI_FX_FILTER ? 256 : v->requested_amount;
 if (v->mix < target) ++v->mix;
 else if (v->mix > target) --v->mix;
 if (v->type == BONSAI_FX_FILTER) {
  advance_filter(v);
  if (!v->filter_seeded) {
   v->low_l = (int32_t)input.l * 256;
   v->low_r = (int32_t)input.r * 256;
   v->filter_seeded = true;
  }
  v->low_l = lowpass(v->low_l, input.l, v->alpha);
  v->low_r = lowpass(v->low_r, input.r, v->alpha);
  int32_t l = input.l + (v->low_l / 256 - input.l) * v->mix / 256;
  int32_t r = input.r + (v->low_r / 256 - input.r) * v->mix / 256;
  return (struct dd_frame){(int16_t)l, (int16_t)r};
 }
 return blend_wet(input, delay_return(v, input), v->mix);
}

void bonsai_fx_process_buffer(struct bonsai_fx_voice *v,
                              struct dd_frame *frames, uint32_t count)
{
 uint32_t i=0;
 for(;i<count;i++) {
  if(v->type==v->requested_type) {
   if(v->type==BONSAI_FX_NONE) return;
   if(v->type==BONSAI_FX_FILTER) {
    if(v->mix==256&&v->filter_seeded) break;
   } else if(v->clear_index==BONSAI_FX_HISTORY&&v->mix==v->requested_amount) break;
  }
  frames[i]=bonsai_fx_process(v,frames[i]);
 }
 if(i==count) return;
 if(v->type==BONSAI_FX_FILTER) {
  int32_t l=v->low_l,r=v->low_r;
  for(;i<count&&v->alpha!=v->target_alpha;i++) {
   advance_filter(v);
   l=lowpass(l,frames[i].l,v->alpha);r=lowpass(r,frames[i].r,v->alpha);
   frames[i]=(struct dd_frame){l/256,r/256};
  }
  uint16_t alpha=v->alpha;
  if(alpha<=128u) {
   for(;i<count;i++) {
    l=lowpass_small_alpha(l,frames[i].l,alpha);
    r=lowpass_small_alpha(r,frames[i].r,alpha);
    frames[i]=(struct dd_frame){l/256,r/256};
   }
  } else {
   for(;i<count;i++) {
    l=lowpass(l,frames[i].l,alpha);r=lowpass(r,frames[i].r,alpha);
    frames[i]=(struct dd_frame){l/256,r/256};
   }
  }
  v->low_l=l;v->low_r=r;
  return;
 }
 /* Keep the input/interpolation state in registers through the buffer.
  * Delay history/position only change on a 4 kHz tick. The scalar path above
  * handles transitions; these local states are written back once per chunk. */
 unsigned phase=v->phase,mix=v->mix;
 int32_t sum=v->input_sum,weighted=v->weighted_sum,previous_sum=v->previous_sum;
 int16_t previous=v->wet_previous,next=v->wet_next;
 bool echo=v->type==BONSAI_FX_ECHO;
 for(;i<count;i++) {
  struct dd_frame in=frames[i];
  int16_t mono=(int16_t)(((int32_t)in.l+in.r)/2);
  sum+=mono;weighted+=sum;
  if(++phase==BONSAI_FX_DECIMATION) {
   int16_t filtered=(int16_t)((weighted+previous_sum)/
    (int)(BONSAI_FX_DECIMATION*BONSAI_FX_DECIMATION));
   previous_sum=sum*(int)BONSAI_FX_DECIMATION-weighted;
   sum=weighted=0;phase=0;
   previous=next;next=echo?echo_tick(v,filtered):reverb_tick(v,filtered);
  }
  int32_t wet=previous+((int32_t)next-previous)*(int)phase/(int)BONSAI_FX_DECIMATION;
  frames[i]=blend_wet(in,wet,mix);
 }
 v->phase=(uint8_t)phase;v->input_sum=sum;v->weighted_sum=weighted;
 v->previous_sum=previous_sum;v->wet_previous=previous;v->wet_next=next;
}
