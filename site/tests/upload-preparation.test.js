import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {UploadSha256} from '../public/upload-sha256.js';
import {UploadPreparation} from '../public/upload-preparation.js';
import {prepareStems,estimatePreparationBytes,uploadSignature,encodeP14S,join,u32} from '../public/device-library.js';
const reference=data=>createHash('sha256').update(data).digest('hex');
test('incremental upload hash matches SHA-256 across padding, block and chunk boundaries',()=>{
 for(const length of [0,1,55,56,63,64,65,127,128,129,131071,131072,131073,1048576]){
  const data=Uint8Array.from({length},(_,i)=>(i*71+(i>>3))&255),expected=reference(data);
  for(const stride of [1,63,257,131072]){const hash=new UploadSha256();for(let at=0;at<data.length;at+=stride)hash.update(data.subarray(at,at+stride));assert.equal(hash.hex(),expected,`${length} bytes at stride ${stride}`);assert.throws(()=>hash.update(data),/already complete/);}
 }
});
test('bounded fallback keeps sector packing and existing four-stem upload identities exact',async()=>{
 const frames=140*256+17,left=Float32Array.from({length:frames},(_,i)=>Math.sin(i*.1)*1.1),right=Float32Array.from({length:frames},(_,i)=>Math.cos(i*.3)*.17),progress=[],preparation=new UploadPreparation();
 try{const encoded=await preparation.encode(left,right,frames,p=>progress.push(p));const expected=join(...Array.from({length:encoded.blocks},(_,i)=>encodeP14S(left,right,i*140)));assert.deepEqual(encoded.data,expected);assert.equal(progress.length,2);assert.equal(progress.at(-1),1);
 const stems=[encoded,{frames:0,blocks:0,data:new Uint8Array()},encoded,{frames:0,blocks:0,data:new Uint8Array()}],oldIdentity=reference(join(...stems.map(s=>join(u32(s.frames),s.data))));assert.equal(await uploadSignature(stems),oldIdentity);
 }finally{preparation.close();}
});
test('preparation can cancel between bounded batches without completing another stem',async()=>{
 const controller=new AbortController(),preparation=new UploadPreparation({signal:controller.signal}),frames=140*512,left=new Float32Array(frames);let calls=0;
 try{await assert.rejects(preparation.encode(left,left,frames,()=>{calls++;controller.abort();}),{name:'AbortError'});assert.equal(calls,1);}finally{preparation.close();}
 const already=new AbortController();already.abort();await assert.rejects(prepareStems([{duration:1}],()=>{},{signal:already.signal}),{name:'AbortError'});
});
test('oversized later stems fail before allocating an offline render',async()=>{
 await assert.rejects(prepareStems([{duration:1},null,{duration:600}]),/eight minutes/);
});

test('preparation memory reservation includes all encoded stems and the largest native render',()=>{
 const seconds=480,frames=seconds*24000,b={duration:seconds},encoded=Math.ceil(frames/140)*512;
 assert.equal(estimatePreparationBytes([b,b,b,b]),4*encoded+frames*8+4*1024*1024);
 assert.equal(estimatePreparationBytes([null,b]),encoded+frames*8+4*1024*1024);
 assert.throws(()=>estimatePreparationBytes([{duration:600}]),/eight minutes/);
});
test('blocked worker startup falls back cleanly and runtime failures reject instead of hanging',async()=>{
 const original=globalThis.Worker;let terminated=0;
 try{
  globalThis.Worker=class {postMessage(){queueMicrotask(()=>this.onerror({preventDefault(){},message:'Blocked by CSP'}));}terminate(){terminated++;}};
  const fallback=await new UploadPreparation().start();assert.equal(fallback.worker,null);assert.equal(await fallback.signature([{frames:0,data:new Uint8Array()}]),reference(new Uint8Array(4)));fallback.close();assert.equal(terminated,1);
  globalThis.Worker=class {postMessage({id,type}){queueMicrotask(()=>type==='ready'?this.onmessage({data:{id,value:true}}):this.onerror({preventDefault(){},message:'Worker stopped'}));}terminate(){terminated++;}};
  const failed=await new UploadPreparation().start();try{await assert.rejects(failed.signature([{frames:0,data:new Uint8Array()}]),/Worker stopped/);}finally{failed.close();}assert.equal(terminated,2);
 }finally{if(original===undefined)delete globalThis.Worker;else globalThis.Worker=original;}
});
