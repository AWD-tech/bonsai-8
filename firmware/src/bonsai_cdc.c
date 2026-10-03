/* SPDX-License-Identifier: MIT */
#include "bonsai_cdc.h"
#include <string.h>
_Static_assert((BONSAI_CDC_CAPACITY&(BONSAI_CDC_CAPACITY-1))==0,"power-of-two TX ring");
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"CDC publication requires lock-free atomics");
void bonsai_cdc_reset(struct bonsai_cdc_tx *q)
{atomic_store(&q->read,0);atomic_store(&q->written,0);}
uint32_t bonsai_cdc_pending(const struct bonsai_cdc_tx *q)
{
 uint32_t w=atomic_load_explicit(&q->written,memory_order_acquire);
 uint32_t r=atomic_load_explicit(&q->read,memory_order_acquire);
 return w-r;
}
uint32_t bonsai_cdc_room(const struct bonsai_cdc_tx *q)
{uint32_t used=bonsai_cdc_pending(q);return used<=BONSAI_CDC_CAPACITY?BONSAI_CDC_CAPACITY-used:0;}
bool bonsai_cdc_write(struct bonsai_cdc_tx *q,const uint8_t *data,uint32_t count)
{
 if(count>BONSAI_CDC_CAPACITY||(count&&!data)||count>bonsai_cdc_room(q))return false;
 uint32_t w=atomic_load_explicit(&q->written,memory_order_relaxed);
 uint32_t at=w&(BONSAI_CDC_CAPACITY-1),first=BONSAI_CDC_CAPACITY-at;
 if(first>count)first=count;
 if(first)memcpy(q->bytes+at,data,first);
 if(first<count)memcpy(q->bytes,data+first,count-first);
 atomic_store_explicit(&q->written,w+count,memory_order_release);return true;
}
uint32_t bonsai_cdc_peek(const struct bonsai_cdc_tx *q,const uint8_t **data)
{
 uint32_t r=atomic_load_explicit(&q->read,memory_order_relaxed);
 uint32_t w=atomic_load_explicit(&q->written,memory_order_acquire),n=w-r;
 uint32_t at=r&(BONSAI_CDC_CAPACITY-1),span=BONSAI_CDC_CAPACITY-at;
 *data=q->bytes+at;
 if(n>BONSAI_CDC_CAPACITY)return 0;
 return n<span?n:span;
}
bool bonsai_cdc_consume(struct bonsai_cdc_tx *q,uint32_t count)
{
 uint32_t r=atomic_load_explicit(&q->read,memory_order_relaxed);
 uint32_t w=atomic_load_explicit(&q->written,memory_order_acquire);
 if(w-r>BONSAI_CDC_CAPACITY||count>w-r)return false;
 atomic_store_explicit(&q->read,r+count,memory_order_release);return true;
}
void bonsai_cdc_parser_reset(struct bonsai_cdc_parser *p)
{memset(p,0,sizeof(*p));}
bool bonsai_cdc_parse(struct bonsai_cdc_parser *p,uint8_t byte)
{
 static const char enter[]="SP1XFER!";
 if(p->pending!=BONSAI_CDC_IDLE)return false;
 p->enter_match=byte==(uint8_t)enter[p->enter_match]?p->enter_match+1:byte=='S'?1:0;
 if(p->enter_match==8){p->pending=BONSAI_CDC_ENTER;p->enter_match=0;return true;}
 if(byte=='\n') {
  p->line[p->used]=0;
  if(!p->overflow) {
   if(!strcmp(p->line,"DDSTAT?"))p->pending=BONSAI_CDC_STATUS;
   else if(!strcmp(p->line,"DDMIR?"))p->pending=BONSAI_CDC_MIRROR;
   else if(!strcmp(p->line,"DDLIB?"))p->pending=BONSAI_CDC_LIBRARY;
   else if(!strcmp(p->line,"DDPROF?"))p->pending=BONSAI_CDC_PROFILE;
   else if(!strncmp(p->line,"DDLOAD ",7))p->pending=BONSAI_CDC_LOAD;
  }
  if(p->pending==BONSAI_CDC_IDLE){p->used=0;p->overflow=false;}
 } else if(byte!='\r') {
  if(p->used<sizeof(p->line)-1)p->line[p->used++]=(char)byte;
  else p->overflow=true;
 }
 return true;
}
bool bonsai_cdc_admissible(const struct bonsai_cdc_parser *p,const struct bonsai_cdc_tx *q)
{
 if(p->pending==BONSAI_CDC_IDLE)return false;
 if(p->pending==BONSAI_CDC_ENTER)return bonsai_cdc_pending(q)==0;
 return bonsai_cdc_room(q)>=BONSAI_CDC_REPLY_MAX;
}
