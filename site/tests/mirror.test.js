import test from 'node:test';
import assert from 'node:assert/strict';
import {mirrorPacket,MirrorTimeline} from '../public/mirror.js';
import {SP1Connection} from '../public/protocol.js';
const row=(seq,ms,buttons=0)=>[seq,ms,buttons,0,64,128,256,52,0,52,0,0,66,0,66];
const parse=(frames,now=1000,lost=0)=>mirrorPacket(JSON.stringify({mirror:1,now,lost,frames}));
test('physical telemetry validates shape and maps all ten switches, positions and eight PWM duties',()=>{
 const p=parse([row(1,1000,1023)]),f=p.frames[0];
 assert.ok(Object.values(f.buttons).every(Boolean));assert.deepEqual(f.faders,[0,.25,.5,1]);
 assert.deepEqual(f.trackLeds,[.052,0,.052,0]);assert.deepEqual(f.statusLeds,[0,.066,0,.066]);
 for(const bad of [row(1,1000,1024),[...row(1,1000),0],row(1,1000).map((v,i)=>i===3?257:v),row(1,1000).map((v,i)=>i===8?1001:v)])assert.equal(parse([bad]),null);
 assert.equal(parse(Array.from({length:17},(_,i)=>row(i,1000))),null);
});
test('a tap and blink between polls retain their timing, with no stuck release',()=>{
 const seen=[],t=new MirrorTimeline(f=>seen.push(f));
 t.push(parse([row(1,1000)]),100);t.tick(160);
 t.push(parse([row(2,1008,16),row(3,1040,0)],1045),145);
 t.tick(168);assert.equal(seen.at(-1).buttons.play,true);
 t.tick(199);assert.equal(seen.at(-1).buttons.play,true);
 t.tick(200);assert.equal(seen.at(-1).buttons.play,false);
 assert.deepEqual(seen.map(f=>f.ms),[1000,1008,1040]);
});
test('stale USB releases controls, loss resynchronizes, and counter wrap is accepted',()=>{
 const seen=[],t=new MirrorTimeline(f=>seen.push(f));
 t.push(parse([row(0xffffffff,0xfffffff0,16)],0xfffffff0),100);t.tick(160);
 t.push(parse([row(0,16,0)],20),136);t.tick(196);assert.equal(seen.at(-1).buttons.play,false);
 t.tick(1200);assert.equal(seen.at(-1),null);
 assert.equal(t.push(parse([row(2,1020,16),row(3,1052)],1060,2),1210),true);
 t.tick(1300);assert.equal(seen.at(-1).buttons.play,false);assert.equal(seen.filter(x=>x?.seq===2).length,0);
});
test('0.3 requests the read-only physical feed and routes fragmented replies separately from mixer state',async()=>{
 const state={firmware:'sp1-dual-deck-0.3',mirror:1,deck:0,master:64,gains:[256,256,256,256,0,0,0,0],mute:[0,0],playing:[1,0],speed:[65536,65536],slots:[1,2]};
 let controller;const writes=[],seen=[];
 const port={readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(raw){
  const command=new TextDecoder().decode(raw);writes.push(command);assert.ok(['DDSTAT?\n','DDMIR?\n'].includes(command));
  const value=command==='DDSTAT?\n'?state:{mirror:1,now:1000,lost:0,frames:[row(1,1000)]};
  const bytes=new TextEncoder().encode(JSON.stringify(value)+'\n');for(let i=0;i<bytes.length;i+=13)controller.enqueue(bytes.slice(i,i+13));
 }}),open:async()=>{},setSignals:async()=>{},close:async()=>{}};
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});
 const connection=new SP1Connection(()=>{},()=>{},p=>seen.push(p));
 try{await connection.connect();assert.equal(connection.fullMirror,true);assert.equal(seen[0].frames[0].trackLeds[0],.052);await connection.disconnect();await connection.pollTask;assert.deepEqual(writes.slice(0,2),['DDSTAT?\n','DDMIR?\n']);}
 finally{Object.defineProperty(globalThis,'navigator',prev);}
});
