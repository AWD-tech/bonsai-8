// Physical telemetry, independent of audio gains and browser audio analysis.
export const MIRROR_KEYS=['track0','track1','track2','track3','play','function','forward','rewind','volumeUp','volumeDown'];
export function mirrorPacket(line){
 let p;try{p=JSON.parse(line);}catch{return null;}
 const uint=(v,max=0xffffffff)=>Number.isInteger(v)&&v>=0&&v<=max;
 if(p?.mirror!==1||!uint(p.now)||!uint(p.lost)||!Array.isArray(p.frames)||p.frames.length>16)return null;
 if(!p.frames.every(f=>Array.isArray(f)&&f.length===15&&uint(f[0])&&uint(f[1])&&uint(f[2],1023)&&f.slice(3,7).every(v=>uint(v,256))&&f.slice(7).every(v=>uint(v,1000))))return null;
 return {...p,frames:p.frames.map(f=>({seq:f[0],ms:f[1],buttons:Object.fromEntries(MIRROR_KEYS.map((key,i)=>[key,Boolean(f[2]&(1<<i))])),faders:f.slice(3,7).map(v=>v/256),trackLeds:f.slice(7,11).map(v=>v/1000),statusLeds:f.slice(11,15).map(v=>v/1000)}))};
}
export class MirrorTimeline {
 constructor(apply){this.apply=apply;this.reset();}
 reset(){this.queue=[];this.lastSeq=null;this.lastNow=null;this.lastReceived=null;this.lost=null;this.stale=false;this.target=null;}
 push(packet,received){
  const gap=this.lastReceived!==null&&received-this.lastReceived>1000;
  const loss=this.lost!==null&&packet.lost!==this.lost;
  const elapsed=this.lastNow===null?0:((packet.now-this.lastNow)>>>0);
  const restart=elapsed>0x7fffffff;
  if(gap||loss||restart){this.queue=[];this.lastSeq=null;this.target=null;}
  // A small fixed display buffer keeps short press/release and blink timing
  // when several hardware edges arrive in the same USB read.
  this.target=this.target===null?received+60:Math.min(this.target+elapsed,received+60);
  this.lastNow=packet.now;this.lastReceived=received;this.lost=packet.lost;this.stale=false;
  const frames=gap||loss||restart?packet.frames.slice(-1):packet.frames;
  for(const f of frames){
   if(this.lastSeq!==null&&((f.seq-this.lastSeq)|0)<=0)continue;
   this.lastSeq=f.seq;
   this.queue.push({at:this.target+((f.ms-packet.now)|0),frame:f});
  }
  if(this.queue.length>128)this.queue=this.queue.slice(-1);
  return gap||loss||restart;
 }
 tick(now){
  if(this.lastReceived!==null&&now-this.lastReceived>1000){
   if(!this.stale){this.stale=true;this.queue=[];this.apply(null);}
   return;
  }
  while(this.queue.length&&this.queue[0].at<=now)this.apply(this.queue.shift().frame);
 }
}
