/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_FX_H
#define BONSAI_FX_H
#include "dual_engine.h"

#define BONSAI_FX_VOICES 8u
#define BONSAI_FX_HISTORY 1112u
#define BONSAI_FX_DECIMATION 12u
enum bonsai_fx_type {
 BONSAI_FX_NONE,
 BONSAI_FX_FILTER,
 BONSAI_FX_ECHO,
 BONSAI_FX_REVERB
};
struct bonsai_fx_voice {
 int16_t history[BONSAI_FX_HISTORY];
 int32_t low_l, low_r, input_sum, weighted_sum, previous_sum;
 int16_t wet_previous, wet_next;
 uint16_t position[6], clear_index, mix, requested_amount, alpha, target_alpha;
 uint8_t phase, type, requested_type;
 bool filter_seeded;
 uint32_t clipped;
};
struct bonsai_fx {
 struct bonsai_fx_voice voice[BONSAI_FX_VOICES];
 _Atomic uint32_t config[BONSAI_FX_VOICES];
};

/* init requires exclusive access. set is nonblocking/control-thread safe;
 * amount is 0..256, clamped. Each voice has ONE selected effect. Configurations
 * persist until set again; there is no button-release action in this core. */
void bonsai_fx_init(struct bonsai_fx *fx);
/* Song-load hook: requires audio exclusion, preserves the atomic selected
 * effect/amount and cumulative clip counter, and removes old song state.
 * History is made inaccessible immediately and cleared incrementally on the
 * next activation; this does not clear 2 KiB inside a scheduler lock. */
bool bonsai_fx_reset_voice(struct bonsai_fx *fx, unsigned voice);
bool bonsai_fx_set(struct bonsai_fx *fx, unsigned voice,
                   enum bonsai_fx_type type, unsigned amount);
/* Audio owner snapshots settings once before rendering an output block. */
void bonsai_fx_begin_block(struct bonsai_fx *fx);
/* Audio owner, once per 48 kHz frame per voice, before stem gain/mute. Call
 * even for currently muted voices if their tails should keep progressing.
 * Bypass preserves stereo exactly after a <=256-frame (5.33 ms) fade-out.
 * Effect changes fade out, clear history incrementally over 1112 frames
 * (23.17 ms), then fade in. No full delay-line clear occurs in this call.
 *
 * FILTER: independent stereo one-pole low-pass, amount maps approximately
 * 18 kHz to 150 Hz. ECHO/REVERB: stereo dry plus mono wet, 4 kHz wet sampling
 * (2 kHz Nyquist), triangular anti-alias decimation and linear return
 * interpolation. Full amount is a 50/50 dry/wet mix, with unity total gain
 * and no per-stem hard clipping before the fader. Filter coefficient changes
 * ramp over at most 227 frames (4.73 ms) to avoid abrupt sweeps.
 * ECHO: fixed 250 ms, feedback 0.5 and input gain 0.5; ~2.5 s to -60 dB.
 * REVERB: compact/dark four-comb network (37.25..73.25 ms), two allpasses;
 * feedback 0.75, longest nominal -60 dB decay ~1.76 s. Not full-band reverb.
 * Amount controls wet level for echo/reverb; at zero dry is unchanged,
 * at full amount dry/wet are each half level. Stereo dry is retained.
 */
struct dd_frame bonsai_fx_process(struct bonsai_fx_voice *v,
                                  struct dd_frame input);
/* Same samples and state as repeated process calls, in place. Settings stay
 * fixed until the next begin_block. Steady effects avoid transition checks
 * and filter crossfades in the inner loop; transitions use the scalar path. */
void bonsai_fx_process_buffer(struct bonsai_fx_voice *v,
                              struct dd_frame *frames, uint32_t count);
#endif
