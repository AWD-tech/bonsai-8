import test from 'node:test';
import assert from 'node:assert/strict';
import {SP1Connection,dualDeckStatus,selectedDeckControls} from '../public/protocol.js';
const state={firmware:'sp1-dual-deck-0.2',deck:1,master:64,gains:[256,128,64,0,0,64,128,256],mute:[0,5],playing:[0,1],speed:[65536,81920],slots:[1,2]};
test('selected deck maps all four stems, mute mask, transport, wheel and volume without stock reversal',()=>{
 assert.deepEqual(selectedDeckControls(state),{deck:1,slot:2,gains:[0,.25,.5,1],mutes:[true,false,true,false],volume:.25,speed:1.25,playing:true});
 assert.deepEqual(selectedDeckControls({...state,deck:0}),{deck:0,slot:1,gains:[1,.5,.25,0],mutes:[false,false,false,false],volume:.25,speed:1,playing:false});
});
test('Dual Deck telemetry accepts real bounds and ignores banners or malformed records',()=>{
 assert.deepEqual(dualDeckStatus(JSON.stringify(state)),state);
 for(const line of ['boot complete','{"firmware":',JSON.stringify({...state,deck:2}),JSON.stringify({...state,gains:[1,2]}),JSON.stringify({...state,speed:[0,65536]}),JSON.stringify({...state,master:999})])assert.equal(dualDeckStatus(line),null);
});
test('Dual Deck connection polls only read-only diagnostics and accepts fragmented JSON',async()=>{
 let controller,closed=false;const writes=[],snapshots=[];
 const port={readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(raw){
  const text=new TextDecoder().decode(raw);writes.push(text);assert.equal(text,'DDSTAT?\n');
  const bytes=new TextEncoder().encode('startup banner\n\r\n'+JSON.stringify(state)+'\n');
  controller.enqueue(bytes.slice(0,20));controller.enqueue(bytes.slice(20,69));controller.enqueue(bytes.slice(69));
 }}),open:async()=>{},setSignals:async()=>{},close:async()=>{closed=true;}};
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});
 const monitor=new SP1Connection((f,b,s)=>{assert.equal(f,null);assert.equal(b,null);snapshots.push(s);},()=>{});
 try{await monitor.connect();assert.deepEqual(snapshots[0],state);await monitor.disconnect();await monitor.pollTask;assert.equal(closed,true);assert.ok(writes.length>=1);assert.ok(writes.every(x=>x==='DDSTAT?\n'));}
 finally{Object.defineProperty(globalThis,'navigator',prev);}
});
test('A port owned by another process gives an upload-specific error without sending commands',async()=>{
 let writes=0;const port={open:async()=>{throw new Error('busy');},close:async()=>{},writable:{getWriter(){writes++;}}};
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});
 try{const monitor=new SP1Connection(()=>{},()=>{});await assert.rejects(monitor.connect(),/Wait for the song upload to finish/);assert.equal(writes,0);assert.equal(monitor.port,null);}
 finally{Object.defineProperty(globalThis,'navigator',prev);}
});

test('Bonsai branding, occupied library rows and deck loading validate strict reply shapes',async()=>{const {libraryPacket,loadPacket}=await import('../public/protocol.js');assert.ok(dualDeckStatus(JSON.stringify({...state,firmware:'bonsai-8-0.4.0'})));const valid={library:1,sample_rate:48000,slots:[{slot:4,present:[1,0,0,0],frames:96000,title:null}]};assert.deepEqual(libraryPacket(JSON.stringify(valid)),valid);assert.equal(libraryPacket(JSON.stringify({...valid,slots:[...valid.slots,...valid.slots]})),null);assert.equal(libraryPacket(JSON.stringify({...valid,sample_rate:24000})),null);assert.deepEqual(loadPacket('{"loaded":1,"deck":1,"slot":16}'),{loaded:1,deck:1,slot:16});assert.equal(loadPacket('{"loaded":1,"deck":2,"slot":16}'),null);});

test('exclusive transfer waits for polling then routes fragmented binary replies without concurrent commands',async()=>{
 let controller,transferring=false,releaseTransfer;const writes=[];
 const port={readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(raw){const text=new TextDecoder().decode(raw);writes.push(text);if(text==='DDSTAT?\n'){assert.equal(transferring,false);controller.enqueue(new TextEncoder().encode(JSON.stringify(state)+'\n'));}else if(text==='binary'){transferring=true;controller.enqueue(new Uint8Array([0,10]));controller.enqueue(new Uint8Array([255,0]));}else throw Error(text);}}),open:async()=>{},setSignals:async()=>{},close:async()=>{}};
 const prev=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});const monitor=new SP1Connection(()=>{},()=>{});
 try{await monitor.connect();const gate=new Promise(resolve=>releaseTransfer=resolve);const operation=monitor.withTransfer(async io=>{assert.deepEqual([...(await io.exchange(new TextEncoder().encode('binary'),4))],[0,10,255,0]);await gate;transferring=false;});await assert.rejects(monitor.withTransfer(()=>{}),/already in progress/);await new Promise(resolve=>setTimeout(resolve,150));const count=writes.length;assert.equal(writes.at(-1),'binary');await new Promise(resolve=>setTimeout(resolve,60));assert.equal(writes.length,count);releaseTransfer();await operation;await monitor.disconnect();await monitor.pollTask;}finally{await monitor.disconnect();Object.defineProperty(globalThis,'navigator',prev);}
});
test('recording states extend telemetry timeouts across the storage cache flush',async()=>{const connection=new SP1Connection(()=>{},()=>{}),calls=[];connection.jsonCommand=(kind,text,timeout)=>{calls.push({kind,text,timeout});return Promise.resolve({});};connection.recordSaving=true;await connection.statusCommand();await connection.mirrorCommand();assert.equal(calls[0].timeout,15000);assert.equal(calls[1].timeout,15000);connection.recordSaving=false;await connection.mirrorCommand();assert.equal(calls[2].timeout,1600);});
