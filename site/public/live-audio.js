// Only the named SP-1 USB input is connected to the analyser or speakers.
const isSP1=label=>/(?:sp[–— _-]?1.*dual[ _-]?deck|bonsai[ _-]?8)/i.test(label||'');
const audioConstraints=id=>({audio:{...(id?{deviceId:{exact:id}}:{}),channelCount:2,sampleRate:48000,echoCancellation:false,noiseSuppression:false,autoGainControl:false},video:false});
export class LiveSP1Audio {
 constructor(onEnded=()=>{}){this.onEnded=onEnded;this.stream=null;this.context=null;this.busy=false;}
 async start(){
  if(this.busy||this.stream)return;
  if(!navigator.mediaDevices?.getUserMedia)throw new Error('USB audio needs a browser with microphone access.');
  this.busy=true;let stream,context;
  try{
   let devices=await navigator.mediaDevices.enumerateDevices();
   let input=devices.find(d=>d.kind==='audioinput'&&isSP1(d.label));
   // Browsers hide names until audio permission is granted. The first stream
   // is inspected only: never connect an unidentified/default mic to audio.
   stream=await navigator.mediaDevices.getUserMedia(audioConstraints(input?.deviceId));
   if(!stream.getAudioTracks().every(t=>isSP1(t.label))||!stream.getAudioTracks().length){
    stream.getTracks().forEach(t=>t.stop());stream=null;
    devices=await navigator.mediaDevices.enumerateDevices();input=devices.find(d=>d.kind==='audioinput'&&isSP1(d.label));
    if(!input)throw new Error('No SP-1 USB audio input found. Reconnect the powered-on player. If it remains missing, the audio firmware needs checking. The computer microphone was not used for playback.');
    stream=await navigator.mediaDevices.getUserMedia(audioConstraints(input.deviceId));
   }
   if(!stream.getAudioTracks().length||!stream.getAudioTracks().every(t=>isSP1(t.label)))throw new Error('Select the Bonsai 8 or SP-1 Dual Deck audio input in your browser permissions.');
   context=new AudioContext({sampleRate:48000});await context.resume();
   const source=context.createMediaStreamSource(stream),analyser=context.createAnalyser(),gain=context.createGain();
   analyser.fftSize=2048;gain.gain.value=.75;source.connect(analyser);analyser.connect(gain);gain.connect(context.destination);
   Object.assign(this,{stream,context,source,analyser,gain,data:new Uint8Array(analyser.fftSize)});
   stream.getAudioTracks().forEach(t=>t.addEventListener('ended',()=>{this.stop();this.onEnded();},{once:true}));
  }catch(error){stream?.getTracks().forEach(t=>t.stop());await context?.close();throw error;}
  finally{this.busy=false;}
 }
 draw(canvas){
  if(!this.analyser)return;
  const ctx=canvas.getContext('2d');this.analyser.getByteTimeDomainData(this.data);
  ctx.clearRect(0,0,canvas.width,canvas.height);ctx.strokeStyle='#757b70';ctx.lineWidth=1.5;ctx.beginPath();
  for(let x=0;x<canvas.width;x++){const value=this.data[Math.floor(x/ canvas.width*this.data.length)];const y=value/255*canvas.height;x?ctx.lineTo(x,y):ctx.moveTo(x,y);}ctx.stroke();
 }
 stop(){
  this.stream?.getTracks().forEach(t=>t.stop());this.source?.disconnect();this.gain?.disconnect();
  this.context?.close().catch(()=>{});this.stream=this.context=this.source=this.analyser=this.gain=null;
 }
}
export function deviceProgress(state,deck){
 const length=state?.length?.[deck],position=state?.position?.[deck];
 if(!Number.isInteger(length)||length<0||!Number.isInteger(position)||position<0||position>length)return null;
 return {seconds:position/24000,duration:length/24000,fraction:length?position/length:0};
}
