// Bonsai 8 storage format. The index owns visibility; audio is published only
// after every acknowledged sector and the extended table have been verified.
export const STORAGE = Object.freeze({sector:512,slots:16,stems:4,base:4096,trackBlocks:86016,magic:0x53453341,x3Magic:0x53453358});
const bytes = text => new TextEncoder().encode(text);
const equal = (a,b) => a.length===b.length&&a.every((v,i)=>v===b[i]);
const view = data => new DataView(data.buffer,data.byteOffset,data.byteLength);
export const join = (...parts) => {const out=new Uint8Array(parts.reduce((n,p)=>n+p.length,0));let at=0;for(const p of parts){out.set(p,at);at+=p.length;}return out;};
export const u32 = n => {const a=new Uint8Array(4);view(a).setUint32(0,n,true);return a;};
export async function sha256(data){return [...new Uint8Array(await crypto.subtle.digest('SHA-256',data))].map(x=>x.toString(16).padStart(2,'0')).join('');}
function slotIndex(slot){if(!Number.isInteger(slot)||slot<1||slot>16)throw new Error('Choose a song slot from 1 to 16.');return slot-1;}
export function validateLayout(data){
 if(data.length!==28||!equal(data.slice(0,4),bytes('SP1!')))throw new Error('The player did not enter the expected transfer mode.');
 const d=view(data),actual=Array.from({length:6},(_,i)=>d.getUint32(4+i*4,true));
 if(!equal(actual,[512,16,4,4096,86016,STORAGE.magic]))throw new Error('Storage layout is incompatible. No songs were changed.');
 return actual;
}
export function validateIndex(meta){if(meta.length!==1024||view(meta).getUint32(0,true)!==STORAGE.magic||view(meta).getUint32(4,true)>=16)throw new Error('This song index is not compatible. Existing storage is preserved.');return meta;}
export function validateExtended(x3){const d=view(x3);if(x3.length!==1536||d.getUint32(0,true)!==STORAGE.x3Magic||d.getUint16(4,true)!==1||d.getUint16(6,true)!==x3.slice(16,1040).reduce((sum,x)=>(sum+x)&65535,0))throw new Error('Extended song metadata is invalid. Repair it before changing songs.');return x3;}
export function indexSongs(meta){validateIndex(meta);const d=view(meta);return Array.from({length:16},(_,i)=>({slot:i+1,present:[...meta.slice(16+i*44,20+i*44)],frames:d.getUint32(12+i*44,true),title:null})).filter(s=>s.present.some(Boolean));}
export function deleteSongIndex(meta,slot){validateIndex(meta);const out=meta.slice(),i=slotIndex(slot);out.fill(0,16+i*44,20+i*44);return out;}
export function validatePreparedStems(stems){
 if(!Array.isArray(stems)||stems.length!==4||stems.some(s=>!s||!Number.isInteger(s.frames)||s.frames<0||s.frames>11522560||s.blocks!==Math.ceil(s.frames/140)||s.blocks>STORAGE.trackBlocks||!(s.data instanceof Uint8Array)||s.data.length!==s.blocks*512)||!stems.some(s=>s.frames))throw new Error('Prepared stems are incomplete or exceed the supported eight-minute capacity.');
 return stems;
}
export function publishSongIndex(meta,table,slot,stems){
 validatePreparedStems(stems);
 validateIndex(meta);validateExtended(table);const i=slotIndex(slot),out=meta.slice(),x3=table.slice(),m=view(out),x=view(x3),length=Math.max(...stems.map(s=>s.frames));
 if(stems.length!==4||!length||length>11522560||stems.some(s=>!Number.isInteger(s.frames)||s.frames<0||s.frames>length||s.blocks!==Math.ceil(s.frames/140)||s.blocks>STORAGE.trackBlocks))throw new Error('Prepared stems exceed the supported eight-minute capacity.');
 if(out.slice(16+i*44,20+i*44).some(Boolean))throw new Error('That song slot is occupied. Delete it explicitly or choose an empty slot.');
 const at=8+i*44;m.setUint32(at,65536,true);m.setUint32(at+4,length*2,true);
 stems.forEach((stem,s)=>{out[at+8+s]=stem.frames?1:0;m.setUint32(at+12+s*4,Math.ceil(length/140),true);m.setUint32(at+28+s*4,0,true);m.setUint32(716+(i*4+s)*4,stem.blocks,true);const e=16+(i*4+s)*16;x3.fill(0,e,e+16);if(stem.frames){x.setUint32(e+4,length*2,true);x.setUint32(e+8,stem.blocks,true);x3[e+12]=5;x3[e+13]=1;x3[e+14]=128;}});
 out.fill(0,976+i*2,978+i*2);out[1008+i]=0;x.setUint16(6,x3.slice(16,1040).reduce((sum,v)=>(sum+v)&65535,0),true);return {meta:out,x3};
}
export function encodeP14S(left,right,start=0){
 const out=new Uint8Array(512);out.set(bytes('P14S'));out[5]=1;out[11]=0x5b;out[12]=24;out[13]=1;
 const value=(channel,i)=>{const f=Math.max(-1,Math.min(1,channel[i]||0));return ((Math.round(f*(f<0?32768:32767))>>2)&16383);};
 for(let g=0;g<70;g++){const p=16+g*7,at=start+g*2,a=value(left,at),b=value(right,at),c=value(left,at+1),d=value(right,at+1);out[p]=a>>6;out[p+1]=(a<<2)|(b>>12);out[p+2]=b>>4;out[p+3]=(b<<4)|(c>>10);out[p+4]=c>>2;out[p+5]=(c<<6)|(d>>8);out[p+6]=d;}
 return out;
}
export function decodeAudioSector(block){
 if(block.length!==512||block[11]!==0x5b||block[14]>3)throw new Error('Invalid audio sector. Export stopped.');const magic=new TextDecoder().decode(block.slice(0,4));
 if(magic==='P14S'&&block[12]===24&&block[13]===1){const out=new Int16Array(280),sample=n=>((n>8191?n-16384:n)*4)>>block[14];for(let g=0;g<70;g++){const p=16+g*7;out[g*4]=sample((block[p]<<6)|(block[p+1]>>2));out[g*4+1]=sample(((block[p+1]&3)<<12)|(block[p+2]<<4)|(block[p+3]>>4));out[g*4+2]=sample(((block[p+3]&15)<<10)|(block[p+4]<<2)|(block[p+5]>>6));out[g*4+3]=sample(((block[p+5]&63)<<8)|block[p+6]);}return {frames:140,pcm:out,codec:5};}
 if(magic==='P16M'&&block[12]===240&&block[13]===1&&block[14]===0){const out=new Int16Array(496),d=view(block);for(let i=0;i<248;i++)out[2*i]=out[2*i+1]=d.getInt16(16+i*2,true);return {frames:248,pcm:out,codec:6};}
 throw new Error('This audio codec cannot be exported by this version. No device data was changed.');
}
export function pcmWav(pcm,rate=24000){const out=new Uint8Array(44+pcm.length*2),d=view(out);out.set(bytes('RIFF'));d.setUint32(4,out.length-8,true);out.set(bytes('WAVEfmt '),8);d.setUint32(16,16,true);d.setUint16(20,1,true);d.setUint16(22,2,true);d.setUint32(24,rate,true);d.setUint32(28,rate*4,true);d.setUint16(32,4,true);d.setUint16(34,16,true);out.set(bytes('data'),36);d.setUint32(40,pcm.length*2,true);for(let i=0;i<pcm.length;i++)d.setInt16(44+i*2,pcm[i],true);return out;}
export async function prepareStems(buffers,onProgress=()=>{}){
 if(!buffers.some(Boolean))throw new Error('Separate a song or import stems first.');const result=[];
 for(let s=0;s<4;s++){const b=buffers[s];if(!b){result.push({frames:0,blocks:0,data:new Uint8Array()});continue;}const frames=Math.ceil(b.duration*24000);if(frames>11522560)throw new Error('Songs on the player must be shorter than eight minutes.');
 const ctx=new OfflineAudioContext(2,frames,24000),source=ctx.createBufferSource();source.buffer=b;source.connect(ctx.destination);source.start();const rendered=await ctx.startRendering(),blocks=Math.ceil(frames/140),data=new Uint8Array(blocks*512);for(let k=0;k<blocks;k++)data.set(encodeP14S(rendered.getChannelData(0),rendered.getChannelData(1),k*140),k*512);result.push({frames,blocks,data});onProgress((s+1)/4);await new Promise(r=>setTimeout(r,0));}
 return result;
}
export class DeviceLibrary {
 constructor(connection,{onProgress=()=>{},checkpointStore=globalThis.localStorage}={}){this.connection=connection;this.onProgress=onProgress;this.store=checkpointStore;}
 async run(work){return this.connection.withTransfer(async io=>{validateLayout(await io.exchange(bytes('SP1XFER!P'),28));let success=false;try{const result=await work(this.transport(io));success=true;return result;}finally{if(success)await io.exchange(bytes('X'),1).then(reply=>{if(reply[0]!==120)throw new Error('The player did not exit transfer mode.');});}});}
 transport(io){const ack=async(request,expected)=>{const reply=await io.exchange(request,1);if(reply[0]!==expected)throw new Error('The player rejected a storage operation. The song was not published.');};return {
 read:async block=>{if(!Number.isInteger(block)||block<0||block>=STORAGE.base+16*4*STORAGE.trackBlocks)throw new Error('Read outside song storage.');const reply=await io.exchange(join(bytes('R'),u32(block)),513);if(reply.length!==513||reply[0]!==114)throw new Error('Audio read failed.');return reply.slice(1);},
 write:async(block,data)=>{if(data.length!==512||!Number.isInteger(block)||block<0||block>=STORAGE.base+16*4*STORAGE.trackBlocks)throw new Error('Invalid storage write.');await ack(join(bytes('B'),u32(block),new Uint8Array([1]),data),98);},flush:()=>ack(bytes('F'),102)};}
 async snapshot(dev){const meta=join(await dev.read(0),await dev.read(1)),x3=join(await dev.read(3),await dev.read(4),await dev.read(5));validateIndex(meta);validateExtended(x3);return {meta,x3};}
 async list(){return this.run(async dev=>indexSongs((await this.snapshot(dev)).meta));}
 async publish(dev,meta,x3){
 // X3 first, index publication last. Verify post-flush so deferred media writes
 // cannot silently publish corrupt metadata. Unrelated slots are copied intact.
 if(x3){for(let i=0;i<3;i++)await dev.write(3+i,x3.slice(i*512,(i+1)*512));await dev.flush();if(!equal(join(await dev.read(3),await dev.read(4),await dev.read(5)),x3))throw new Error('Extended metadata verification failed. The song remains unpublished.');}
 await dev.write(1,meta.slice(512));await dev.write(0,meta.slice(0,512));await dev.flush();
 if(!equal(join(await dev.read(0),await dev.read(1)),meta))throw new Error('Song index verification failed. Reconnect before continuing.');
 if(x3&&!equal(join(await dev.read(3),await dev.read(4),await dev.read(5)),x3))throw new Error('Extended metadata verification failed. Reconnect before continuing.');
 }
 async remove(slot){return this.run(async dev=>{const {meta}=await this.snapshot(dev);if(!indexSongs(meta).some(s=>s.slot===slot))throw new Error('That song slot is already empty.');await this.publish(dev,deleteSongIndex(meta,slot));});}
 async upload(slot,stems){
 slotIndex(slot);validatePreparedStems(stems);const signature=await sha256(join(...stems.map(s=>join(u32(s.frames),s.data)))),key=`bonsai8-upload-${slot}-${signature}`;
 return this.run(async dev=>{const {meta,x3}=await this.snapshot(dev),next=publishSongIndex(meta,x3,slot,stems),identity=await sha256(join(meta,x3));let checkpoint;
 try{checkpoint=JSON.parse(this.store?.getItem(key)||'null');}catch{}
 if(checkpoint?.identity!==identity||!Array.isArray(checkpoint.verified)||checkpoint.verified.length!==4||checkpoint.verified.some((n,s)=>!Number.isInteger(n)||n<0||n>stems[s].blocks))checkpoint={identity,verified:[0,0,0,0]};const total=stems.reduce((n,s)=>n+s.blocks,0);let done=0;
 for(let s=0;s<4;s++){const stem=stems[s],base=STORAGE.base+((slot-1)*4+s)*STORAGE.trackBlocks,resume=Math.min(stem.blocks,Math.max(0,Number(checkpoint.verified[s])||0));
 // Before trusting a saved checkpoint, compare its whole verified prefix on
 // the connected player. Matching VID/PID alone does not identify a device.
 for(let b=0;b<resume;b++){if(!equal(await dev.read(base+b),stem.data.slice(b*512,(b+1)*512)))throw new Error('This player does not match the saved upload. No new writes were made to this stem.');done++;if(!(b%64))this.onProgress(done/total,'Checking saved upload');}
 for(let b=resume;b<stem.blocks;b++){await dev.write(base+b,stem.data.slice(b*512,(b+1)*512));checkpoint.verified[s]=b+1;done++;if(!(b%32)||b===stem.blocks-1){try{this.store?.setItem(key,JSON.stringify(checkpoint));}catch{}this.onProgress(done/total,`Uploading stem ${s+1} of 4`);}}
 }
 await dev.flush();for(let s=0;s<4;s++){const stem=stems[s],base=STORAGE.base+((slot-1)*4+s)*STORAGE.trackBlocks;for(const b of new Set([0,Math.floor(stem.blocks/2),stem.blocks-1]))if(b>=0&&b<stem.blocks&&!equal(await dev.read(base+b),stem.data.slice(b*512,(b+1)*512)))throw new Error('Audio verification failed. The song remains unpublished.');}
 await this.publish(dev,next.meta,next.x3);try{this.store?.removeItem(key);}catch{}return {slot,audio_id:signature,present:stems.map(s=>s.frames?1:0),frames:Math.max(...stems.map(s=>s.frames))*2,title:null};
 });
 }
 async export(slot){return this.run(async dev=>{const {meta,x3}=await this.snapshot(dev),i=slotIndex(slot),m=view(meta),x=view(x3),song=indexSongs(meta).find(s=>s.slot===slot);if(!song)throw new Error('Song is no longer present.');const result=[];
 for(let s=0;s<4;s++){if(!song.present[s])continue;const base=STORAGE.base+(i*4+s)*STORAGE.trackBlocks,e=16+(i*4+s)*16,first=decodeAudioSector(await dev.read(base)),blocks=m.getUint32(20+i*44+s*4,true)||Math.ceil(song.frames/(first.frames*2));if(!blocks||blocks>STORAGE.trackBlocks)throw new Error('Invalid song length.');const exact=x.getUint8(e+12)===first.codec&&x.getUint32(e+4,true)>0?Math.ceil(x.getUint32(e+4,true)/2):blocks*first.frames;if(exact>blocks*first.frames)throw new Error('Invalid extended song length.');const content=x.getUint32(e+8,true)||m.getUint32(716+(i*4+s)*4,true)||blocks;if(content>blocks)throw new Error('Invalid song content length.');const anchor=(x.getUint8(e+12)===first.codec?Math.floor(x.getUint32(e,true)/2):m.getUint32(36+i*44+s*4,true)*first.frames)%exact,pcm=new Int16Array(exact*2);
 for(let b=0;b<Math.min(blocks,content);b++){const decoded=b===0?first:decodeAudioSector(await dev.read(base+b));if(decoded.codec!==first.codec)throw new Error('Audio codec changed inside the song.');for(let f=0;f<decoded.frames&&b*first.frames+f<exact;f++){const destination=(b*first.frames+f+anchor)%exact;pcm[destination*2]=decoded.pcm[f*2];pcm[destination*2+1]=decoded.pcm[f*2+1];}if(!(b%32))this.onProgress((s+b/content)/4,`Exporting track ${s+1}`);}
 result.push({stem:s,wav:pcmWav(pcm)});
 }return result;});}
}
