/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_STRETCH_H
#define BONSAI_STRETCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define BS_STEMS 4u
#define BS_SEQUENCE 1440u
#define BS_OVERLAP 384u
#define BS_SEARCH 480u
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
 struct bs_plan pending;
 uint32_t first;
};
/* One worker owns scratch; the audio thread never performs a search or writes
 * tail history. Its current overlap must finish before the worker reuses tail. */
struct bs_work {
 struct bs_frame cache[BS_CACHE];
 float reference[BS_OVERLAP][2];
 struct bs_state *owner;
 struct bs_plan plan;
 uint32_t sequence,first,count,index,offset,limit,best,refine_end;
 float dot,energy,highest,reference_energy,reference_peak;
 uint8_t stage,stride;
};
typedef struct bs_frame (*bs_read_fn)(void *context,unsigned stem,uint32_t source);
void bs_init(struct bs_state *s,uint32_t first,bool enabled);
bool bs_request(struct bs_state *s,uint32_t ratio);
uint32_t bs_actual(const struct bs_state *s);
bool bs_cancel_stale(struct bs_work *work);
/* Worker only. Budget charges source channels copied and correlation channels
 * compared. All cache/reference/tail stages are incremental, including reset. */
uint32_t bs_service(struct bs_state *s,struct bs_work *work,bs_read_fn read,
                    void *context,const uint16_t gains[BS_STEMS],uint32_t oldest,
                    uint32_t written,uint32_t budget);
/* Renderer only. Returns false until a complete plan is release-published. */
bool bs_activate(struct bs_state *s,uint32_t *nominal,uint32_t *fraction);
bool bs_buffered(struct bs_state *s,uint32_t oldest,uint32_t written);
struct bs_frame bs_sample(struct bs_state *s,unsigned stem,unsigned which,
                          bs_read_fn read,void *context);
void bs_advance(struct bs_state *s);
uint32_t bs_retain(const struct bs_state *s);
#endif
