#include "bonsai_cdc.h"
#include "dual_mirror.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static struct bonsai_cdc_tx q;
static void test_partial_and_wrap(void)
{
 uint8_t input[4096],actual[4096];
 for(unsigned i=0;i<sizeof(input);i++)input[i]=(uint8_t)(i*37u);
 bonsai_cdc_reset(&q);
 atomic_store(&q.written,UINT32_MAX-63);atomic_store(&q.read,UINT32_MAX-63);
 assert(bonsai_cdc_write(&q,input,3000));
 uint32_t w=atomic_load(&q.written);
 assert(!bonsai_cdc_write(&q,input,1097)&&atomic_load(&q.written)==w);
 assert(!bonsai_cdc_write(&q,NULL,1));
 assert(bonsai_cdc_write(&q,NULL,0));
 static const unsigned accepted[]={0,1,17,64,256};unsigned got=0,turn=0;
 while(got<3000) {
  const uint8_t *p;unsigned n=bonsai_cdc_peek(&q,&p),take=accepted[turn++%5];
  if(take>n)take=n;
  memcpy(actual+got,p,take);assert(bonsai_cdc_consume(&q,take));got+=take;
 }
 assert(!memcmp(input,actual,3000)&&bonsai_cdc_pending(&q)==0);
 assert(!bonsai_cdc_consume(&q,1));
 assert(bonsai_cdc_write(&q,input,4096));
 assert(bonsai_cdc_room(&q)==0&&!bonsai_cdc_write(&q,input,1));
 bonsai_cdc_reset(&q);assert(bonsai_cdc_pending(&q)==0);
}
static void test_pending_commands_and_edges(void)
{
 struct bonsai_cdc_parser p={0};struct dd_ui_queue ui={0};uint16_t values[13]={0};
 uint8_t full[BONSAI_CDC_CAPACITY]={0};bonsai_cdc_reset(&q);
 assert(bonsai_cdc_write(&q,full,sizeof(full)));
 for(const char *c="DDMIR?\n";*c;c++)assert(bonsai_cdc_parse(&p,(uint8_t)*c));
 assert(p.pending==BONSAI_CDC_MIRROR&&!bonsai_cdc_admissible(&p,&q));
 assert(!bonsai_cdc_parse(&p,'S')); /* cannot eat next request while busy */
 for(unsigned i=0;i<16;i++) {
  values[0]=(i&1)?0:16;values[5]=(i&1)?0:1000;values[12]=(i&1)?1000:0;
  assert(dd_ui_push(&ui,100+i*8,values,true));
 }
 assert(ui.count==16&&ui.lost==0); /* waiting does not dequeue/coalesce */
 assert(bonsai_cdc_consume(&q,2048)&&bonsai_cdc_admissible(&p,&q));
 for(unsigned i=0;i<16;i++) {
  struct dd_ui_frame f;assert(dd_ui_pop(&ui,&f));
  assert(f.seq==i+1&&f.value[0]==((i&1)?0:16));
  assert(f.value[5]==((i&1)?0:1000)&&f.value[12]==((i&1)?1000:0));
 }
 bonsai_cdc_parser_reset(&p);
 for(const char *c="SP1XFER!";*c;c++)assert(bonsai_cdc_parse(&p,(uint8_t)*c));
 assert(p.pending==BONSAI_CDC_ENTER&&!bonsai_cdc_admissible(&p,&q));
 assert(bonsai_cdc_consume(&q,2048)&&bonsai_cdc_admissible(&p,&q));
 bonsai_cdc_parser_reset(&p);
 for(unsigned i=0;i<80;i++)assert(bonsai_cdc_parse(&p,'X'));
 for(const char *c="DDLOAD 0 1\n";*c;c++)assert(bonsai_cdc_parse(&p,(uint8_t)*c));
 assert(p.pending==BONSAI_CDC_IDLE);
 for(const char *c="DDLOAD 1 16\r\n";*c;c++)assert(bonsai_cdc_parse(&p,(uint8_t)*c));
 assert(p.pending==BONSAI_CDC_LOAD&&!strcmp(p.line,"DDLOAD 1 16"));
 bonsai_cdc_parser_reset(&p);assert(p.pending==BONSAI_CDC_IDLE&&!p.used);
}
#define TOTAL 1000000u
static void *producer(void *unused)
{
 (void)unused;uint8_t data[113];unsigned at=0;
 while(at<TOTAL) {
  unsigned n=TOTAL-at;if(n>sizeof(data))n=sizeof(data);
  for(unsigned i=0;i<n;i++)data[i]=(uint8_t)((at+i)*29u);
  if(bonsai_cdc_write(&q,data,n))at+=n;
 }
 return NULL;
}
static void test_concurrent_publication(void)
{
 bonsai_cdc_reset(&q);pthread_t thread;assert(!pthread_create(&thread,NULL,producer,NULL));
 for(unsigned at=0;at<TOTAL;) {
  const uint8_t *p;unsigned n=bonsai_cdc_peek(&q,&p);if(n>71)n=71;
  for(unsigned i=0;i<n;i++)assert(p[i]==(uint8_t)((at+i)*29u));
  assert(bonsai_cdc_consume(&q,n));at+=n;
 }
 assert(!pthread_join(thread,NULL));assert(bonsai_cdc_pending(&q)==0);
}
int main(void)
{
 test_partial_and_wrap();test_pending_commands_and_edges();test_concurrent_publication();
 puts("PASS: whole-response CDC admission, partial/zero acceptance, wrap, pending command/edge retention, transfer barrier and concurrent publication");
}
