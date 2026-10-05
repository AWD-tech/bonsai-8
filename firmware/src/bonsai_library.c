/* SPDX-License-Identifier: MIT */
#include "bonsai_library.h"
#include <string.h>

/* Little-endian on-card layout from main.c's meta_blk/slot_state/x3_tab;
 * explicit byte access avoids native alignment, packing and endian dependence.
 * tools/sp1.py update_song uses the same offsets. */
enum {
 META_MAGIC=0x53453341u, X3_MAGIC=0x53453358u,
 META_SLOT=8, SLOT_BYTES=44, META_CONTENT=716, META_CHOP=976,
 META_SONG_MODE=1008, X3_ROWS=16, X3_SLOT_BYTES=64, X3_ROWS_END=1040
};
static uint16_t read16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8); }
static uint32_t read32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void write16(uint8_t *p,uint16_t n)
{ p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void write32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24); }
static uint16_t checksum(const uint8_t *x3)
{
 uint16_t sum=0;
 for(unsigned i=X3_ROWS;i<X3_ROWS_END;i++) sum=(uint16_t)(sum+x3[i]);
 return sum;
}
static bool zero(const uint8_t *p,unsigned n)
{
 for(unsigned i=0;i<n;i++) if(p[i]) return false;
 return true;
}
static bool valid_empty(const uint8_t *meta,const uint8_t *x3,uint32_t slot)
{
 if(!meta||!x3||slot>=BONSAI_LIBRARY_SLOTS) return false;
 if(read32(meta)!=META_MAGIC||read32(meta+4)>=BONSAI_LIBRARY_SLOTS||
    read32(x3)!=X3_MAGIC||read16(x3+4)!=1||read16(x3+6)!=checksum(x3)) return false;
 return zero(meta+META_SLOT+slot*SLOT_BYTES+8,4);
}
static bool valid_length(uint32_t frames,uint32_t sectors)
{
 return frames&&frames<=BONSAI_LIBRARY_MAX_NATIVE_FRAMES&&
  sectors&&sectors<=BONSAI_LIBRARY_TRACK_SECTORS&&
  sectors==frames/BONSAI_LIBRARY_SECTOR_FRAMES+
           (frames%BONSAI_LIBRARY_SECTOR_FRAMES!=0);
}
bool bonsai_library_prepare(uint8_t meta[BONSAI_LIBRARY_META_BYTES],
                            uint8_t x3[BONSAI_LIBRARY_X3_BYTES],uint32_t slot,
                            uint32_t frames,uint32_t sectors)
{
 if(!valid_empty(meta,x3,slot)||!valid_length(frames,sectors)) return false;
 uint8_t *song=meta+META_SLOT+slot*SLOT_BYTES;
 uint8_t *content=meta+META_CONTENT+slot*16;
 uint8_t *rows=x3+X3_ROWS+slot*X3_SLOT_BYTES;
 memset(song,0,SLOT_BYTES);write32(song,65536);write32(song+4,frames*2);
 write32(song+12,sectors);
 memset(content,0,16);write32(content,sectors);
 memset(meta+META_CHOP+slot*2,0,2);meta[META_SONG_MODE+slot]=0;
 memset(rows,0,X3_SLOT_BYTES);
 write32(rows+4,frames*2);write32(rows+8,sectors);
 rows[12]=5;rows[13]=1;rows[14]=128;
 write16(x3+6,checksum(x3));
 return true;
}
bool bonsai_library_publish(uint8_t meta[BONSAI_LIBRARY_META_BYTES],
                            const uint8_t x3[BONSAI_LIBRARY_X3_BYTES],uint32_t slot)
{
 if(!valid_empty(meta,x3,slot)) return false;
 uint8_t *song=meta+META_SLOT+slot*SLOT_BYTES;
 const uint8_t *content=meta+META_CONTENT+slot*16;
 const uint8_t *rows=x3+X3_ROWS+slot*X3_SLOT_BYTES;
 uint32_t length=read32(song+4),sectors=read32(song+12);
 if((length&1)||!valid_length(length/2,sectors)||read32(song)!=65536||
    !zero(song+16,28)||read32(content)!=sectors||!zero(content+4,12)||
    !zero(meta+META_CHOP+slot*2,2)||meta[META_SONG_MODE+slot]||
    read32(rows)!=0||read32(rows+4)!=length||read32(rows+8)!=sectors||
    rows[12]!=5||rows[13]!=1||rows[14]!=128||rows[15]||
    !zero(rows+16,48)) return false;
 song[8]=1;
 return true;
}
