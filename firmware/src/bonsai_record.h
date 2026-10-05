/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_RECORD_H
#define BONSAI_RECORD_H
#include "dual_engine.h"

#define BONSAI_RECORD_RING 8192u
#define BONSAI_RECORD_SECTOR_FRAMES 140u

enum bonsai_record_state {
 BONSAI_RECORD_IDLE,
 BONSAI_RECORD_ACTIVE,
 BONSAI_RECORD_DRAINING,
 BONSAI_RECORD_COMPLETE,
 BONSAI_RECORD_FAILED
};
enum bonsai_record_error {
 BONSAI_RECORD_OK,
 BONSAI_RECORD_OVERFLOW,
 BONSAI_RECORD_STORAGE_ERROR
};
struct bonsai_record {
 struct dd_frame ring[BONSAI_RECORD_RING];
 _Atomic uint32_t written, read;
 _Atomic uint32_t source_frames, committed_frames, committed_sectors;
 _Atomic unsigned state, error;
 _Atomic bool stop_requested;
 /* Audio producer only. */
 struct dd_frame half;
 bool have_half;
 /* Storage consumer only. Pending frames remain in the ring until committed. */
 uint32_t pending_frames;
};

/* Initialization and start require exclusive access to producer and consumer.
 * start refuses to discard an active/draining take; it resets completed/failed
 * takes. No allocation, USB, storage IO or blocking occurs in this module. */
void bonsai_record_init(struct bonsai_record *r);
bool bonsai_record_start(struct bonsai_record *r);

/* Nonblocking control request. The audio producer MUST continue calling push48
 * (zero frames / NULL is sufficient) until it acknowledges stop as DRAINING.
 * A final unpaired 48 kHz frame is retained as one native 24 kHz frame. */
void bonsai_record_request_stop(struct bonsai_record *r);
/* Single audio producer: the final stereo 48 kHz mix. Adjacent pairs average
 * toward zero into 24 kHz frames, including pairs split between calls.
 * false means inactive/stopped/failed. Overflow fails the entire take; never
 * publish metadata for a failed take, even if earlier sectors were written. */
bool bonsai_record_push48(struct bonsai_record *r, const int16_t *stereo,
                          uint32_t frames);

/* Single storage consumer. peek does not release ring space. Repeated peeks
 * yield the same sector until commit, so a write can be retried safely.
 * Returns 0 when no full sector is ready, or on failure. After stop, the last
 * partial sector is zero-padded and its exact native frame count is returned.
 * Call commit ONLY after the sector was successfully written to storage.
 * A COMPLETE take still needs adapter-side flush/verification before publish. */
uint32_t bonsai_record_peek_sector(struct bonsai_record *r, uint8_t out[512]);
bool bonsai_record_commit_sector(struct bonsai_record *r);
/* May be called by storage consumer on write/flush/verification failure.
 * FAILED is terminal until an exclusive start; it cannot become COMPLETE. */
void bonsai_record_fail(struct bonsai_record *r,
                        enum bonsai_record_error error);

#endif
