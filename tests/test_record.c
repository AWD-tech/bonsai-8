#include "bonsai_record.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static struct bonsai_record record;
static int16_t samples[BONSAI_RECORD_RING * 4u];
static uint8_t sector[512], again[512];
static struct dd_frame decoded[DD_MAX_DECODE];

static unsigned state(void) { return atomic_load(&record.state); }
static void start(void)
{
 bonsai_record_init(&record);
 assert(bonsai_record_start(&record));
 assert(state() == BONSAI_RECORD_ACTIVE);
}
static void stop(void)
{
 bonsai_record_request_stop(&record);
 assert(!bonsai_record_push48(&record, NULL, 0));
 assert(state() == BONSAI_RECORD_DRAINING);
}
static int16_t value(uint32_t i) { return (int16_t)(((int)(i % 16000u) - 8000) * 4); }
static void make_native(int16_t *out, uint32_t first, uint32_t n)
{
 for (uint32_t i = 0; i < n; ++i) {
  int16_t x = value(first + i);
  out[i * 4u] = out[i * 4u + 2u] = x;
  out[i * 4u + 1u] = out[i * 4u + 3u] = -x;
 }
}
static void check_sector(const uint8_t *bytes, uint32_t first, uint32_t n)
{
 assert(dd_decode(bytes, DD_P14S, decoded) == 140);
 for (uint32_t i = 0; i < n; ++i) {
  assert(decoded[i].l == value(first + i));
  assert(decoded[i].r == -value(first + i));
 }
 for (uint32_t i = n; i < 140; ++i)
  assert(decoded[i].l == 0 && decoded[i].r == 0);
}

static void test_golden_and_partial_stop(void)
{
 /* Exact first 14 payload bytes produced by tools/sp1.py pack_block for
  * struct.pack('<8h', -32768,32767,-1,1,-5,6,-32765,32764). */
 static const uint8_t payload[] = {
  0x80,0x01,0xff,0xff,0xff,0xc0,0x00,0xff,0xf8,0x00,0x18,0x00,0x1f,0xff
 };
 static const int16_t native[] = {-32768,32767,-1,1,-5,6,-32765,32764};
 uint8_t expected[512] = {0};
 memcpy(expected, "P14S", 4); expected[5] = 1; expected[11] = 0x5b;
 expected[12] = 0x18; expected[13] = 1;
 memcpy(expected + 16, payload, sizeof(payload));
 start();
 for (unsigned i = 0; i < 4; ++i) {
  samples[i*4] = samples[i*4+2] = native[i*2];
  samples[i*4+1] = samples[i*4+3] = native[i*2+1];
 }
 assert(bonsai_record_push48(&record, samples, 8));
 assert(!bonsai_record_start(&record));
 assert(bonsai_record_peek_sector(&record, sector) == 0);
 stop(); assert(!bonsai_record_start(&record));
 assert(bonsai_record_peek_sector(&record, sector) == 4);
 assert(!memcmp(sector, expected, sizeof(sector)));
 assert(bonsai_record_peek_sector(&record, again) == 4);
 assert(!memcmp(sector, again, sizeof(sector)));
 assert(atomic_load(&record.read) == 0);
 assert(atomic_load(&record.committed_frames) == 0);
 assert(bonsai_record_commit_sector(&record));
 assert(state() == BONSAI_RECORD_COMPLETE);
 assert(atomic_load(&record.source_frames) == 8);
 assert(atomic_load(&record.written) == 4);
 assert(atomic_load(&record.committed_frames) == 4);
 assert(atomic_load(&record.committed_sectors) == 1);
 assert(!bonsai_record_commit_sector(&record));
 assert(!bonsai_record_push48(&record, samples, 8));
 assert(bonsai_record_start(&record));
 assert(atomic_load(&record.written) == 0);
}

static void test_odd_chunks_and_empty_stop(void)
{
 int16_t input[] = {-32768,32767,32767,-32768, 5,-5,4,-4, 32767,-32768};
 start();
 assert(bonsai_record_push48(&record, input, 1));
 assert(atomic_load(&record.written) == 0);
 assert(bonsai_record_push48(&record, input + 2, 2));
 assert(atomic_load(&record.written) == 1);
 assert(bonsai_record_push48(&record, input + 6, 2));
 assert(atomic_load(&record.written) == 2);
 stop();
 assert(atomic_load(&record.source_frames) == 5);
 assert(bonsai_record_peek_sector(&record, sector) == 3);
 assert(dd_decode(sector, DD_P14S, decoded) == 140);
 assert(decoded[0].l == 0 && decoded[0].r == 0);
 assert(decoded[1].l == 4 && decoded[1].r == -4);
 assert(decoded[2].l == 32764 && decoded[2].r == -32768);
 for (unsigned i=3;i<140;++i) assert(!decoded[i].l && !decoded[i].r);
 assert(bonsai_record_commit_sector(&record));
 assert(state() == BONSAI_RECORD_COMPLETE);
 start(); stop();
 assert(bonsai_record_peek_sector(&record, sector) == 0);
 assert(state() == BONSAI_RECORD_COMPLETE);
 assert(atomic_load(&record.committed_frames) == 0);
 assert(atomic_load(&record.committed_sectors) == 0);
}

static void test_wrap_and_commit_ownership(void)
{
 start();
 uint32_t origin = UINT32_MAX - 42u;
 atomic_store(&record.written, origin); atomic_store(&record.read, origin);
 make_native(samples, 0, BONSAI_RECORD_RING);
 assert(bonsai_record_push48(&record, samples, BONSAI_RECORD_RING * 2u));
 uint32_t consumed = 0;
 for (unsigned i = 0; i < 32; ++i) {
  assert(bonsai_record_peek_sector(&record, sector) == 140);
  check_sector(sector, consumed, 140);
  assert(atomic_load(&record.read) == origin + consumed);
  assert(bonsai_record_commit_sector(&record)); consumed += 140;
 }
 /* The released space can wrap and refill while the existing tail remains. */
 make_native(samples, BONSAI_RECORD_RING, consumed);
 assert(bonsai_record_push48(&record, samples, consumed * 2u));
 stop();
 uint32_t total = BONSAI_RECORD_RING + consumed, n;
 while ((n = bonsai_record_peek_sector(&record, sector))) {
  check_sector(sector, consumed, n);
  assert(bonsai_record_commit_sector(&record)); consumed += n;
 }
 assert(consumed == total && state() == BONSAI_RECORD_COMPLETE);
 assert(atomic_load(&record.committed_frames) == total);
 assert(atomic_load(&record.written) == origin + total);
}

static void test_overflow_and_storage_failure(void)
{
 start(); make_native(samples, 0, BONSAI_RECORD_RING);
 assert(bonsai_record_push48(&record, samples, BONSAI_RECORD_RING * 2u));
 assert(bonsai_record_peek_sector(&record, sector) == 140);
 assert(!bonsai_record_push48(&record, samples, 2));
 assert(state() == BONSAI_RECORD_FAILED);
 assert(atomic_load(&record.error) == BONSAI_RECORD_OVERFLOW);
 assert(!bonsai_record_commit_sector(&record));
 assert(!bonsai_record_peek_sector(&record, sector));
 bonsai_record_request_stop(&record);
 assert(!bonsai_record_push48(&record, NULL, 0));
 assert(state() == BONSAI_RECORD_FAILED);
 assert(bonsai_record_start(&record));
 assert(bonsai_record_push48(&record, samples, BONSAI_RECORD_RING * 2u));
 /* A final unpaired input frame also requires space before stop completes. */
 assert(bonsai_record_push48(&record, samples, 1));
 bonsai_record_request_stop(&record);
 assert(!bonsai_record_push48(&record, NULL, 0));
 assert(state() == BONSAI_RECORD_FAILED);
 assert(atomic_load(&record.error) == BONSAI_RECORD_OVERFLOW);
 start(); assert(bonsai_record_push48(&record, samples, 280));
 assert(bonsai_record_peek_sector(&record, sector) == 140);
 bonsai_record_fail(&record, BONSAI_RECORD_STORAGE_ERROR);
 assert(!bonsai_record_commit_sector(&record));
 assert(state() == BONSAI_RECORD_FAILED);
 assert(atomic_load(&record.error) == BONSAI_RECORD_STORAGE_ERROR);
 start();
 assert(!bonsai_record_push48(&record, NULL, UINT32_MAX));
 assert(state() == BONSAI_RECORD_FAILED);
}

static void test_all_pcm16_quantization(void)
{
 for (int first = INT16_MIN; first <= INT16_MAX; first += 140) {
  unsigned n = INT16_MAX - first + 1 < 140 ? (unsigned)(INT16_MAX - first + 1) : 140;
  start();
  for (unsigned i = 0; i < n; ++i) {
   int x = first + (int)i;
   samples[i*4] = samples[i*4+1] = samples[i*4+2] = samples[i*4+3] = (int16_t)x;
  }
  assert(bonsai_record_push48(&record, samples, n * 2u)); stop();
  assert(bonsai_record_peek_sector(&record, sector) == n);
  assert(dd_decode(sector, DD_P14S, decoded) == 140);
  for (unsigned i = 0; i < n; ++i) {
   /* Clearing the two least significant two's-complement bits gives the
    * Python arithmetic-right-shift / decoder's multiply-by-four result. */
   int x = first + (int)i;
   int expected = x - ((x % 4 + 4) % 4);
   assert(decoded[i].l == expected && decoded[i].r == expected);
  }
  assert(bonsai_record_commit_sector(&record));
 }
}

#define CONCURRENT_FRAMES 200003u
static void *produce(void *unused)
{
 (void)unused;
 int16_t chunk[64*4];
 for (uint32_t i = 0; i < CONCURRENT_FRAMES;) {
  unsigned n = CONCURRENT_FRAMES - i < 64 ? CONCURRENT_FRAMES - i : 64;
  /* Simulate an adequately fast storage consumer; the actual audio API
   * never waits. Only this test's producer pacing waits for free capacity. */
  while (atomic_load(&record.written) - atomic_load(&record.read) >
         BONSAI_RECORD_RING - n) sched_yield();
  make_native(chunk, i, n);
  assert(bonsai_record_push48(&record, chunk, n * 2u)); i += n;
 }
 bonsai_record_request_stop(&record);
 assert(!bonsai_record_push48(&record, NULL, 0));
 return NULL;
}
static void test_concurrent_spsc(void)
{
 start(); pthread_t producer;
 assert(pthread_create(&producer, NULL, produce, NULL) == 0);
 uint32_t consumed = 0;
 while (state() != BONSAI_RECORD_COMPLETE) {
  assert(state() != BONSAI_RECORD_FAILED);
  uint32_t n = bonsai_record_peek_sector(&record, sector);
  if (!n) { sched_yield(); continue; }
  check_sector(sector, consumed, n);
  assert(bonsai_record_peek_sector(&record, again) == n);
  assert(!memcmp(sector, again, sizeof(sector)));
  assert(bonsai_record_commit_sector(&record)); consumed += n;
 }
 assert(pthread_join(producer, NULL) == 0);
 assert(consumed == CONCURRENT_FRAMES);
 assert(atomic_load(&record.source_frames) == CONCURRENT_FRAMES * 2u);
 assert(atomic_load(&record.committed_frames) == CONCURRENT_FRAMES);
 assert(atomic_load(&record.committed_sectors) == (CONCURRENT_FRAMES + 139u)/140u);
}

int main(void)
{
 test_golden_and_partial_stop(); test_odd_chunks_and_empty_stop();
 test_wrap_and_commit_ownership(); test_overflow_and_storage_failure();
 test_all_pcm16_quantization(); test_concurrent_spsc();
 printf("PASS: recording P14S compatibility, downsampling, stop/drain, wrap, "
        "fail-closed overflow, commit ownership and SPSC concurrency (%zu bytes)\n",
        sizeof(record));
}
