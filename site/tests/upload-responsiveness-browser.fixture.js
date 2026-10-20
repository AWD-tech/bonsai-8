import {prepareStems,DeviceLibrary,encodeP14S,sha256,join,u32} from './device-library.js';
import {yieldUpload} from './upload-preparation.js';
export async function run({seconds=120,withoutWorker=false}={}){
 const worker=globalThis.Worker;if(withoutWorker)globalThis.Worker=undefined;
 try{
  const b=new AudioBuffer({length:24000*seconds+13,numberOfChannels:2,sampleRate:24000});
  b.getChannelData(0).fill(.25);b.getChannelData(1).fill(-.125);
  await yieldUpload();
  const longTasks=[],observer=new PerformanceObserver(list=>longTasks.push(...list.getEntries().map(e=>e.duration)));
  observer.observe({type:'longtask',buffered:false});
  const progress=[],states=new Set(),start=performance.now(),heapBefore=performance.memory?.usedJSHeapSize;
  const stems=await prepareStems([b,b,b,b],p=>{progress.push({p,t:performance.now()-start});states.add(document.visibilityState);});
  const prepareEnd=performance.now(),heapPrepared=performance.memory?.usedJSHeapSize;
  let reachedTransfer=false;
  // The transport boundary is reached after the real upload identity is ready.
  // No serial permission, device connection, or storage write is performed.
  try{await new DeviceLibrary({withTransfer(){reachedTransfer=true;throw Error('test transport boundary');}}).upload(1,stems);}catch(e){if(e.message!=='test transport boundary')throw e;}
  const hashEnd=performance.now(),heapAfter=performance.memory?.usedJSHeapSize;
  await yieldUpload();await new Promise(resolve=>setTimeout(resolve,100));observer.disconnect();
  // Validate real Worker bytes across first/batch/end boundaries. Native digest
  // parity is checked after the measured region because it deliberately uses the
  // old full-library concatenation as the independent compatibility reference.
  for(const stem of stems)for(const block of [0,255,256,Math.floor(stem.blocks/2),stem.blocks-1]){
   const expected=encodeP14S(b.getChannelData(0),b.getChannelData(1),block*140),actual=stem.data.subarray(block*512,(block+1)*512);
   if(actual.length!==512||actual.some((v,i)=>v!==expected[i]))throw Error(`Sector packing changed at ${block}`);
  }
  const {uploadSignature}=await import('./device-library.js');
  const expected=await sha256(join(...stems.map(s=>join(u32(s.frames),s.data))));
  const signature=await uploadSignature(stems);
  if(signature!==expected)throw Error('Upload identity differs from existing checkpoint format.');
  return {seconds,withoutWorker,visibility:[...states],elapsedMs:hashEnd-start,prepareMs:prepareEnd-start,hashMs:hashEnd-prepareEnd,heapBefore,heapPrepared,heapAfter,encodedBytes:stems.reduce((n,s)=>n+s.data.byteLength,0),longTasks,progressUpdates:progress.length,maxProgressGapMs:Math.max(...progress.map((p,i)=>p.t-(progress[i-1]?.t||0))),reachedTransfer,exactP14SAndIdentity:true};
 }finally{globalThis.Worker=worker;}
}
