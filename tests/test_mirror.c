#include "dual_mirror.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
 struct dd_ui_queue q={0};struct dd_ui_frame f;uint16_t value[13]={0};char line[160];
 value[1]=23;value[5]=52;value[9]=66;
 assert(dd_ui_push(&q,100,value,false));dd_ui_start(&q);
 assert(dd_ui_pop(&q,&f)&&f.value[1]==23&&f.value[5]==52&&f.value[9]==66);
 /* Press and release both survive a host polling interval. */
 value[0]=16;dd_ui_push(&q,108,value,true);value[0]=0;dd_ui_push(&q,140,value,true);
 assert(dd_ui_pop(&q,&f)&&f.ms==108&&f.value[0]==16);
 assert(dd_ui_pop(&q,&f)&&f.ms==140&&f.value[0]==0);assert(!dd_ui_pop(&q,&f));
 assert(!dd_ui_push(&q,150,value,true));assert(dd_ui_push(&q,240,value,true));dd_ui_start(&q);
 for(unsigned i=0;i<80;i++) {value[0]=i&1;dd_ui_push(&q,300+i*8,value,true);}
 assert(q.count==64&&q.lost>0);unsigned n=0;while(dd_ui_pop(&q,&f)) n++;assert(n==64);
 /* Uptime/counter wrap and maximum wire row stay bounded. */
 q.latest.ms=0xfffffff0u;q.seq=0xffffffffu;value[0]=1023;
 assert(dd_ui_push(&q,32,value,true));assert(dd_ui_pop(&q,&f)&&f.seq==0);
 int len=dd_ui_format(line,sizeof(line),&f);assert(len>0&&(size_t)len<sizeof(line));
 assert(strstr(line,"[0,32,1023,23,")==line);
 assert(dd_ui_tracks(213)==1&&dd_ui_tracks(1303)==9&&dd_ui_tracks(1743)==15);
 assert(dd_ui_tracks(1807)==16&&dd_ui_tracks(1861)==17&&dd_ui_tracks(2159)==24);
 assert(dd_ui_tracks(-1)==0&&dd_ui_tracks(2400)==0);
 puts("PASS: mirror preserves tap edges, physical values, LED duties, bounded overflow and counter wrap");
}
