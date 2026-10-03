/* SPDX-License-Identifier: MIT */
#ifndef BONSAI_CDC_H
#define BONSAI_CDC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define BONSAI_CDC_CAPACITY 4096u
#define BONSAI_CDC_REPLY_MAX 2048u
#define BONSAI_CDC_RX_BUDGET 64u
#define BONSAI_CDC_COMMAND_SIZE 40u

/* One storage-thread producer, one CDC callback consumer. Admission is all or
 * nothing; publication follows the complete copy. Source memory is not retained.
 * Reset requires both owners excluded. No driver calls, allocation or sleeps. */
struct bonsai_cdc_tx {
 uint8_t bytes[BONSAI_CDC_CAPACITY];
 _Atomic uint32_t written,read;
};
void bonsai_cdc_reset(struct bonsai_cdc_tx *q);
uint32_t bonsai_cdc_pending(const struct bonsai_cdc_tx *q);
uint32_t bonsai_cdc_room(const struct bonsai_cdc_tx *q);
bool bonsai_cdc_write(struct bonsai_cdc_tx *q,const uint8_t *data,uint32_t count);
/* Consumer obtains one contiguous span, then consumes only the driver's actual
 * accepted byte count. A zero/partial acceptance never discards the remainder. */
uint32_t bonsai_cdc_peek(const struct bonsai_cdc_tx *q,const uint8_t **data);
bool bonsai_cdc_consume(struct bonsai_cdc_tx *q,uint32_t count);

enum bonsai_cdc_command { BONSAI_CDC_IDLE, BONSAI_CDC_STATUS,
 BONSAI_CDC_MIRROR, BONSAI_CDC_LIBRARY, BONSAI_CDC_LOAD,
 BONSAI_CDC_PROFILE, BONSAI_CDC_ENTER };
struct bonsai_cdc_parser {
 char line[BONSAI_CDC_COMMAND_SIZE];
 uint8_t used,enter_match;
 bool overflow;
 enum bonsai_cdc_command pending;
};
void bonsai_cdc_parser_reset(struct bonsai_cdc_parser *p);
/* false means a command is already pending: caller must not remove a new byte
 * from RX. Malformed/overlong lines have no side effects. Transfer magic retains
 * its existing un-delimited form. One pending command bounds work per refill. */
bool bonsai_cdc_parse(struct bonsai_cdc_parser *p,uint8_t byte);
bool bonsai_cdc_admissible(const struct bonsai_cdc_parser *p,
                          const struct bonsai_cdc_tx *q);
#endif
