import {encodeP14S} from './upload-codec.js?v=20261020-upload-01';
import {UploadSha256} from './upload-sha256.js?v=20261020-upload-01';
// MessageChannel does not depend on animation frames or foreground-tab timers.
// It also keeps the no-Worker fallback responsive without nested timer clamping.
export function yieldUpload(){return new Promise(resolve=>{if(typeof MessageChannel==='undefined'){setTimeout(resolve,0);return;}const channel=new MessageChannel();channel.port1.onmessage=()=>{channel.port1.close();channel.port2.close();resolve();};channel.port2.postMessage(0);});}
export class UploadPreparation {
 constructor({signal}={}){this.worker=null;this.requests=new Map();this.nextId=0;this.digest=null;this.signal=signal;this.abort=()=>this.close(signal.reason||new DOMException('Upload preparation cancelled.','AbortError'));if(!signal?.aborted)signal?.addEventListener('abort',this.abort,{once:true});}
 check(){this.signal?.throwIfAborted();}
 async start(){
  this.check();if(typeof Worker==='undefined')return this;
  try{
   this.worker=new Worker(new URL('./upload-worker.js?v=20261020-upload-01',import.meta.url),{type:'module'});
   this.worker.onmessage=({data:{id,value,error}})=>{const request=this.requests.get(id);if(!request)return;this.requests.delete(id);error?request.reject(Error(error)):request.resolve(value);};
   this.worker.onerror=event=>{event.preventDefault();for(const request of this.requests.values())request.reject(Error(event.message||'Upload worker failed.'));this.requests.clear();};
   await this.request('ready');
  }catch{this.close();this.check();} // CSP or older browsers use the same bounded encoder below.
  return this;
 }
 request(type,fields={},transfer=[]){this.check();return new Promise((resolve,reject)=>{const id=++this.nextId;this.requests.set(id,{resolve,reject});try{this.worker.postMessage({id,type,...fields},transfer);}catch(error){this.requests.delete(id);reject(error);}});}
 async encode(left,right,frames,onProgress){
  this.check();const blocks=Math.ceil(frames/140),data=new Uint8Array(blocks*512),batch=256;
  for(let block=0;block<blocks;block+=batch){
   this.check();const count=Math.min(batch,blocks-block),start=block*140,end=Math.min(frames,(block+count)*140);
   if(this.worker){const l=left.slice(start,end),r=right.slice(start,end),encoded=await this.request('encode',{left:l,right:r,blocks:count},[l.buffer,r.buffer]);data.set(encoded,block*512);}
   else {for(let b=block;b<block+count;b++)data.set(encodeP14S(left,right,b*140),b*512);await yieldUpload();}
   this.check();onProgress((block+count)/blocks);
  }
  return {frames,blocks,data};
 }
 async signature(stems,onProgress=()=>{}){
  this.check();if(this.worker)await this.request('hash-start');else this.digest=new UploadSha256();
  const total=stems.reduce((n,s)=>n+s.data.length+4,0);let done=0;
  const add=async data=>{this.check();if(this.worker){const chunk=data.slice();await this.request('hash-chunk',{chunk},[chunk.buffer]);}else {this.digest.update(data);await yieldUpload();}done+=data.length;onProgress(done/total);};
  for(const stem of stems){const frames=new Uint8Array(4);new DataView(frames.buffer).setUint32(0,stem.frames,true);await add(frames);for(let at=0;at<stem.data.length;at+=131072)await add(stem.data.subarray(at,at+131072));}
  return this.worker?this.request('hash-end'):this.digest.hex();
 }
 close(error=new Error('Upload preparation stopped.')){this.signal?.removeEventListener('abort',this.abort);this.worker?.terminate();this.worker=null;for(const request of this.requests.values())request.reject(error);this.requests.clear();this.digest=null;}
}
