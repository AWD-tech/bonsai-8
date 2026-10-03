// Only the named SP-1 USB input is connected to the analyser or speakers.
const isSP1=label=>/(?:sp[–— _-]?1.*dual[ _-]?deck|bonsai[ _-]?8)/i.test(label||'');
const audioConstraints=id=>({audio:{...(id?{deviceId:{exact:id}}:{}),channelCount:2,sampleRate:48000,echoCancellation:false,noiseSuppression:false,autoGainControl:false},video:false});
const cancelled=()=>new DOMException('Listening was cancelled.','AbortError');
const isAlias=id=>id==='default'||id==='communications';
function findInput(devices){
 const named=devices.filter(device=>device.kind==='audioinput'&&isSP1(device.label));
 return named.find(device=>device.deviceId&&!isAlias(device.deviceId))||named[0];
}
function stopTracks(resources){
 const stream=resources.stream;resources.stream=null;
 for(const track of stream?.getTracks()||[]){try{track.stop();}catch{}}
}
function release(resources){
 if(!resources)return Promise.resolve();
 stopTracks(resources);
 for(const key of ['source','analyser','gain']){
  const node=resources[key];resources[key]=null;
  try{node?.disconnect();}catch{}
 }
 const context=resources.context;resources.context=null;
 if(!context||context.state==='closed')return Promise.resolve();
 try{return Promise.resolve(context.close()).catch(()=>{});}catch{return Promise.resolve();}
}
function namedLiveInput(stream,id){
 const tracks=stream?.getAudioTracks()||[];
 return tracks.length>0&&tracks.every(track=>{
  const actualId=track.getSettings?.()?.deviceId;
  return isSP1(track.label)&&track.readyState!=='ended'&&(!id||isAlias(id)||!actualId||actualId===id);
 });
}
export class LiveSP1Audio {
 constructor(onEnded=()=>{}){
  this.onEnded=onEnded;this.stream=null;this.context=null;this.busy=false;
  this.generation=0;this.pending=null;this.active=null;
 }
 async start(){
  if(this.busy||this.stream)return;
  if(!navigator.mediaDevices?.getUserMedia)throw new Error('USB audio needs a browser with microphone access.');
  const token=++this.generation,attempt={stream:null,context:null};
  const current=()=>{if(token!==this.generation)throw cancelled();};
  this.pending=attempt;this.busy=true;
  try{
   // Begin output during the click, before device/permission awaits can expire
   // transient user activation. Absorb a late resume rejection immediately.
   const context=new AudioContext({sampleRate:48000});attempt.context=context;
   const resumed=Promise.resolve(context.resume()).then(()=>({ok:true}),error=>({ok:false,error}));
   let devices=await navigator.mediaDevices.enumerateDevices();current();
   let input=findInput(devices);
   attempt.stream=await navigator.mediaDevices.getUserMedia(audioConstraints(input?.deviceId));current();
   // Browsers hide names until audio permission is granted. A default stream
   // is inspected only, never connected to the output or analyser.
   if(!namedLiveInput(attempt.stream,input?.deviceId)){
    stopTracks(attempt);
    devices=await navigator.mediaDevices.enumerateDevices();current();
    input=findInput(devices);
    if(!input)throw new Error('No SP-1 USB audio input found. Reconnect the powered-on player. If it remains missing, the audio firmware needs checking. The computer microphone was not used for playback.');
    attempt.stream=await navigator.mediaDevices.getUserMedia(audioConstraints(input.deviceId));current();
   }
   if(!namedLiveInput(attempt.stream,input?.deviceId))throw new Error('Select the Bonsai 8 or SP-1 Dual Deck audio input in your browser permissions.');
   const resumeResult=await resumed;current();
   if(!resumeResult.ok)throw resumeResult.error;
   if(context.state&&context.state!=='running')throw new Error('Audio output is paused by the browser. Choose Listen here again.');
   if(!namedLiveInput(attempt.stream,input?.deviceId))throw new Error('SP-1 USB audio disconnected while starting. Reconnect the player and try again.');
   const stream=attempt.stream,source=context.createMediaStreamSource(stream),analyser=context.createAnalyser(),gain=context.createGain();
   Object.assign(attempt,{source,analyser,gain});
   analyser.fftSize=2048;gain.gain.value=.75;source.connect(analyser);analyser.connect(gain);gain.connect(context.destination);
   current();this.active=attempt;
   Object.assign(this,{stream,context,source,analyser,gain,data:new Uint8Array(analyser.fftSize)});
   stream.getAudioTracks().forEach(track=>track.addEventListener('ended',()=>{
    if(this.generation!==token||this.active!==attempt)return;
    this.stop();this.onEnded();
   },{once:true}));
  }catch(error){
   await release(attempt);
   throw token!==this.generation?cancelled():error;
  }finally{
   if(token===this.generation){this.busy=false;this.pending=null;}
  }
 }
 draw(canvas){
  if(!this.analyser)return;
  const ctx=canvas.getContext('2d');this.analyser.getByteTimeDomainData(this.data);
  ctx.clearRect(0,0,canvas.width,canvas.height);ctx.strokeStyle='#757b70';ctx.lineWidth=1.5;ctx.beginPath();
  for(let x=0;x<canvas.width;x++){const value=this.data[Math.floor(x/canvas.width*this.data.length)];const y=value/255*canvas.height;x?ctx.lineTo(x,y):ctx.moveTo(x,y);}ctx.stroke();
 }
 stop(){
  const active=this.active,pending=this.pending;
  ++this.generation;this.busy=false;this.active=this.pending=null;
  this.stream=this.context=this.source=this.analyser=this.gain=this.data=null;
  void release(active);if(pending!==active)void release(pending);
 }
}
export function deviceProgress(state,deck){
 const length=state?.length?.[deck],position=state?.position?.[deck];
 if(!Number.isInteger(length)||length<0||!Number.isInteger(position)||position<0||position>length)return null;
 return {seconds:position/24000,duration:length/24000,fraction:length?position/length:0};
}
