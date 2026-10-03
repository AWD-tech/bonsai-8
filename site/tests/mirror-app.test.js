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

test('explicit pitch-preserving telemetry shows selected deck tempo and restores legacy pitch',async()=>{
 const app=appHarness();app.connection.onStatus('Connected',true);
 const value=id=>String(app.elements.get(id)?.value),text=id=>app.elements.get(id)?.textContent;
 await app.connection.onControls(null,null,{...state,pitch_preserving:1,speed:[49152,81920]});
 assert.equal(text('pitch-label'),'Deck tempo');assert.equal(text('pitch-unit'),'%');assert.equal(value('pitch-value'),'75');
 assert.match(text('mix-help'),/preserves pitch/);
 await app.connection.onControls(null,null,{...state,pitch_preserving:1,deck:1,speed:[49152,81920]});
 assert.equal(value('pitch-value'),'125','physical deck change must select that deck’s tempo');
 for(const capability of [undefined,0,true,'1']){
  await app.connection.onControls(null,null,{...state,pitch_preserving:capability,speed:[81920,65536]});
  assert.equal(text('pitch-label'),'Speed / pitch');assert.equal(text('pitch-unit'),'st');
  assert.equal(value('pitch-value'),'3.9');assert.match(text('mix-help'),/speed and pitch together/);
 }
});

test('disconnect and local deck selection restore browser pitch without changing local audio speed',async()=>{
 const app=appHarness();app.audio.decks[0].speed=2**(2/12);app.audio.decks[1].speed=2**(-3/12);
 app.connection.onStatus('Connected',true);
 await app.connection.onControls(null,null,{...state,pitch_preserving:1,deck:1,speed:[65536,49152]});
 assert.equal(String(app.elements.get('pitch-value').value),'75');
 app.connection.onStatus('Disconnected',false);
 assert.equal(app.elements.get('pitch-label').textContent,'Speed / pitch');
 assert.equal(app.elements.get('pitch-unit').textContent,'st');
 assert.equal(String(app.elements.get('pitch-value').value),'+2');
 app.device.events.down('function');
 assert.equal(app.audio.selected,1);assert.equal(String(app.elements.get('pitch-value').value),'-3');
 assert.equal(app.elements.get('pitch-label').textContent,'Speed / pitch');
 assert.equal(app.elements.get('pitch-unit').textContent,'st');
 assert.equal(app.audio.decks[0].speed,2**(2/12));assert.equal(app.audio.decks[1].speed,2**(-3/12));
});

const syncState={...state,pitch_preserving:1,grid_bpm:[120000,98345],grid_valid:[1,1],sync:[0,2],sync_error:[0,0],tap_count:[4,4]};
test('selected hardware sync readout labels source BPM and follows deck/state changes',async()=>{
 const app=appHarness();app.connection.onStatus('Connected',true);
 await app.connection.onControls(null,null,syncState);
 const readout=app.elements.get('hardware-sync');assert.equal(readout.hidden,false);
 assert.equal(readout.textContent,'Deck A · Source 120 BPM · Manual tempo');
 await app.connection.onControls(null,null,{...syncState,deck:1});
 assert.equal(readout.textContent,'Deck B · Source 98.3 BPM · Beat locked');
 await app.connection.onControls(null,null,{...syncState,deck:1,sync:[0,1]});
 assert.match(readout.textContent,/Aligning beats/);
 assert.doesNotMatch(readout.textContent,/key|detected|automatic/i);
});

test('tap progress and every sync error give physical guidance without claiming a beat lock',async()=>{
 const app=appHarness();app.connection.onStatus('Connected',true);
 for(let count=1;count<=3;count++){
  await app.connection.onControls(null,null,{...syncState,grid_bpm:[0,98345],grid_valid:[0,1],tap_count:[count,4],sync_error:[1,0]});
  assert.match(app.elements.get('hardware-sync').textContent,new RegExp(`Tap ${count}/4`));
  assert.match(app.elements.get('hardware-sync').textContent,/Source BPM unknown/);
 }
 const guidance={2:/Tap four steady beats for both songs/,3:/Start both decks/,4:/closer in tempo/,5:/Song changed/,6:/Tap four steady beats again/,7:/Playback moved/,8:/Enable sync/,9:/Retry on the player/};
 for(const [code,expected] of Object.entries(guidance)){
  await app.connection.onControls(null,null,{...syncState,deck:1,sync_error:[0,Number(code)]});
  const text=app.elements.get('hardware-sync').textContent;
  assert.match(text,expected);assert.doesNotMatch(text,/Beat locked|Aligning beats/);
 }
});

test('sync readout rejects malformed optional telemetry and clears on legacy connection or disconnect',async()=>{
 const app=appHarness();app.connection.onStatus('Connected',true);
 await app.connection.onControls(null,null,syncState);const readout=app.elements.get('hardware-sync');
 const bad=[{grid_bpm:undefined},{grid_bpm:[120000]},{grid_bpm:['120000',98345]},
  {grid_bpm:[19000,98345]},{grid_valid:[true,1]},{sync:[0,4]},{sync_error:[0,10]},
  {tap_count:[5,4]},{pitch_preserving:true},{pitch_preserving:'1'},{pitch_preserving:undefined}];
 for(const patch of bad){
  await app.connection.onControls(null,null,{...syncState,...patch});
  assert.equal(readout.hidden,true);assert.equal(readout.textContent,'');
 }
 await app.connection.onControls(null,null,{...syncState,deck:1,grid_valid:[0,1]});
 assert.doesNotMatch(readout.textContent,/Beat locked/,'a missing master grid cannot support the reported lock');
 await app.connection.onControls(null,null,syncState);app.connection.onStatus('Disconnected',false);
 assert.equal(readout.hidden,true);assert.equal(readout.textContent,'');
});

test('sync readout is inside the visible live-player section, outside the connection dialog',()=>{
 const html=readFileSync(new URL('../public/index.html',import.meta.url),'utf8');
 const start=html.indexOf('<section id="live-player"'),end=html.indexOf('</section>',start);
 assert.match(html.slice(start,end),/id="hardware-sync"/);
 assert.equal((html.match(/id="hardware-sync"/g)||[]).length,1);
 assert.doesNotMatch(html.slice(html.indexOf('<dialog id="connect-dialog"')),/id="hardware-sync"/);
});
