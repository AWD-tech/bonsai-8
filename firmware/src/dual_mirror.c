/* SPDX-License-Identifier: MIT */
#include "dual_mirror.h"
#include <stdio.h>
#include <string.h>
bool dd_ui_push(struct dd_ui_queue *q,uint32_t ms,const uint16_t value[13],bool connected)
{
 if(q->valid&&!memcmp(value,q->latest.value,sizeof(q->latest.value))&&(uint32_t)(ms-q->latest.ms)<100u) return false;
 q->latest.seq=++q->seq;q->latest.ms=ms;memcpy(q->latest.value,value,sizeof(q->latest.value));q->valid=true;
 if(!connected) return true;
 if(q->count==DD_UI_CAPACITY) {q->head=(q->head+1)%DD_UI_CAPACITY;q->count--;q->lost++;}
 q->frame[(q->head+q->count)%DD_UI_CAPACITY]=q->latest;q->count++;return true;
}
bool dd_ui_pop(struct dd_ui_queue *q,struct dd_ui_frame *out)
{
 if(!q->count) return false;
 *out=q->frame[q->head];q->head=(q->head+1)%DD_UI_CAPACITY;q->count--;return true;
}
void dd_ui_start(struct dd_ui_queue *q)
{
 q->head=q->count=q->lost=0;
 if(q->valid) {q->frame[0]=q->latest;q->count=1;}
}
int dd_ui_format(char *out,size_t size,const struct dd_ui_frame *f)
{
 return snprintf(out,size,"[%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
  (unsigned long)f->seq,(unsigned long)f->ms,f->value[0],f->value[1],f->value[2],f->value[3],f->value[4],
  f->value[5],f->value[6],f->value[7],f->value[8],f->value[9],f->value[10],f->value[11],f->value[12]);
}
uint16_t dd_ui_tracks(int raw)
{
 /* Upstream's measured resistor-ladder bands, including supported chords.
  * Unknown PLAY+multiple-track combinations are not guessed. */
 static const uint16_t high[]={110,308,488,650,795,925,1044,1154,1256,1347,1430,1509,1583,1650,1713,1773,1840,1886,1957,2081,2267};
 static const uint8_t mask[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,20,24};
 if(raw<0) return 0;
 for(unsigned i=0;i<sizeof(mask);i++) if(raw<high[i]) return mask[i];
 return 0;
}
