/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_STRETCH_H
#define BONSAI_STRETCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define BS_STEMS 4u
#define BS_SEQUENCE 1440u
#define BS_OVERLAP 384u
#define BS_SEARCH 600u
#define BS_HOP (BS_SEQUENCE-BS_OVERLAP)
#define BS_CACHE (BS_OVERLAP+2u*BS_SEARCH)
struct bs_frame { int16_t l,r; };
struct bs_plan {
 uint32_t chosen,nominal,fraction,ratio,next_nominal,next_fraction,generation;
 bool tail;
};
struct bs_state {
 struct bs_frame tail[BS_STEMS][BS_OVERLAP];
 struct bs_frame cached[BS_STEMS][2];
 uint32_t cached_cursor,cached_sequence;
 bool cached_valid;
 _Atomic uint32_t generation,requested,actual,sequence,cursor;
 _Atomic uint32_t chosen,nominal,fraction,next_nominal,next_fraction;
 _Atomic bool enabled,active,ready;
 bool active_tail;
 /* Storage-owned completed search; renderer cannot inspect pending until
  * ready. Parking releases shared scratch while the old overlap finishes. */
 bool prepared,prepared_transient;
 uint32_t prepared_sequence;
 struct bs_plan pending;
 uint32_t first;
};
/* One worker owns scratch; the audio thread never performs a search or writes
 * tail history. Search can run during the overlap; tail reuse waits for its
 * end. Completed searches park per deck without retaining this shared worker. */
struct bs_work {
 struct bs_frame cache[BS_CACHE];
 struct bs_frame reference[BS_OVERLAP];
 struct bs_state *owner;
 /* The packed reference folds channel balance into a shared-scale int16
  * coefficient. Exact integer moments track the quadratic energy window. */
 int64_t corr_dot,moment_a[2],moment_b[2],moment_e[2];
 float pack_scale;
 uint32_t reference_abs[2];
 struct bs_plan plan;
 uint32_t sequence,first,count,index,offset,limit,best,refine_end;
 float highest,reference_energy,reference_peak;
 float balance[2],reference_power[2];
 int8_t sign[2];
 uint8_t crossings[2];
 uint8_t stage,stride;
 uint32_t scan_count,onset,onset_last,onset_count,nonquiet_count;
 float scan_energy,scan_peak;
 bool transient;
};
typedef struct bs_frame (*bs_read_fn)(void *context,unsigned stem,uint32_t source);
/* Optional engine-owned bounded mixing. The caller has already validated the
 * published source span. Destination is worker-private; copy exactly count
 * frames using this call's immutable gain snapshot and the same /1024 rounding.
 * It may only read source state and write out; resets remain exclusive. */
typedef void (*bs_mix_batch_fn)(void *context,uint32_t source,uint32_t count,
                              const uint16_t gains[BS_STEMS],struct bs_frame *out);
/* Reset requires exclusive ownership against both service and renderer. */
void bs_init(struct bs_state *s,uint32_t first,bool enabled);
bool bs_request(struct bs_state *s,uint32_t ratio);
uint32_t bs_actual(const struct bs_state *s);
bool bs_cancel_stale(struct bs_work *work);
/* Worker only. Budget charges source channels copied, reference/energy
 * preparation and correlation channels compared. Every sample stage is
 * incremental, including reset. */
uint32_t bs_service(struct bs_state *s,struct bs_work *work,bs_read_fn read,
                    void *context,const uint16_t gains[BS_STEMS],uint32_t oldest,
                    uint32_t written,uint32_t budget);
uint32_t bs_service_with_batch(struct bs_state *s,struct bs_work *work,bs_read_fn read,
                    void *context,const uint16_t gains[BS_STEMS],uint32_t oldest,
                    uint32_t written,uint32_t budget,bs_mix_batch_fn batch);
/* Renderer only. Returns false until a complete plan is release-published. */
bool bs_activate(struct bs_state *s,uint32_t *nominal,uint32_t *fraction);
bool bs_buffered(struct bs_state *s,uint32_t oldest,uint32_t written);
/* Renderer-owned cached pairs: prepare once, then index every audible stem.
 * Valid until the next call after cursor advancement or descriptor activation. */
const struct bs_frame (*bs_samples(struct bs_state *s,bs_read_fn read,void *context))[2];
struct bs_frame bs_sample(struct bs_state *s,unsigned stem,unsigned which,
                          bs_read_fn read,void *context);
void bs_advance(struct bs_state *s);
uint32_t bs_retain(const struct bs_state *s);
#endif
