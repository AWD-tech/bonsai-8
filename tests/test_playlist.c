#include "bonsai_playlist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
 uint32_t rows[16];for(unsigned i=0;i<16;i++)rows[i]=BONSAI_PLAYLIST_DEFAULT;
 int cursor=-1;for(int i=0;i<48;i++){cursor=bonsai_playlist_next(rows,65535,0,cursor,1);assert(cursor==i%16);}
 assert(bonsai_playlist_next(rows,0,0,0,1)==-1);
 assert(bonsai_playlist_next(rows,65535,2,0,1)==-1);
 rows[0]=12000|(1u<<16);rows[1]=9000|(3u<<16);rows[2]=12000|(2u<<16);rows[3]=0;
 assert(bonsai_playlist_next(rows,15,0,-1,1)==1);
 assert(bonsai_playlist_next(rows,15,0,1,1)==0);
 assert(bonsai_playlist_next(rows,15,0,0,1)==1); /* wrap */
 assert(bonsai_playlist_next(rows,15,1,1,1)==2);
 assert(bonsai_playlist_next(rows,15,1,2,-1)==1);
 assert(bonsai_playlist_next(rows,15,1,0,1)==2); /* non-member cursor */
 assert(bonsai_playlist_next(rows,1,1,0,1)==-1); /* empty B */
 assert(bonsai_playlist_next(rows,17,0,0,1)==4); /* unknown BPM last */
 assert(bonsai_playlist_next(rows,16,0,4,1)==4); /* singleton */
 uint8_t banks[1024]={0};assert(bonsai_playlist_bank(banks)==-1);
 if(argc==2){FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(banks,1,1024,f)==1024);fclose(f);struct bonsai_playlist saved;
  assert(bonsai_playlist_bank(banks)==1);assert(bonsai_playlist_decode(banks+512,&saved));assert(saved.generation==0);
  assert(saved.row[0]==(12345|(1u<<16)));assert(saved.row[1]==(3u<<16));
  uint8_t meta[1024],x3[1040];for(unsigned i=0;i<1024;i++)meta[i]=(uint8_t)i;for(unsigned i=0;i<1040;i++)x3[i]=(uint8_t)(i*3);
  assert(saved.identity[0]==bonsai_playlist_identity(meta,x3,0));
  banks[600]^=1;assert(bonsai_playlist_bank(banks)==0);banks[100]^=1;assert(bonsai_playlist_bank(banks)==-1);
 }
 puts("playlist ordering, independence and bank validation passed");return 0;
}
