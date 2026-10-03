import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
import * as core from '../public/core.js';
import * as live from '../public/live-audio.js';
import * as mirror from '../public/mirror.js';
import * as heights from '../public/control-height.js';
import * as protocol from '../public/protocol.js';
import {AudioEngine} from '../public/audio.js';

// Run the real application callback/frame loop with rendering and DOM at their boundary.
function appHarness({LiveAudio=live.LiveSP1Audio}={}){
 const elements=new Map();let device,connection,audio,monitor,libraryOptions,now=100;
 const node=()=>({value:0,hidden:false,classList:{toggle(){},add(){},remove(){}},setAttribute(){},addEventListener(){},getContext(){return {clearRect(){},beginPath(){},moveTo(){},lineTo(){},stroke(){}}}});
 const document={getElementById(id){if(!elements.has(id))elements.set(id,node());return elements.get(id);},querySelectorAll(){return [];},querySelector(){return null;},addEventListener(){}};

 class Device {constructor(_,events){device=this;this.events=events;this.targets=[];this.buttons={};}setGain(i,v){if(!this.hardwareMode)this.targets[i]=v;}setControlHeight(){}setBodyHeight(){}setButton(k,v){this.buttons[k]=v;}lights(values){this.leds=[...values];}setHardwareFrame(f){this.hardwareFrame=f;this.targets=f.faders;this.buttons=f.buttons;this.leds=f.trackLeds;this.statusLeds=f.statusLeds;}setHardwareMode(on){this.hardwareMode=on;}}
 class Connection{constructor(onControls,onStatus,onMirror){connection=this;this.onControls=onControls;this.onStatus=onStatus;this.onMirror=onMirror;}}
 class Monitor extends LiveAudio {constructor(...args){super(...args);monitor=this;}}
 const context=vm.createContext({...core,...protocol,...live,...mirror,...heights,LiveSP1Audio:Monitor,setupMatching:()=>({refresh(){},busy:false}),setupBrowserMixer:options=>{audio=options.audio;return {sync(){},connected(on){if(on)audio.pauseAll();},select(index){audio.select(index);options.onSelect();}};},setupLibrary:options=>{libraryOptions=options;return {state(){},telemetry(){},refresh(){},controls(){}};},localStorage:{getItem(){return null;},setItem(){}},document,AudioEngine,Device,SP1Connection:Connection,navigator:{serial:{addEventListener(){}}},performance:{now:()=>now},console,setTimeout,clearTimeout,setInterval,Uint8Array,zip(){}});
 vm.runInContext(readFileSync(new URL('../public/app.js',import.meta.url),'utf8').replace(/^import .*;\n/gm,''),context);
 return {device,connection,audio,monitor,libraryOptions,frame(ms=100){now+=ms;device.events.frame();},elements};
}
const state={firmware:'sp1-dual-deck-0.3',deck:0,master:64,gains:[256,256,256,256,0,0,0,0],mute:[1,0],playing:[1,0],speed:[65536,65536],slots:[1,2],mirror:1};
// A physical PLAY tap has already released while the song continues playing.
const ui={seq:1,ms:1000,buttons:{play:false,function:true,track0:false,track1:false,track2:false,track3:false,forward:false,rewind:false,volumeUp:false,volumeDown:false},faders:[.1,.2,.3,.4],trackLeds:[1,0,1,0],statusLeds:[0,1,0,1]};
test('physical release, fader pickup and both LED rows survive the application render loop',async()=>{
 const app=appHarness();await app.connection.onControls(null,null,state);app.connection.onMirror({mirror:1,now:1000,lost:0,frames:[ui]});app.frame();
 assert.equal(app.device.buttons.play,false,'PLAY must release even while the song keeps playing');
 assert.equal(app.device.buttons.function,true,'FUNCTION must mirror its physical held state');
 assert.deepEqual([...app.device.targets],ui.faders,'physical fader positions differ from deck gain during pickup');
 assert.deepEqual(app.device.leds,ui.trackLeds,'browser audio meters must not overwrite hardware track lights');
 assert.deepEqual(app.device.statusLeds,ui.statusLeds,'all four side lights must mirror hardware');
});

test('USB audio errors remain visible while hardware status continues polling',async()=>{
 const app=appHarness();const s={...state,usb_capture:1};await app.connection.onControls(null,null,s);
 await app.elements.get('listen-sp1').onclick();
 const error=app.elements.get('live-audio-status').textContent;
 assert.match(error,/USB audio needs/);
 await app.connection.onControls(null,null,s);
 assert.equal(app.elements.get('live-audio-status').textContent,error,'status polls must preserve audio failure details');
});

test('physical monitoring pauses both local decks without replacing their saved gains, speed or buffers',async()=>{
 const app=appHarness(),a=app.audio;
 a.decks[0].gains=[.2,.3,.4,.5];a.decks[1].gains=[.8,.7,.6,.5];
 a.decks[0].speed=1.2;a.decks[1].speed=.9;
 const first={duration:10,getChannelData:()=>new Float32Array(10)};a.decks[0].buffers=[first];a.decks[0].media.name='Local song';
 app.connection.onStatus('Connected',true);
 await app.connection.onControls(null,null,{...state,master:12,gains:[256,256,256,256,256,256,256,256],speed:[65536,81920]});
 assert.deepEqual(a.decks[0].gains,[.2,.3,.4,.5]);assert.deepEqual(a.decks[1].gains,[.8,.7,.6,.5]);
 assert.equal(a.decks[0].speed,1.2);assert.equal(a.decks[1].speed,.9);assert.equal(a.decks[0].buffers[0],first);
 app.connection.onStatus('Disconnected',false);
 assert.equal(app.elements.get('song-name').textContent,'Local song');
 assert.equal(a.decks[0].playing,false);assert.equal(a.decks[1].playing,false);
});

// Control only the monitor boundary. The real application's asynchronous handler,
// connection callbacks and DOM updates run unchanged inside the VM.
class DeferredMonitor {
 constructor(onEnded){this.onEnded=onEnded;this.requests=[];this.stream=null;this.busy=false;this.current=null;}
 start(){
  if(this.busy||this.stream)return;
  this.busy=true;
  const request={};this.current=request;
  const pending=new Promise((resolve,reject)=>{request.resolve=()=>{if(this.current===request)this.stream={};resolve();};request.reject=reject;});
  this.requests.push(request);
  return pending.finally(()=>{if(this.current===request)this.busy=false;});
 }
 stop(){this.current=null;this.busy=false;this.stream=null;}
 draw(){}
}
async function connectedMonitor(){
 const app=appHarness({LiveAudio:DeferredMonitor});
 app.connection.onStatus('Connected',true);
 await app.connection.onControls(null,null,{...state,usb_capture:1});
 return app;
}
const abort=()=>Object.assign(new Error('Listening was cancelled.'),{name:'AbortError'});

test('disconnect during pending permission does not display a cancellation error or resurrect monitor UI',async()=>{
 const app=await connectedMonitor(),button=app.elements.get('listen-sp1'),status=app.elements.get('live-audio-status');
 const started=button.onclick();
 app.connection.onStatus('Disconnected',false);
 const disconnectedStatus=status.textContent;
 app.monitor.requests[0].reject(abort());await started;
 assert.equal(status.textContent,disconnectedStatus);
 assert.equal(app.elements.get('toast')?.textContent,undefined);
 assert.equal(app.elements.get('live-player').hidden,true);
 assert.equal(button.textContent,'Listen here');assert.equal(button.disabled,true);
});

test('a canceled earlier start cannot enable Listen or overwrite a newer pending session',async()=>{
 const app=await connectedMonitor(),button=app.elements.get('listen-sp1'),status=app.elements.get('live-audio-status');
 const earlier=button.onclick();app.connection.onStatus('Disconnected',false);
 app.connection.onStatus('Connected',true);await app.connection.onControls(null,null,{...state,usb_capture:1});
 const newer=button.onclick(),pendingStatus=status.textContent;
 app.monitor.requests[0].reject(abort());await earlier;
 assert.equal(app.monitor.busy,true);assert.equal(button.disabled,true,'newer start still awaits permission/resume');
 assert.equal(status.textContent,pendingStatus);assert.equal(app.elements.get('toast')?.textContent,undefined);
 app.monitor.requests[1].resolve();await newer;
 assert.equal(button.textContent,'Stop listening');assert.equal(button.disabled,false);
 assert.match(status.textContent,/Live stereo mix/);
});

test('late failure from an earlier start cannot overwrite a newer active session',async()=>{
 const app=await connectedMonitor(),button=app.elements.get('listen-sp1'),status=app.elements.get('live-audio-status');
 const earlier=button.onclick();app.connection.onStatus('Disconnected',false);
 app.connection.onStatus('Connected',true);await app.connection.onControls(null,null,{...state,usb_capture:1});
 const newer=button.onclick();app.monitor.requests[1].resolve();await newer;
 const activeStatus=status.textContent;
 app.monitor.requests[0].reject(new Error('Earlier permission failed'));await earlier;
 assert.equal(status.textContent,activeStatus);assert.equal(button.textContent,'Stop listening');
 assert.equal(button.disabled,false);assert.equal(app.elements.get('toast')?.textContent,undefined);
});

test('repeated Listen invocation while starting cannot claim monitoring has already started',async()=>{
 const app=await connectedMonitor(),button=app.elements.get('listen-sp1'),status=app.elements.get('live-audio-status');
 const first=button.onclick(),pendingStatus=status.textContent;
 await button.onclick();
 assert.equal(app.monitor.requests.length,1);assert.equal(button.disabled,true);
 assert.equal(status.textContent,pendingStatus);assert.notEqual(button.textContent,'Stop listening');
 app.monitor.requests[0].resolve();await first;
 assert.equal(button.textContent,'Stop listening');
});

test('library operation cancels pending listening without a late error toast',async()=>{
 const app=await connectedMonitor(),button=app.elements.get('listen-sp1'),status=app.elements.get('live-audio-status');
 const first=button.onclick();app.libraryOptions.stopMonitor();
 const stoppedStatus=status.textContent;
 app.monitor.requests[0].reject(abort());await first;
 assert.equal(status.textContent,stoppedStatus);assert.equal(button.textContent,'Listen here');
 assert.equal(app.elements.get('toast')?.textContent,undefined);
});

test('AbortError from the current monitor start remains quiet and makes Listen available again',async()=>{
 const app=await connectedMonitor(),button=app.elements.get('listen-sp1'),status=app.elements.get('live-audio-status');
 const started=button.onclick(),pendingStatus=status.textContent;
 app.monitor.requests[0].reject(abort());await started;
 assert.equal(status.textContent,pendingStatus);assert.equal(app.elements.get('toast')?.textContent,undefined);
 assert.equal(button.disabled,false);assert.equal(app.monitor.stream,null);
});
