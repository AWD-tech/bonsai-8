/* SPDX-License-Identifier: MIT
 * Pure runtime tap/beat controller. No audio, storage, allocation or IO.
 * One caller owns all state; the adapter publishes atomic telemetry copies.
 */
#ifndef BONSAI_SYNC_H
#define BONSAI_SYNC_H
#include <stdbool.h>
#include <stdint.h>
#define BONSAI_SYNC_NATIVE_RATE 24000u
#define BONSAI_SYNC_OUTPUT_RATE 48000u
#define BONSAI_SYNC_UNITY 65536u
#define BONSAI_SYNC_MAX_FRAMES 11522560u
#define BONSAI_SYNC_NUDGE_Q32 0x08000000u /* 1/32 quarter beat */
#define BONSAI_SYNC_LOCK_Q32 0x04000000u /* 1/64 quarter beat */
enum bonsai_sync_result {
 BONSAI_SYNC_OK, BONSAI_SYNC_PENDING, BONSAI_SYNC_UNKNOWN_GRID,
 BONSAI_SYNC_NOT_PLAYING, BONSAI_SYNC_RANGE, BONSAI_SYNC_STALE_SONG,
 BONSAI_SYNC_BAD_TAP, BONSAI_SYNC_INCOHERENT, BONSAI_SYNC_MANUAL,
 BONSAI_SYNC_INVALID
};
enum bonsai_sync_mode { BONSAI_SYNC_MODE_MANUAL, BONSAI_SYNC_ALIGNING,
 BONSAI_SYNC_LOCKED, BONSAI_SYNC_ERROR };
/* All fields describe one coherent audible transport snapshot. source_frame
 * is the nominal cumulative native-24k cursor modulo 2^32, NOT ring reclaim or
 * a WSOLA-selected grain position. loop_frame is its actual song coordinate.
 * output_frame is the shared audible-48k clock modulo 2^32. tempo_q16 is
 * native source consumption per nominal native output frame: 65536 = 1x.
 * Both decks passed to a step must share the SAME output_frame.
 */
struct bonsai_sync_position {
 uint32_t song_epoch,source_frame,loop_frame,output_frame,tempo_q16;
 bool playing;
};
struct bonsai_sync_deck {
 uint32_t song_epoch,loop_frames,bpm_milli,beat_native;
 uint32_t tap_source,tap_output,tap_intervals[3];
 struct bonsai_sync_position position;
 uint8_t tap_count;
 bool grid_valid,observed;
 enum bonsai_sync_mode mode;
};
struct bonsai_sync {
 struct bonsai_sync_deck deck[2];
 uint32_t min_tempo_q16,max_tempo_q16,phase_offset_q32;
 int8_t follower; /* -1 manual; one follower prevents an A/B feedback cycle */
};
struct bonsai_sync_request {
 uint32_t tempo_q16;
 int32_t phase_error_q32;
 enum bonsai_sync_result result;
 enum bonsai_sync_mode mode;
 uint8_t deck;
 bool apply;
};
bool bonsai_sync_init(struct bonsai_sync *s,uint32_t min_q16,uint32_t max_q16);
/* Every load/replacement/reset uses a new nonzero epoch, even same slot/length.
 * Zero frames means an unloaded/loading deck and clears its grid. No
 * persistent metadata is trusted or stored here. Other deck stays intact.
 */
bool bonsai_sync_song(struct bonsai_sync *s,unsigned deck,uint32_t epoch,uint32_t frames);
enum bonsai_sync_result bonsai_sync_observe(struct bonsai_sync *s,unsigned deck,
 const struct bonsai_sync_position *p);
/* Four consecutive quarter-beat taps while playing teach source BPM and the
 * first tap's native anchor. Tempo changes/pause/discontinuity reset tap run.
 */
enum bonsai_sync_result bonsai_sync_tap(struct bonsai_sync *s,unsigned deck);
bool bonsai_sync_set_grid(struct bonsai_sync *s,unsigned deck,uint32_t epoch,
 uint32_t bpm_milli,uint32_t first_beat_native);
enum bonsai_sync_result bonsai_sync_follow(struct bonsai_sync *s,unsigned selected);
/* Advance offset of an ACTIVE known-grid follower by +/- 1/32 beat. */
enum bonsai_sync_result bonsai_sync_nudge(struct bonsai_sync *s,unsigned selected,int direction);
void bonsai_sync_manual(struct bonsai_sync *s,unsigned deck);
/* Supplies tempo ONLY to a pitch-preserving DSP. No source/ring seek request.
 * Unsupported nominal/corrected ratios return apply=false, never varispeed or
 * a clamped-success ratio. A 5% maximum correction converges phase gradually.
 */
struct bonsai_sync_request bonsai_sync_step(struct bonsai_sync *s);
#endif
