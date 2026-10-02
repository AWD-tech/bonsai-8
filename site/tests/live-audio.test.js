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
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator'),s=stream('MacBook Microphone');
 Object.defineProperty(globalThis,'navigator',{configurable:true,value:{mediaDevices:{enumerateDevices:async()=>[],getUserMedia:async()=>s}}});
 try{const a=new LiveSP1Audio();await assert.rejects(a.start(),/No SP-1 USB audio input/);assert.equal(s.track.stopped,true);assert.equal(a.context,null);assert.equal(a.stream,null);}
 finally{Object.defineProperty(globalThis,'navigator',prev);}
});
test('deck positions come from the device sample count, including paused and cued states',()=>{
 assert.deepEqual(deviceProgress({length:[240000,480000],position:[120000,0]},0),{seconds:5,duration:10,fraction:.5});
 assert.deepEqual(deviceProgress({length:[240000,480000],position:[120000,0]},1),{seconds:0,duration:20,fraction:0});
 assert.equal(deviceProgress({length:[4],position:[99]},0),null);assert.equal(deviceProgress({},0),null);
});
