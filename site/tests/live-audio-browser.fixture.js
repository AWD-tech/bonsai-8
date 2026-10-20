import {LiveSP1Audio} from './public/live-audio.js';

const check=(condition,message)=>{if(!condition)throw new Error(message);};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function deferred(){let resolve;const promise=new Promise(yes=>{resolve=yes;});return {promise,resolve};}
async function until(predicate,message){
 const deadline=performance.now()+4000;
 while(performance.now()<deadline){if(predicate())return;await delay(25);}
 throw new Error(message);
}
function rms(analyser){
 const values=new Float32Array(analyser.fftSize);analyser.getFloatTimeDomainData(values);
 return Math.sqrt(values.reduce((sum,value)=>sum+value*value,0)/values.length);
}
function near(value,expected,tolerance=.015){return Math.abs(value-expected)<=tolerance;}

export async function run(){
 window.validationProgress='resuming native signal generator';
 const NativeContext=window.AudioContext,connect=AudioNode.prototype.connect,disconnect=AudioNode.prototype.disconnect;
 const contexts=[],sourceStreams=[],edges=[],disconnected=new Set(),monitors=[],signals=[],checks=[];
 const previousMedia=Object.getOwnPropertyDescriptor(navigator,'mediaDevices');
 // Every node and clock is native. Instrumentation forwards to the native methods.
 window.AudioContext=class extends NativeContext {
  constructor(options){super(options);contexts.push(this);}
  createMediaStreamSource(stream){sourceStreams.push(stream);return super.createMediaStreamSource(stream);}
 };
 AudioNode.prototype.connect=function(destination,...args){const result=connect.call(this,destination,...args);edges.push([this,destination]);return result;};
 AudioNode.prototype.disconnect=function(...args){const result=disconnect.apply(this,args);disconnected.add(this);return result;};
 const generator=new NativeContext({sampleRate:48000});
 await generator.resume();
 const adapter={devices:[],calls:[],async enumerateDevices(){return this.devices;},async getUserMedia(constraints){this.calls.push(constraints);return this.acquire(constraints);}};
 Object.defineProperty(navigator,'mediaDevices',{configurable:true,value:adapter});
 const physical={kind:'audioinput',label:'Bonsai 8',deviceId:'synthetic-bonsai'};
 function signal({label='Bonsai 8',deviceId='synthetic-bonsai'}={}){
  const merger=generator.createChannelMerger(2),destination=generator.createMediaStreamDestination(),gains=[],oscillators=[];
  [997,1499].forEach((frequency,index)=>{
   const oscillator=generator.createOscillator(),gain=generator.createGain();
   oscillator.frequency.value=frequency;gain.gain.value=index?.2:.4;
   oscillator.connect(gain);gain.connect(merger,0,index);oscillator.start();gains.push(gain);oscillators.push(oscillator);
  });
  merger.connect(destination);
  const stream=destination.stream,track=stream.getAudioTracks()[0],nativeSettings=track.getSettings.bind(track);
  // A real generated track has no hardware name/id. Only these metadata fields are fixtures.
  Object.defineProperty(track,'label',{configurable:true,value:label});
  track.getSettings=()=>({...nativeSettings(),deviceId});
  const value={stream,track,gains,oscillators};signals.push(value);return value;
 }
 function monitor(onEnded){const value=new LiveSP1Audio(onEnded);monitors.push(value);return value;}
 function configure(acquire,devices=[physical]){adapter.acquire=acquire;adapter.devices=devices;adapter.calls=[];}
 function tap(audio){
  const splitter=audio.context.createChannelSplitter(2),silence=audio.context.createGain();silence.gain.value=0;
  const analysers=[0,1].map(index=>{const analyser=audio.context.createAnalyser();analyser.fftSize=2048;splitter.connect(analyser,index);analyser.connect(silence);return analyser;});
  audio.gain.connect(splitter);silence.connect(audio.context.destination);
  return Object.assign(()=>analysers.map(rms),{analysers});
 }
 async function closed(context,message='Monitor AudioContext was not closed'){await until(()=>context.state==='closed',message);}
 function released(audio){check(['stream','context','source','analyser','gain','data','active','pending'].every(key=>audio[key]===null),'Monitor retained active resources');check(audio.busy===false,'Monitor remained busy');}
 async function record(name,runCheck){
  window.validationProgress=name;
  const started=performance.now();const details=await runCheck();checks.push({name,pass:true,milliseconds:Math.round(performance.now()-started),...details});
 }
 let result;
 try{
  await record('native stereo graph, waveform, input mute/unmute, and stop',async()=>{
   const input=signal(),audio=monitor();configure(async()=>input.stream,[{...physical,label:'Default - Bonsai 8',deviceId:'default'},physical]);
   await audio.start();
   check(audio.context.state==='running'&&audio.context.sampleRate===48000,'Monitor did not run at 48 kHz');
   check(audio.source instanceof MediaStreamAudioSourceNode&&audio.analyser instanceof AnalyserNode&&audio.gain instanceof GainNode,'Graph did not use native Web Audio nodes');
   check(edges.some(([from,to])=>from===audio.source&&to===audio.analyser)&&edges.some(([from,to])=>from===audio.analyser&&to===audio.gain)&&edges.some(([from,to])=>from===audio.gain&&to===audio.context.destination),'Production graph was not connected through to the native destination');
   const request=adapter.calls[0];
   check(adapter.calls.length===1&&request.audio.deviceId.exact==='synthetic-bonsai','Physical named input was not preferred over default alias');
   check(request.audio.channelCount===2&&request.audio.sampleRate===48000&&!request.audio.echoCancellation&&!request.audio.noiseSuppression&&!request.audio.autoGainControl&&request.video===false,'Capture constraints changed');
   const levels=tap(audio),expected=[.4*.75/Math.sqrt(2),.2*.75/Math.sqrt(2)];
   await until(()=>levels().every((level,index)=>near(level,expected[index])),'Native stereo PCM did not reach the post-gain output tap');
   const audibleLevels=levels(),inputRms=rms(audio.analyser);
   check(inputRms>.1,'Production analyser was silent');
   const canvas=document.querySelector('#wave');audio.draw(canvas);
   check(canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data.some(value=>value!==0),'Waveform did not draw');
   input.gains.forEach(gain=>{gain.gain.value=0;});
   await until(()=>levels().every(level=>level<1e-6)&&rms(audio.analyser)<1e-6,'Muted synthetic input did not become silent through monitor');
   const mutedLevels=levels();input.gains.forEach((gain,index)=>{gain.gain.value=index?.2:.4;});
   await until(()=>levels().every((level,index)=>near(level,expected[index])),'Synthetic input did not recover after unmute');
   const context=audio.context,nodes=[audio.source,audio.analyser,audio.gain];audio.stop();audio.stop();
   await closed(context);released(audio);
   check(input.track.readyState==='ended'&&nodes.every(node=>disconnected.has(node)),'Stop did not end track and disconnect production nodes');
   return {sampleRate:context.sampleRate,expectedOutputRms:expected,measuredOutputRms:audibleLevels,measuredInputRms:inputRms,mutedOutputRms:mutedLevels,trackState:input.track.readyState,contextState:context.state};
  });
  await record('repeated start while pending and active creates one capture/graph',async()=>{
   const input=signal(),gate=deferred(),requested=deferred(),audio=monitor();
   configure(()=>{requested.resolve();return gate.promise;});
   const before=sourceStreams.length,starting=audio.start();await requested.promise;
   await Promise.all(Array.from({length:12},()=>audio.start()));
   check(adapter.calls.length===1&&audio.busy,'Repeated pending start duplicated capture');
   gate.resolve(input.stream);await starting;
   await Promise.all(Array.from({length:12},()=>audio.start()));
   check(adapter.calls.length===1&&sourceStreams.length===before+1,'Repeated active start duplicated capture or graph');
   const levels=tap(audio);await until(()=>levels()[0]>.18,'Repeated-start graph was silent');
   const context=audio.context;audio.stop();await closed(context);released(audio);
   return {repeatedStarts:24,captureRequests:adapter.calls.length,createdGraphs:sourceStreams.length-before};
  });
  await record('60-second native stereo monitor with periodic RMS, frequency, and clock samples',async()=>{
   const input=signal(),audio=monitor();configure(async()=>input.stream);await audio.start();
   const levels=tap(audio),expectedRms=[.4*.75/Math.sqrt(2),.2*.75/Math.sqrt(2)],expectedHz=[997,1499];
   await until(()=>levels().every((level,index)=>near(level,expectedRms[index])),'Sustained monitor did not acquire its stereo signal');
   const rmsRange=[[Infinity,-Infinity],[Infinity,-Infinity]],frequencyRange=[[Infinity,-Infinity],[Infinity,-Infinity]];
   const startWall=performance.now(),startClock=audio.context.currentTime;let lastWall=startWall,lastClock=startClock,samples=0,maxClockStepErrorSeconds=0;
   while(performance.now()-startWall<60000){
    await delay(250);
    const now=performance.now(),clock=audio.context.currentTime,clockStep=clock-lastClock,wallStep=(now-lastWall)/1000;
    maxClockStepErrorSeconds=Math.max(maxClockStepErrorSeconds,Math.abs(clockStep-wallStep));
    check(clockStep>0&&Math.abs(clockStep-wallStep)<.25,'Native audio clock stalled or diverged from elapsed wall time');
    check(audio.context.state==='running'&&input.track.readyState==='live','Sustained context or track stopped');
    for(let channel=0;channel<2;channel++){
     const data=new Float32Array(levels.analysers[channel].fftSize);levels.analysers[channel].getFloatTimeDomainData(data);
     const value=Math.sqrt(data.reduce((sum,sample)=>sum+sample*sample,0)/data.length),crossings=[];
     for(let i=1;i<data.length;i++)if(data[i-1]<=0&&data[i]>0)crossings.push(i-1-data[i-1]/(data[i]-data[i-1]));
     check(crossings.length>2,'Sustained channel lost its tone');
     const hz=(crossings.length-1)*audio.context.sampleRate/(crossings.at(-1)-crossings[0]);
     check(near(value,expectedRms[channel])&&Math.abs(hz-expectedHz[channel])<3,'Sustained channel changed level or frequency');
     rmsRange[channel][0]=Math.min(rmsRange[channel][0],value);rmsRange[channel][1]=Math.max(rmsRange[channel][1],value);
     frequencyRange[channel][0]=Math.min(frequencyRange[channel][0],hz);frequencyRange[channel][1]=Math.max(frequencyRange[channel][1],hz);
    }
    lastWall=now;lastClock=clock;samples++;
   }
   const wallSeconds=(lastWall-startWall)/1000,audioClockSeconds=lastClock-startClock;
   check(samples>=200&&Math.abs(audioClockSeconds-wallSeconds)<.25,'Sustained measurement lacked samples or its clock drifted');
   const context=audio.context;audio.stop();await closed(context);released(audio);
   return {wallSeconds,audioClockSeconds,samples,sampleIntervalMilliseconds:250,analysedFramesPerSample:2048,expectedHz,frequencyRangeHz:frequencyRange,outputRmsRange:rmsRange,maxClockStepErrorSeconds,limitation:'Periodic analyser windows; not continuous dropout detection or physical USB/speaker endurance'};
  });
  await record('cancelled late capture cannot replace or stop a newer real graph',async()=>{
   const stale=signal(),fresh=signal(),gate=deferred(),requested=deferred(),audio=monitor();let count=0;
   configure(()=>++count===1?(requested.resolve(),gate.promise):Promise.resolve(fresh.stream));
   const old=audio.start().then(()=>null,error=>error);await requested.promise;const oldContext=contexts.at(-1);
   audio.stop();await audio.start();const currentContext=audio.context,levels=tap(audio);gate.resolve(stale.stream);
   const error=await old;await closed(oldContext);
   check(error?.name==='AbortError'&&stale.track.readyState==='ended','Cancelled late capture was not rejected and stopped');
   check(audio.stream===fresh.stream&&audio.context===currentContext&&fresh.track.readyState==='live','Late result replaced or stopped newer capture');
   await until(()=>levels()[0]>.18,'Newer graph lost its audio signal');
   const measuredOutputRms=levels();audio.stop();await closed(currentContext);released(audio);
   return {cancelledError:error.name,lateTrackState:stale.track.readyState,measuredOutputRms};
  });
  await record('simulated track disconnect cleans graph once; stale ended event is ignored',async()=>{
   const first=signal(),second=signal();let count=0,ended=0;const audio=monitor(()=>ended++);
   configure(async()=>++count===1?first.stream:second.stream);
   await audio.start();const firstContext=audio.context;audio.stop();await closed(firstContext);await audio.start();
   const context=audio.context,nodes=[audio.source,audio.analyser,audio.gain],levels=tap(audio);
   first.track.dispatchEvent(new Event('ended'));
   check(audio.stream===second.stream&&ended===0,'Stale ended event stopped new monitor');
   await until(()=>levels()[0]>.18,'New graph was silent before disconnect');
   second.track.stop();second.track.dispatchEvent(new Event('ended'));second.track.dispatchEvent(new Event('ended'));
   await closed(context);released(audio);
   check(ended===1&&nodes.every(node=>disconnected.has(node)),'Disconnect failed to release nodes or fired callback more than once');
   return {endedCallbacks:ended,contextState:context.state};
  });
  await record('hidden labels: unidentified probe is stopped before named stream is connected',async()=>{
   const probe=signal({label:'Synthetic computer microphone',deviceId:'synthetic-mic'}),named=signal({label:'SP-1 Dual Deck'}),audio=monitor();let calls=0;
   configure(async()=>++calls===1?probe.stream:named.stream,[]);
   adapter.enumerateDevices=async()=>calls?[physical]:[];
   const before=sourceStreams.length;await audio.start();
   check(probe.track.readyState==='ended'&&adapter.calls.length===2,'Unidentified probe was not stopped before retry');
   check(sourceStreams.length===before+1&&sourceStreams.at(-1)===named.stream,'Unidentified stream entered native graph');
   check(!adapter.calls[0].audio.deviceId&&adapter.calls[1].audio.deviceId.exact==='synthetic-bonsai','Hidden-label retry constraints changed');
   const levels=tap(audio);await until(()=>levels()[0]>.18,'Named retry graph was silent');
   const context=audio.context;audio.stop();await closed(context);
   adapter.enumerateDevices=async()=>adapter.devices;
   return {captureRequests:calls,unidentifiedTrackState:probe.track.readyState,createdGraphs:sourceStreams.length-before};
  });
  for(const scenario of [
   {name:'unidentified microphone with no named input',devices:[],label:'Synthetic computer microphone',id:'synthetic-mic',expected:/No SP-1 USB audio input/,requests:1},
   {name:'named device with mismatched exact identity',devices:[physical],label:'Bonsai 8',id:'wrong-device',expected:/Select the Bonsai 8/,requests:2},
   {name:'named selection returns an unidentified microphone',devices:[physical],label:'Synthetic computer microphone',id:'synthetic-mic',expected:/Select the Bonsai 8/,requests:2},
  ]){
   await record(`reject ${scenario.name} before any graph`,async()=>{
    const received=[],audio=monitor(),before=sourceStreams.length,firstContext=contexts.length;
    configure(async()=>{const input=signal({label:scenario.label,deviceId:scenario.id});received.push(input);return input.stream;},scenario.devices);
    const error=await audio.start().then(()=>null,error=>error);
    check(error&&scenario.expected.test(error.message),'Unexpected named-device rejection');
    check(sourceStreams.length===before,'Rejected input reached a native MediaStreamAudioSourceNode');
    check(received.length===scenario.requests&&received.every(input=>input.track.readyState==='ended'),'Rejected captures were not stopped');
    check(!audio.stream&&!audio.context&&!audio.busy&&!audio.pending,'Rejected monitor retained startup state');
    await Promise.all(contexts.slice(firstContext).map(context=>closed(context)));
    return {error:error.message,captureRequests:received.length,createdGraphs:0};
   });
  }
  result={pass:true,checks};
 }catch(error){result={pass:false,error:error.stack,checks};}
 finally{
  monitors.forEach(audio=>audio.stop());signals.forEach(input=>{input.track.stop();input.oscillators.forEach(oscillator=>oscillator.stop());});
  await Promise.all(contexts.filter(context=>context.state!=='closed').map(context=>context.close()));await generator.close();
  window.AudioContext=NativeContext;AudioNode.prototype.connect=connect;AudioNode.prototype.disconnect=disconnect;
  if(previousMedia)Object.defineProperty(navigator,'mediaDevices',previousMedia);else delete navigator.mediaDevices;
 }
 return {...result,monitorContexts:contexts.length,closedMonitorContexts:contexts.filter(context=>context.state==='closed').length,syntheticTracks:signals.length,endedSyntheticTracks:signals.filter(input=>input.track.readyState==='ended').length};
}
