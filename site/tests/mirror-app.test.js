import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
import * as core from '../public/core.js';
import * as live from '../public/live-audio.js';
import * as mirror from '../public/mirror.js';
import * as heights from '../public/control-height.js';
import * as protocol from '../public/protocol.js';

// Run the real application callback/frame loop with rendering and DOM at their boundary.
function appHarness(){
 const elements=new Map();let device,connection,now=100;
 const node=()=>({value:0,hidden:false,classList:{toggle(){},add(){},remove(){}},setAttribute(){},addEventListener(){},getContext(){return {clearRect(){},beginPath(){},moveTo(){},lineTo(){},stroke(){}}}});
 const document={getElementById(id){if(!elements.has(id))elements.set(id,node());return elements.get(id);},querySelectorAll(){return [];},querySelector(){return null;},addEventListener(){}};
 class Audio {constructor(){Object.assign(this,{gains:[1,1,1,1],mutes:[false,false,false,false],solos:[false,false,false,false],buffers:[],volume:.75,speed:1,duration:0,playing:false});}setGain(i,v){this.gains[i]=v;}setVolume(v){this.volume=v;}async rate(p){this.speed=2**(p/12);}tick(){}position(){return 0;}pause(){this.playing=false;}}
 class Device {constructor(_,events){device=this;this.events=events;this.targets=[];this.buttons={};}setGain(i,v){if(!this.hardwareMode)this.targets[i]=v;}setControlHeight(){}setBodyHeight(){}setButton(k,v){this.buttons[k]=v;}lights(values){this.leds=[...values];}setHardwareFrame(f){this.hardwareFrame=f;this.targets=f.faders;this.buttons=f.buttons;this.leds=f.trackLeds;this.statusLeds=f.statusLeds;}setHardwareMode(on){this.hardwareMode=on;}}
 class Connection{constructor(onControls,onStatus,onMirror){connection=this;this.onControls=onControls;this.onStatus=onStatus;this.onMirror=onMirror;}}
 const context=vm.createContext({...core,...protocol,...live,...mirror,...heights,setupLibrary:()=>({state(){},telemetry(){},refresh(){},controls(){}}),localStorage:{getItem(){return null;},setItem(){}},document,AudioEngine:Audio,Device,SP1Connection:Connection,navigator:{serial:{addEventListener(){}}},performance:{now:()=>now},console,setTimeout,clearTimeout,setInterval,Uint8Array,zip(){}});
 vm.runInContext(readFileSync(new URL('../public/app.js',import.meta.url),'utf8').replace(/^import .*;\n/gm,''),context);
 return {device,connection,frame(ms=100){now+=ms;device.events.frame();},elements};
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
