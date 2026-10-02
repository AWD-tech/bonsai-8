#include "bonsai_library.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t meta[1024],x3[1536],before_meta[1024],before_x3[1536];
static uint8_t expected_meta[1024],expected_x3[1536];
static void put16(uint8_t *p,uint16_t n)
{ p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void put32(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void sum_rows(uint8_t *table)
{
 unsigned sum=0;
 for(unsigned i=16;i<1040;i++) sum+=table[i];
 put16(table+6,(uint16_t)sum);
}
static void fixture(unsigned empty_slot)
{
 /* Nonzero unrelated settings/reserved bytes make accidental zeroing visible.
  * All other slots are occupied, including the first and last when possible. */
 for(unsigned i=0;i<sizeof(meta);i++) meta[i]=(uint8_t)(i*37+19);
 for(unsigned i=0;i<sizeof(x3);i++) x3[i]=(uint8_t)(i*53+7);
 put32(meta,0x53453341);put32(meta+4,15);
 for(unsigned s=0;s<16;s++) memset(meta+16+44*s,1,4);
 memset(meta+16+44*empty_slot,0,4);
 put32(x3,0x53453358);put16(x3+4,1);sum_rows(x3);
}
static void snapshot(void)
{ memcpy(before_meta,meta,sizeof(meta));memcpy(before_x3,x3,sizeof(x3)); }
static void unchanged(void)
{ assert(!memcmp(meta,before_meta,sizeof(meta)));assert(!memcmp(x3,before_x3,sizeof(x3))); }
static void reject_prepare(uint32_t slot,uint32_t frames,uint32_t sectors)
{
 snapshot();assert(!bonsai_library_prepare(meta,x3,slot,frames,sectors));unchanged();
}
static void reject_publish(unsigned slot)
{
 snapshot();assert(!bonsai_library_publish(meta,x3,slot));unchanged();
}
static void test_exact_layout_and_preservation(unsigned slot,uint32_t frames)
{
 uint32_t sectors=frames/140+(frames%140!=0);
 fixture(slot);memcpy(expected_meta,meta,sizeof(meta));memcpy(expected_x3,x3,sizeof(x3));
 /* Golden byte layout independently encoded from meta_blk and x3_trk.
  * Only one target stem is published; a mix recording is not four copies. */
 unsigned off=8+44*slot,row=16+64*slot;
 memset(expected_meta+off,0,44);
 put32(expected_meta+off,65536);put32(expected_meta+off+4,frames*2);
 put32(expected_meta+off+12,sectors);
 memset(expected_meta+716+16*slot,0,16);put32(expected_meta+716+16*slot,sectors);
 expected_meta[976+2*slot]=expected_meta[977+2*slot]=0;
 expected_meta[1008+slot]=0;
 memset(expected_x3+row,0,64);
 put32(expected_x3+row+4,frames*2);put32(expected_x3+row+8,sectors);
 expected_x3[row+12]=5;expected_x3[row+13]=1;expected_x3[row+14]=128;
 sum_rows(expected_x3);
 assert(bonsai_library_prepare(meta,x3,slot,frames,sectors));
 assert(!memcmp(meta,expected_meta,sizeof(meta)));
 assert(!memcmp(x3,expected_x3,sizeof(x3)));
 assert(!meta[off+8]&&!meta[off+9]&&!meta[off+10]&&!meta[off+11]);
 assert(get32(meta+off+4)==frames*2&&get32(x3+row+4)==frames*2);
 /* The publish step changes exactly one byte; X3 and other slots survive. */
 snapshot();assert(bonsai_library_publish(meta,x3,slot));
 before_meta[off+8]=1;unchanged();
 reject_publish(slot); /* An occupied slot is never re-prepared or republished. */
 reject_prepare(slot,frames,sectors);
}
static void test_reject_without_mutation(void)
{
 fixture(0);reject_prepare(16,140,1);reject_prepare(UINT32_MAX,140,1);
 reject_prepare(0,0,0);reject_prepare(0,0,1);reject_prepare(0,1,0);
 reject_prepare(0,140,2);reject_prepare(0,141,1);reject_prepare(0,141,3);
 reject_prepare(0,BONSAI_LIBRARY_MAX_NATIVE_FRAMES+1,82305);
 reject_prepare(0,UINT32_MAX,UINT32_MAX);
 reject_prepare(0,140,BONSAI_LIBRARY_TRACK_SECTORS+1);
 for(unsigned stem=0;stem<4;stem++) {
  fixture(5);meta[16+44*5+stem]=0x80;reject_prepare(5,141,2);reject_publish(5);
 }
 fixture(0);meta[0]^=1;reject_prepare(0,140,1);reject_publish(0);
 fixture(0);put32(meta+4,16);reject_prepare(0,140,1);reject_publish(0);
 fixture(0);x3[0]^=1;reject_prepare(0,140,1);reject_publish(0);
 fixture(0);put16(x3+4,2);reject_prepare(0,140,1);reject_publish(0);
 fixture(0);x3[6]^=1;reject_prepare(0,140,1);reject_publish(0);
 fixture(0);x3[1039]^=1;reject_prepare(0,140,1);reject_publish(0);
 fixture(0);snapshot();
 assert(!bonsai_library_prepare(NULL,x3,0,140,1));
 assert(!bonsai_library_prepare(meta,NULL,0,140,1));
 assert(!bonsai_library_publish(NULL,x3,0));
 assert(!bonsai_library_publish(meta,NULL,0));
 assert(!bonsai_library_publish(meta,x3,16));unchanged();
 fixture(0);reject_publish(0); /* Merely empty is not a prepared recording. */
}
static void prepared(unsigned slot)
{ fixture(slot);assert(bonsai_library_prepare(meta,x3,slot,141,2)); }
static void test_publish_revalidates_every_target_field(void)
{
 const unsigned slot=8;
 for(unsigned i=0;i<44;i++) {
  prepared(slot);meta[8+44*slot+i]^=1;reject_publish(slot);
 }
 for(unsigned i=0;i<16;i++) {
  prepared(slot);meta[716+16*slot+i]^=1;reject_publish(slot);
 }
 for(unsigned i=0;i<2;i++) {
  prepared(slot);meta[976+2*slot+i]^=1;reject_publish(slot);
 }
 prepared(slot);meta[1008+slot]=1;reject_publish(slot);
 for(unsigned i=0;i<64;i++) {
  prepared(slot);x3[16+64*slot+i]^=1;sum_rows(x3);reject_publish(slot);
 }
 prepared(slot);x3[7]^=1;reject_publish(slot);
 prepared(slot);x3[18]^=1;reject_publish(slot); /* Unrelated row corruption. */
}
int main(void)
{
 const uint32_t frames[]={1,139,140,141,280,281,BONSAI_LIBRARY_MAX_NATIVE_FRAMES};
 for(unsigned slot=0;slot<16;slot++)
  for(unsigned i=0;i<sizeof(frames)/sizeof(frames[0]);i++)
   test_exact_layout_and_preservation(slot,frames[i]);
 test_reject_without_mutation();test_publish_revalidates_every_target_field();
 puts("PASS: recording metadata layout, two-stage publish, all-slot/tail preservation, lengths and fail-closed validation");
}
