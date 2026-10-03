/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_FX_H
#define BONSAI_FX_H
#include "dual_engine.h"

#define BONSAI_FX_VOICES 8u
enum bonsai_fx_type {
 BONSAI_FX_NONE = 0,
 BONSAI_FX_FILTER = 1
};
struct bonsai_fx_voice {
 int32_t low_l, low_r;
 uint16_t mix, requested_amount, alpha, target_alpha;
 uint8_t type, requested_type;
 bool filter_seeded;
 uint32_t clipped;
};
struct bonsai_fx {
 struct bonsai_fx_voice voice[BONSAI_FX_VOICES];
 _Atomic uint32_t config[BONSAI_FX_VOICES];
};

/* init requires exclusive access. set is nonblocking/control-thread safe;
 * amount is 0..256, clamped. Filter is the only supported effect. Retired
 * numeric IDs 2/3 are rejected, even at zero amount, without changing config.
 * Configurations persist until set again; button release has no core action. */
void bonsai_fx_init(struct bonsai_fx *fx);
/* Song-load hook: requires audio exclusion, preserves the atomic selected
 * filter/amount and cumulative clip counter, and removes old song state. */
bool bonsai_fx_reset_voice(struct bonsai_fx *fx, unsigned voice);
bool bonsai_fx_set(struct bonsai_fx *fx, unsigned voice,
                   enum bonsai_fx_type type, unsigned amount);
/* Audio owner snapshots settings once before rendering an output block.
 * Invalid directly-written config IDs/amounts fail closed to a bypass fade. */
void bonsai_fx_begin_block(struct bonsai_fx *fx);
/* Audio owner, once per 48 kHz frame per voice, before stem gain/mute. Call
 * even for currently muted voices to keep their filter state progressing.
 * Bypass preserves stereo exactly after a <=256-frame (5.33 ms) fade-out.
 *
 * FILTER: independent stereo one-pole high-pass. Increasing amount sweeps
 * cutoff from approximately 60 Hz to 8 kHz, removing bass and retaining highs.
 * Its normalized passband has unity gain at Nyquist, with no resonance boost.
 * Filter coefficient changes ramp over at most 185 frames (3.86 ms).
 * Output uses int32 headroom through stem gain and the two-deck sum; full-scale
 * polarity transients can exceed int16 but do not clip or wrap before the
 * final engine bus clamp. Source input must remain int16 stereo.
 */
struct dd_wide_frame bonsai_fx_process(struct bonsai_fx_voice *v,
                                  struct dd_frame input);
/* Same samples and state as repeated process calls, in place. Settings stay
 * fixed until the next begin_block. Input frame values must be in int16 range;
 * the in-place output is wide. Steady filters avoid transition checks and
 * crossfades in the inner loop; transitions use the scalar path. */
void bonsai_fx_process_buffer(struct bonsai_fx_voice *v,
                              struct dd_wide_frame *frames, uint32_t count);
#endif
