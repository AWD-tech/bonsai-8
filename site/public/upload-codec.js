// P14S packing is shared by the upload worker and existing format tests.
const magic=new TextEncoder().encode('P14S');
export function encodeP14S(left,right,start=0){
 const out=new Uint8Array(512);out.set(magic);out[5]=1;out[11]=0x5b;out[12]=24;out[13]=1;
 const value=(channel,i)=>{const f=Math.max(-1,Math.min(1,channel[i]||0));return ((Math.round(f*(f<0?32768:32767))>>2)&16383);};
 for(let g=0;g<70;g++){const p=16+g*7,at=start+g*2,a=value(left,at),b=value(right,at),c=value(left,at+1),d=value(right,at+1);out[p]=a>>6;out[p+1]=(a<<2)|(b>>12);out[p+2]=b>>4;out[p+3]=(b<<4)|(c>>10);out[p+4]=c>>2;out[p+5]=(c<<6)|(d>>8);out[p+6]=d;}
 return out;
}
