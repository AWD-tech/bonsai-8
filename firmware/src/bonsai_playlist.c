/* SPDX-License-Identifier: MIT */
#include "bonsai_playlist.h"
#include <stddef.h>
#include <string.h>
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t crc(uint32_t c,const uint8_t *p,size_t n){
 while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^(0xedb88320u& (0u-(c&1u)));}return c;
}
bool bonsai_playlist_decode(const uint8_t bank[512],struct bonsai_playlist *out){
 if(memcmp(bank,"B8PL",4)||u32(bank+4)!=1u||u32(bank+508)!=~crc(~0u,bank,508))return false;
 if(u32(bank+12))return false;
 for(unsigned i=144;i<508;i++)if(bank[i])return false;
 for(unsigned i=0;i<16;i++){
  uint32_t r=u32(bank+16+i*8),bpm=r&65535u;
  if((r&~0x3ffffu)||(bpm&&(bpm<2000u||bpm>40000u)))return false;
 }
 if(out){out->generation=u32(bank+8);for(unsigned i=0;i<16;i++){out->row[i]=u32(bank+16+i*8);out->identity[i]=u32(bank+20+i*8);}}
 return true;
}
int bonsai_playlist_bank(const uint8_t banks[1024]){
 bool a=bonsai_playlist_decode(banks,NULL),b=bonsai_playlist_decode(banks+512,NULL);
 if(!a)return b?1:-1;
 if(!b)return 0;
 uint32_t delta=u32(banks+520)-u32(banks+8);
 return delta&&delta<0x80000000u?1:0;
}
uint32_t bonsai_playlist_identity(const uint8_t meta[1024],const uint8_t x3[1040],unsigned slot){
 if(slot>=16)return 0;
 uint32_t c=crc(~0u,meta+8+slot*44,44);
 c=crc(c,meta+716+slot*16,16);return ~crc(c,x3+16+slot*64,64);
}
static uint32_t key(uint32_t row,unsigned slot){uint32_t bpm=row&65535u;return ((bpm?bpm:65536u)<<4)|slot;}
int bonsai_playlist_next(const uint32_t rows[16],uint16_t present,unsigned deck,int current,int direction){
 if(deck>1||!direction)return -1;
 uint32_t mask=1u<<(16+deck);int edge=-1,next=-1;
 uint32_t current_key=current>=0&&current<16?key(rows[current],(unsigned)current):0;
 for(unsigned s=0;s<16;s++)if((present&(1u<<s))&&(rows[s]&mask)){
  uint32_t k=key(rows[s],s);
  if(edge<0||(direction>0?k<key(rows[edge],(unsigned)edge):k>key(rows[edge],(unsigned)edge)))edge=(int)s;
  if(current>=0&&current<16&&(direction>0?k>current_key:k<current_key)&&
     (next<0||(direction>0?k<key(rows[next],(unsigned)next):k>key(rows[next],(unsigned)next))))next=(int)s;
 }
 return next<0?edge:next;
}
