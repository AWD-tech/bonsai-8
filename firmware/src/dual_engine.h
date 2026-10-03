/* SPDX-License-Identifier: MIT */
#ifndef SP1_DUAL_ENGINE_H
#define SP1_DUAL_ENGINE_H
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#define DD_DECKS 2u
#define DD_STEMS 4u
#define DD_RING 4096u
#define DD_P14S 5u
#define DD_P16M 6u
#define DD_MAX_DECODE 248u
#define DD_EFFECT_CHUNK 64u
struct dd_frame { int16_t l, r; };
/* Processing headroom only; source rings and stored audio remain int16 stereo. */
struct dd_wide_frame { int32_t l, r; };
struct bonsai_fx;
struct dd_voice {
 struct dd_frame ring[DD_RING];
 _Atomic uint32_t written, gain;
 uint16_t current_gain;
 uint8_t present;
};
struct dd_deck {
 struct dd_voice voice[DD_STEMS];
 _Atomic uint32_t read, speed, playing, mute_mask;
 uint32_t fraction, underruns;
 uint16_t envelope;
 bool starved;
 struct dd_wide_frame last;
};
struct dd_engine {
 struct dd_deck deck[DD_DECKS];
 _Atomic uint32_t master;
 uint32_t clips;
 /* Optional externally owned effects bank. NULL preserves the dry renderer.
  * Attach/detach only with audio excluded; controls use bonsai_fx_set(). */
 struct bonsai_fx *fx;
 /* Audio-owner scratch: bounded chunks avoid growing the 1536-byte thread
  * stack or allocating in the callback. Shared by both decks serially. */
 struct dd_wide_frame effect_frames[DD_EFFECT_CHUNK];
 int32_t effect_sum[DD_DECKS][DD_EFFECT_CHUNK*2];
};
struct dd_pickup { int previous; bool waiting; };
void dd_init(struct dd_engine *e);
/* Reset needs exclusive access; the adapter briefly locks scheduling. */
void dd_reset_deck(struct dd_deck *d, uint8_t mask);
uint32_t dd_room(const struct dd_deck *d, unsigned stem);
bool dd_push(struct dd_deck *d, unsigned stem, const struct dd_frame *src, uint32_t n);
void dd_render(struct dd_engine *e, int16_t *stereo, uint32_t frames);
bool dd_pickup_move(struct dd_pickup *p, uint16_t target, uint16_t physical);
/* Native 24k frame count, or zero for malformed/unsupported blocks. */
uint32_t dd_decode(const uint8_t block[512], uint8_t codec, struct dd_frame out[DD_MAX_DECODE]);
#endif
