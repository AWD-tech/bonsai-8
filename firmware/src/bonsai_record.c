/* SPDX-License-Identifier: MIT */
#include "bonsai_record.h"
#include <string.h>

_Static_assert((BONSAI_RECORD_RING & (BONSAI_RECORD_RING - 1u)) == 0,
               "Recording ring must have a power-of-two size");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_BOOL_LOCK_FREE == 2,
               "Audio producer atomics must never use blocking locks");
_Static_assert(sizeof(struct bonsai_record) <= 33024,
               "Recording core must fit its 32 KiB ring and small state budget");

void bonsai_record_init(struct bonsai_record *r)
{
 atomic_init(&r->written, 0);
 atomic_init(&r->read, 0);
 atomic_init(&r->source_frames, 0);
 atomic_init(&r->committed_frames, 0);
 atomic_init(&r->committed_sectors, 0);
 atomic_init(&r->state, BONSAI_RECORD_IDLE);
 atomic_init(&r->error, BONSAI_RECORD_OK);
 atomic_init(&r->stop_requested, false);
 r->half = (struct dd_frame){0, 0};
 r->have_half = false;
 r->pending_frames = 0;
}

bool bonsai_record_start(struct bonsai_record *r)
{
 unsigned state = atomic_load_explicit(&r->state, memory_order_relaxed);
 if (state == BONSAI_RECORD_ACTIVE || state == BONSAI_RECORD_DRAINING)
  return false;
 bonsai_record_init(r);
 atomic_store_explicit(&r->state, BONSAI_RECORD_ACTIVE, memory_order_release);
 return true;
}

void bonsai_record_request_stop(struct bonsai_record *r)
{
 atomic_store_explicit(&r->stop_requested, true, memory_order_release);
}

void bonsai_record_fail(struct bonsai_record *r,
                        enum bonsai_record_error error)
{
 unsigned expected = BONSAI_RECORD_OK;
 if (error == BONSAI_RECORD_OK) error = BONSAI_RECORD_STORAGE_ERROR;
 atomic_compare_exchange_strong_explicit(&r->error, &expected, error,
                                         memory_order_relaxed,
                                         memory_order_relaxed);
 atomic_store_explicit(&r->state, BONSAI_RECORD_FAILED, memory_order_release);
}

static void finish_producer(struct bonsai_record *r)
{
 if (r->have_half) {
  uint32_t w = atomic_load_explicit(&r->written, memory_order_relaxed);
  uint32_t rd = atomic_load_explicit(&r->read, memory_order_acquire);
  if (w - rd == BONSAI_RECORD_RING) {
   bonsai_record_fail(r, BONSAI_RECORD_OVERFLOW);
   return;
  }
  r->ring[w & (BONSAI_RECORD_RING - 1u)] = r->half;
  r->have_half = false;
  atomic_store_explicit(&r->written, w + 1u, memory_order_release);
 }
 unsigned expected = BONSAI_RECORD_ACTIVE;
 atomic_compare_exchange_strong_explicit(&r->state, &expected,
                                         BONSAI_RECORD_DRAINING,
                                         memory_order_release,
                                         memory_order_relaxed);
}

bool bonsai_record_push48(struct bonsai_record *r, const int16_t *stereo,
                          uint32_t frames)
{
 if (atomic_load_explicit(&r->state, memory_order_acquire) !=
     BONSAI_RECORD_ACTIVE) return false;
 if (atomic_load_explicit(&r->stop_requested, memory_order_acquire)) {
  finish_producer(r);
  return false;
 }
 uint32_t w = atomic_load_explicit(&r->written, memory_order_relaxed);
 uint32_t rd = atomic_load_explicit(&r->read, memory_order_acquire);
 /* Reject oversized input before frames+1 or stereo indexing can overflow. */
 uint32_t produced = frames / 2u + (r->have_half && (frames & 1u));
 if (produced > BONSAI_RECORD_RING ||
     w - rd > BONSAI_RECORD_RING - produced) {
  bonsai_record_fail(r, BONSAI_RECORD_OVERFLOW);
  return false;
 }
 for (uint32_t i = 0; i < frames; ++i) {
  struct dd_frame next = {stereo[i * 2u], stereo[i * 2u + 1u]};
  if (!r->have_half) {
   r->half = next;
   r->have_half = true;
  } else {
   struct dd_frame average = {
    (int16_t)(((int32_t)r->half.l + next.l) / 2),
    (int16_t)(((int32_t)r->half.r + next.r) / 2)
   };
   r->ring[w++ & (BONSAI_RECORD_RING - 1u)] = average;
   r->have_half = false;
  }
 }
 atomic_fetch_add_explicit(&r->source_frames, frames, memory_order_relaxed);
 atomic_store_explicit(&r->written, w, memory_order_release);
 if (atomic_load_explicit(&r->stop_requested, memory_order_acquire))
  finish_producer(r);
 return atomic_load_explicit(&r->state, memory_order_acquire) ==
        BONSAI_RECORD_ACTIVE;
}

/* Python pack_block uses signed sample >> 2: floor division by four, even
 * for negative samples. Express it portably without signed right shifts. */
static uint16_t quantize(int16_t sample)
{
 int32_t s = sample;
 int32_t q = s >= 0 ? s / 4 : -((-s + 3) / 4);
 return (uint16_t)q & 0x3fffu;
}

static void complete_if_empty(struct bonsai_record *r)
{
 /* Acquire producer completion before checking indices: it must not be able
  * to append its final half-frame after this empty check. */
 if (atomic_load_explicit(&r->state, memory_order_acquire) !=
     BONSAI_RECORD_DRAINING) return;
 if (atomic_load_explicit(&r->read, memory_order_relaxed) !=
     atomic_load_explicit(&r->written, memory_order_acquire)) return;
 unsigned expected = BONSAI_RECORD_DRAINING;
 atomic_compare_exchange_strong_explicit(&r->state, &expected,
                                         BONSAI_RECORD_COMPLETE,
                                         memory_order_release,
                                         memory_order_relaxed);
}

uint32_t bonsai_record_peek_sector(struct bonsai_record *r, uint8_t out[512])
{
 unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
 if (state != BONSAI_RECORD_ACTIVE && state != BONSAI_RECORD_DRAINING)
  return 0;
 uint32_t rd = atomic_load_explicit(&r->read, memory_order_relaxed);
 uint32_t available = atomic_load_explicit(&r->written, memory_order_acquire) - rd;
 if (!r->pending_frames) {
  if (!available) { complete_if_empty(r); return 0; }
  if (available < BONSAI_RECORD_SECTOR_FRAMES &&
      state != BONSAI_RECORD_DRAINING) return 0;
  r->pending_frames = available < BONSAI_RECORD_SECTOR_FRAMES ?
                      available : BONSAI_RECORD_SECTOR_FRAMES;
 }
 memset(out, 0, 512);
 memcpy(out, "P14S", 4);
 out[5] = 1;
 out[11] = 0x5b;
 out[12] = 0x18; out[13] = 0x01; /* 280 interleaved samples. */
 for (uint32_t i = 0; i < BONSAI_RECORD_SECTOR_FRAMES; i += 2u) {
  struct dd_frame first = i < r->pending_frames ?
   r->ring[(rd + i) & (BONSAI_RECORD_RING - 1u)] : (struct dd_frame){0, 0};
  struct dd_frame second = i + 1u < r->pending_frames ?
   r->ring[(rd + i + 1u) & (BONSAI_RECORD_RING - 1u)] : (struct dd_frame){0, 0};
  uint16_t a = quantize(first.l), b = quantize(first.r);
  uint16_t c = quantize(second.l), d = quantize(second.r);
  uint8_t *p = out + 16u + i / 2u * 7u;
  p[0] = a >> 6;
  p[1] = (uint8_t)((a << 2) | (b >> 12));
  p[2] = b >> 4;
  p[3] = (uint8_t)((b << 4) | (c >> 10));
  p[4] = c >> 2;
  p[5] = (uint8_t)((c << 6) | (d >> 8));
  p[6] = (uint8_t)d;
 }
 return r->pending_frames;
}

bool bonsai_record_commit_sector(struct bonsai_record *r)
{
 unsigned state = atomic_load_explicit(&r->state, memory_order_acquire);
 if (!r->pending_frames || (state != BONSAI_RECORD_ACTIVE &&
                           state != BONSAI_RECORD_DRAINING)) return false;
 uint32_t rd = atomic_load_explicit(&r->read, memory_order_relaxed);
 atomic_fetch_add_explicit(&r->committed_frames, r->pending_frames,
                           memory_order_relaxed);
 atomic_fetch_add_explicit(&r->committed_sectors, 1, memory_order_relaxed);
 atomic_store_explicit(&r->read, rd + r->pending_frames, memory_order_release);
 r->pending_frames = 0;
 complete_if_empty(r);
 return true;
}
