import test from 'node:test';
import assert from 'node:assert/strict';
import {LiveSP1Audio,deviceProgress} from '../public/live-audio.js';
function stream(label){const track={label,stopped:false,stop(){this.stopped=true;},addEventListener(){}};return {track,getTracks(){return [track];},getAudioTracks(){return [track];}};}
class Node {constructor(){this.gain={value:0};}connect(){}disconnect(){}}
class Context {constructor(){this.destination={};}async resume(){}createMediaStreamSource(s){assert.match(s.track.label,/SP-1/);return new Node();}createAnalyser(){return new Node();}createGain(){return new Node();}async close(){this.closed=true;}}
test('live audio accepts only the SP-1 and requests unprocessed stereo from its exact input',async()=>{
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator'),oldContext=globalThis.AudioContext,calls=[],s=stream('SP-1 Dual Deck');
 Object.defineProperty(globalThis,'navigator',{configurable:true,value:{mediaDevices:{enumerateDevices:async()=>[{kind:'audioinput',label:s.track.label,deviceId:'sp1-usb'}],getUserMedia:async c=>{calls.push(c);return s;}}}});globalThis.AudioContext=Context;
 try{const a=new LiveSP1Audio();await a.start();assert.equal(a.stream,s);assert.deepEqual(calls[0].audio.deviceId,{exact:'sp1-usb'});assert.equal(calls[0].audio.echoCancellation,false);assert.equal(calls[0].audio.noiseSuppression,false);a.stop();assert.equal(s.track.stopped,true);assert.equal(a.analyser,null);}
 finally{Object.defineProperty(globalThis,'navigator',prev);globalThis.AudioContext=oldContext;}
});
test('an unidentified computer microphone is stopped before any monitoring or playback graph exists',async()=>{
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator'),oldContext=globalThis.AudioContext,s=stream('MacBook Microphone');globalThis.AudioContext=Context;
 Object.defineProperty(globalThis,'navigator',{configurable:true,value:{mediaDevices:{enumerateDevices:async()=>[],getUserMedia:async()=>s}}});
 try{const a=new LiveSP1Audio();await assert.rejects(a.start(),/No SP-1 USB audio input/);assert.equal(s.track.stopped,true);assert.equal(a.context,null);assert.equal(a.stream,null);}
 finally{Object.defineProperty(globalThis,'navigator',prev);globalThis.AudioContext=oldContext;}
});
test('deck positions come from the device sample count, including paused and cued states',()=>{
 assert.deepEqual(deviceProgress({length:[240000,480000],position:[120000,0]},0),{seconds:5,duration:10,fraction:.5});
 assert.deepEqual(deviceProgress({length:[240000,480000],position:[120000,0]},1),{seconds:0,duration:20,fraction:0});
 assert.equal(deviceProgress({length:[4],position:[99]},0),null);assert.equal(deviceProgress({},0),null);
});

function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};}
function namedStream(label='Bonsai 8'){
 const listeners=new Map(),track={label,readyState:'live',stopped:false,stop(){this.stopped=true;this.readyState='ended';},addEventListener(event,fn){listeners.set(event,fn);},emit(event){listeners.get(event)?.();},getSettings(){return {deviceId:'sp1-usb'};}};
 return {track,getTracks(){return [track];},getAudioTracks(){return [track];}};
}
async function environment({getUserMedia,resume=async()=>{},events=[],devices=[{kind:'audioinput',label:'Bonsai 8',deviceId:'sp1-usb'}]},run){
 const previous=Object.getOwnPropertyDescriptor(globalThis,'navigator'),previousContext=globalThis.AudioContext,contexts=[];
 class TestContext extends Context{
  constructor(){super();this.state='suspended';contexts.push(this);events.push('context');}
  async resume(){events.push('resume');await resume(this,contexts.length);this.state='running';}
  createMediaStreamSource(s){assert.match(s.track.label,/(SP-1|Bonsai 8)/);return new Node();}
  async close(){this.state='closed';this.closed=true;}
 }
 Object.defineProperty(globalThis,'navigator',{configurable:true,value:{mediaDevices:{enumerateDevices:async()=>{events.push('enumerate');return devices;},getUserMedia}}});globalThis.AudioContext=TestContext;
 try{await run(contexts);}finally{Object.defineProperty(globalThis,'navigator',previous);globalThis.AudioContext=previousContext;}
}

test('live output resumes during the initiating gesture, before device enumeration awaits',async()=>{
 const events=[],s=namedStream();await environment({getUserMedia:async()=>s,events},async()=>{
  const a=new LiveSP1Audio(),starting=a.start(),firstEvents=events.slice(0,3);await starting;a.stop();assert.deepEqual(firstEvents,['context','resume','enumerate']);
 });
});

test('stop during pending media permission cancels startup and releases the late stream',async()=>{
 const media=deferred(),requested=deferred(),s=namedStream();await environment({getUserMedia:()=>{requested.resolve();return media.promise;}},async contexts=>{
  const a=new LiveSP1Audio(),starting=a.start();const result=starting.catch(error=>error);await requested.promise;a.stop();media.resolve(s);
  const error=await result;assert.equal(error?.name,'AbortError');assert.equal(a.stream,null);assert.equal(a.context,null);assert.equal(a.busy,false);assert.equal(s.track.stopped,true);assert.ok(contexts.every(context=>context.closed));
 });
});

test('stop during pending resume cancels startup without publishing an audio graph',async()=>{
 const resumed=deferred(),requested=deferred(),s=namedStream();await environment({getUserMedia:async()=>{requested.resolve();return s;},resume:()=>resumed.promise},async contexts=>{
  const a=new LiveSP1Audio(),starting=a.start();const result=starting.catch(error=>error);await requested.promise;await Promise.resolve();a.stop();resumed.resolve();
  const error=await result;assert.equal(error?.name,'AbortError');assert.equal(a.stream,null);assert.equal(a.source,null);assert.equal(s.track.stopped,true);assert.ok(contexts.every(context=>context.closed));
 });
});

test('a superseded startup failure cannot clear or stop the newer session',async()=>{
 const first=deferred(),requested=deferred(),current=namedStream();let requests=0;
 await environment({getUserMedia:()=>++requests===1?(requested.resolve(),first.promise):Promise.resolve(current)},async()=>{
  const a=new LiveSP1Audio(),old=a.start().catch(error=>error);await requested.promise;a.stop();await a.start();const currentContext=a.context;
  first.reject(new Error('old permission failed'));const error=await old;
  assert.equal(error?.name,'AbortError');assert.equal(a.stream,current);assert.equal(a.context,currentContext);assert.equal(current.track.stopped,false);assert.equal(currentContext.closed,undefined);a.stop();
 });
});

test('a superseded startup finally cannot clear the newer startup busy state',async()=>{
 const first=deferred(),second=deferred(),firstRequested=deferred(),secondRequested=deferred();let requests=0;
 await environment({getUserMedia:()=>++requests===1?(firstRequested.resolve(),first.promise):(secondRequested.resolve(),second.promise)},async()=>{
  const a=new LiveSP1Audio(),old=a.start().catch(error=>error);await firstRequested.promise;a.stop();const newer=a.start().catch(error=>error);await secondRequested.promise;
  first.reject(new Error('old request failed'));await old;assert.equal(a.busy,true);second.resolve(namedStream());await newer;assert.equal(a.busy,false);a.stop();
 });
});

test('an ended event from a replaced track cannot stop the current monitor',async()=>{
 const first=namedStream(),second=namedStream();let requests=0,ended=0;
 await environment({getUserMedia:async()=>++requests===1?first:second},async()=>{
  const a=new LiveSP1Audio(()=>ended++);await a.start();a.stop();await a.start();first.track.emit('ended');
  assert.equal(a.stream,second);assert.equal(second.track.stopped,false);assert.equal(ended,0);second.track.emit('ended');assert.equal(a.stream,null);assert.equal(ended,1);
 });
});

test('a named input with the wrong exact device identity is never monitored',async()=>{
 const wrong=namedStream();wrong.track.getSettings=()=>({deviceId:'another-input'});
 await environment({getUserMedia:async()=>wrong},async contexts=>{
  const a=new LiveSP1Audio();await assert.rejects(a.start(),/Select the Bonsai 8/);assert.equal(a.stream,null);assert.equal(a.source,undefined);assert.equal(wrong.track.stopped,true);assert.ok(contexts.every(context=>context.closed));
 });
});

test('a track that ends while output resumes is rejected before graph publication',async()=>{
 const resumed=deferred(),requested=deferred(),s=namedStream();await environment({getUserMedia:async()=>{requested.resolve();return s;},resume:()=>resumed.promise},async contexts=>{
  const a=new LiveSP1Audio(),starting=a.start();const result=starting.catch(error=>error);await requested.promise;await Promise.resolve();s.track.readyState='ended';resumed.resolve();
  const error=await result;assert.match(error.message,/USB audio disconnected while starting/);assert.equal(a.stream,null);assert.equal(s.track.stopped,true);assert.ok(contexts.every(context=>context.closed));
 });
});


test('a named physical input is preferred over the browser default alias',async()=>{
 const s=namedStream(),calls=[];await environment({devices:[{kind:'audioinput',label:'Default - Bonsai 8',deviceId:'default'},{kind:'audioinput',label:'Bonsai 8',deviceId:'sp1-usb'}],getUserMedia:async constraints=>{calls.push(constraints);return s;}},async()=>{
  const a=new LiveSP1Audio();await a.start();assert.deepEqual(calls[0].audio.deviceId,{exact:'sp1-usb'});assert.equal(calls.length,1);a.stop();
 });
});

test('a default alias may expose its physical device identity in track settings',async()=>{
 const s=namedStream();await environment({devices:[{kind:'audioinput',label:'Default - Bonsai 8',deviceId:'default'}],getUserMedia:async constraints=>{assert.deepEqual(constraints.audio.deviceId,{exact:'default'});return s;}},async()=>{
  const a=new LiveSP1Audio();await a.start();assert.equal(a.stream,s);a.stop();
 });
});
