/* SPDX-License-Identifier: MIT */
#ifndef SP1_DUAL_MIRROR_H
#define SP1_DUAL_MIRROR_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define DD_UI_CAPACITY 64u
/* Wire order: buttons, four physical faders (0..256), four track and four
 * status LED PWM duties (0..1000). Buttons: T1..4, PLAY, FN, FWD, REW, V+, V-. */
struct dd_ui_frame { uint32_t seq, ms; uint16_t value[13]; };
struct dd_ui_queue {
 struct dd_ui_frame frame[DD_UI_CAPACITY], latest;
 uint32_t head, count, seq, lost;
 bool valid;
};
/* Caller serializes push/pop/reset; no IO is allowed inside that section. */
bool dd_ui_push(struct dd_ui_queue *q, uint32_t ms, const uint16_t value[13], bool connected);
bool dd_ui_pop(struct dd_ui_queue *q, struct dd_ui_frame *out);
void dd_ui_start(struct dd_ui_queue *q);
int dd_ui_format(char *out, size_t size, const struct dd_ui_frame *f);
uint16_t dd_ui_tracks(int raw);
#endif
