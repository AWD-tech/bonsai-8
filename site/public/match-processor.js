import {preparationBudget} from './match-dsp.js';
const aborted=()=>Object.assign(new Error('Matching cancelled. Original audio is unchanged.'),{name:'AbortError'});
export class MatchProcessor {
 constructor({workerFactory=()=>new Worker(new URL('./match-worker.js',import.meta.url),{type:'module'})}={}){this.workerFactory=workerFactory;this.busy=false;this.cancelled=false;this.sequence=0;this.pending=null;}
 cancel(){this.cancelled=true;this.pending?.reject(aborted());this.pending=null;this.worker?.terminate();this.worker=null;}
 begin(){if(this.busy)throw Error('Finish or cancel the current matching operation first.');this.busy=true;this.cancelled=false;}
 check(){if(this.cancelled)throw aborted();}
 async run(type,payload,transfer=[],onProgress=()=>{}){
  this.check();let worker;
  try{return await new Promise((resolve,reject)=>{const id=++this.sequence;worker=this.workerFactory();this.worker=worker;this.pending={reject};worker.onmessage=({data})=>{if(data.id!==id)return;if(data.error)reject(Error(data.error));else if(data.result)resolve(data.result);else if(Number.isFinite(data.progress))onProgress(data.progress);};worker.onerror=()=>reject(Error('The matching worker could not run. Reload the site and try again.'));worker.postMessage({id,type,payload},transfer);});}
  finally{if(this.worker===worker){worker?.terminate();this.worker=null;}this.pending=null;}
 }
 async analysisMix(buffers,indices,sampleRate,length){
  const result=new Float32Array(length),available=indices.filter(i=>buffers[i]);
  for(const index of available){this.check();const b=buffers[index];let selected=0,energy=-1;for(let c=0;c<b.numberOfChannels;c++){const data=b.getChannelData(c);let sum=0;for(let i=0;i<data.length;i+=127)sum+=data[i]**2;if(sum>energy){energy=sum;selected=c;}}const source=b.getChannelData(selected),ratio=b.sampleRate/sampleRate;
   for(let i=0;i<length;i++){const start=Math.floor(i*ratio),end=Math.min(source.length,Math.max(start+1,Math.floor((i+1)*ratio)));let value=0;for(let j=start;j<end;j++)value+=source[j];if(end>start)result[i]+=value/(end-start)/available.length;if(i&&i%65536===0){await new Promise(r=>setTimeout(r,0));this.check();}}
  }
  return result;
 }
 async analyze(buffers){
  this.begin();try{if(!Array.isArray(buffers)||buffers.length!==4||!buffers.some(Boolean)||buffers.some(b=>b&&(!Number.isFinite(b.duration)||b.duration<=0||!Number.isInteger(b.sampleRate)||b.sampleRate<8000||b.sampleRate>96000||![1,2].includes(b.numberOfChannels))))throw Error('Load valid mono/stereo stems before analyzing.');const duration=Math.min(120,Math.max(...buffers.filter(Boolean).map(b=>b.duration))),sampleRate=11025,length=Math.floor(duration*sampleRate);const mono=await this.analysisMix(buffers,buffers[1]?[1]:[0,1,2,3],sampleRate,length),tonal=[0,2,3].filter(i=>buffers[i]),keyMono=await this.analysisMix(buffers,tonal.length?tonal:[1],sampleRate,length);return await this.run('analyze',{mono,keyMono,sampleRate},[mono.buffer,keyMono.buffer]);}finally{this.busy=false;}
 }
 async prepare(buffers,{tempo=1,pitches=[0,0,0,0],context,onProgress=()=>{}}={}){
  this.begin();try{const budget=preparationBudget(buffers,tempo);if(!context?.createBuffer||!Array.isArray(pitches)||pitches.length!==4||pitches.some(p=>!Number.isFinite(p)||p<-12||p>12))throw Error('Choose four pitches within one octave and a valid audio context.');const result=[];
   for(let index=0;index<4;index++){this.check();const b=buffers[index];if(!b){result.push(null);continue;}const channels=Array.from({length:b.numberOfChannels},(_,c)=>b.getChannelData(c).slice()),processed=await this.run('render',{channels,sampleRate:b.sampleRate,tempo,semitones:pitches[index]},channels.map(c=>c.buffer),p=>onProgress((index+p)/4));this.check();const out=context.createBuffer(b.numberOfChannels,Math.round(budget.outputDuration*b.sampleRate),b.sampleRate);processed.forEach((channel,c)=>out.copyToChannel(channel,c));result.push(out);}
   onProgress(1);return result;
  }finally{this.busy=false;}
 }
}
