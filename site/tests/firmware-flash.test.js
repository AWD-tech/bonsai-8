import test from 'node:test';
import assert from 'node:assert/strict';
import {validateFirmware,firmwareChunks,FirmwareFlasher} from '../public/firmware-flash.js';
import {sha256} from '../public/device-library.js';
import {packet,response} from '../public/protocol.js';
async function release(length=8193){const binary=new Uint8Array(length),d=new DataView(binary.buffer);d.setUint32(0,0x20020000,true);d.setUint32(4,0x20009,true);return {binary,manifest:{product:'Bonsai 8',version:'0.4.0',status:'candidate',bytes:length,application_address:0x20000,sha256:await sha256(binary)}};}
test('manifest hash and vector validation happen before the updater can erase',async()=>{const r=await release();await validateFirmware(r.binary,r.manifest);await assert.rejects(validateFirmware(r.binary,{...r.manifest,sha256:'0'.repeat(64)}),/checksum/);const bad=r.binary.slice();new DataView(bad.buffer).setUint32(4,0x10001,true);await assert.rejects(validateFirmware(bad,{...r.manifest,sha256:await sha256(bad)}),/vectors/);await assert.rejects(validateFirmware(r.binary,{...r.manifest,application_address:0}),/supported/);});
test('updater chunks never cross flash pages or the application boundary',async()=>{const r=await release(0xdf000),chunks=firmwareChunks(r.binary);assert.equal(chunks[0].address,0x20000);let bytes=0;for(const c of chunks){assert.ok(c.payload.length<=248);const n=c.payload.length-8;assert.ok(c.address>=0x20000&&c.address+n<=0xff000);assert.ok((c.address&4095)+n<=4096);bytes+=n;}assert.equal(bytes,r.binary.length);assert.throws(()=>firmwareChunks(new Uint8Array(0xdf001)),/application region/);});
test('wrong bootloader state refuses erase and closes its exclusive port',async()=>{let controller,closed=false;const commands=[];const port={getInfo:()=>({usbVendorId:0x2367,usbProductId:0x1701}),open:async()=>{},setSignals:async()=>{},close:async()=>{closed=true;},readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(raw){const request=response(raw);commands.push(request.command);controller.enqueue(packet(0x53,request.seq,[0,0,0,0,0]));}})};const previous=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});try{await assert.rejects(new FirmwareFlasher().flash(await release()),/fresh bootloader/);assert.deepEqual(commands,[0x52]);assert.equal(closed,true);}finally{Object.defineProperty(globalThis,'navigator',previous);}});
test('application flasher uses the existing updater handshake, bounded chunks and two finalizations',async()=>{let controller,closed=false;const requests=[];const port={getInfo:()=>({usbVendorId:0x2367,usbProductId:0x1701}),open:async()=>{},setSignals:async()=>{},close:async()=>{closed=true;},readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(raw){const r=response(raw);requests.push(r);if(r.command===0x52)controller.enqueue(packet(0x53,r.seq,[0,0,1,0,0]));else if(r.command===0x46)controller.enqueue(packet(0x47,r.seq));else if(r.command===0x48)controller.enqueue(packet(0x49,r.seq,[123,0,0,0]));else assert.ok([0x45,0x50].includes(r.command));}})};const previous=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort:async()=>port}}});try{const r=await release(10),result=await new FirmwareFlasher().flash(r);assert.equal(result.sha256,r.manifest.sha256);assert.deepEqual(requests.map(r=>r.command),[0x52,0x52,0x52,0x46,0x45,0x48,0x48,0x50]);const chunk=requests[4];assert.equal(new DataView(chunk.payload.buffer).getUint32(4,true),0x20000);assert.deepEqual([...requests[5].payload],[0x20,0,0,0]);assert.deepEqual([...requests[6].payload],[123,0,0,0]);assert.equal(closed,true);}finally{Object.defineProperty(globalThis,'navigator',previous);}});
function updater({write,wrongDevice=false}={}){let controller,closed=0;const requests=[];const port={getInfo:()=>({usbVendorId:0x2367,usbProductId:wrongDevice?0x1702:0x1701}),open:async()=>{},setSignals:async()=>{},close:async()=>{closed++;},readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(raw){const r=response(raw);requests.push(r);if(write?.(r,controller)===true)return;if(r.command===0x52)controller.enqueue(packet(0x53,r.seq,[0,0,1,0,0]));else if(r.command===0x46)controller.enqueue(packet(0x47,r.seq));else if(r.command===0x48)controller.enqueue(packet(0x49,r.seq,[0,0,0,0]));}})};return {port,requests,get closed(){return closed;}};}
async function withSerial(requestPort,action){const previous=Object.getOwnPropertyDescriptor(globalThis,'navigator');Object.defineProperty(globalThis,'navigator',{configurable:true,value:{serial:{requestPort}}});try{await action();}finally{if(previous)Object.defineProperty(globalThis,'navigator',previous);else delete globalThis.navigator;}}
test('validated release owns bytes and rejects a filename from another version',async()=>{const r=await release(10),checked=await validateFirmware(r.binary,{...r.manifest,file:'bonsai-8-0.4.0.bin'});r.binary[8]=123;r.manifest.version='9.9.9';assert.equal(checked.binary[8],0);assert.equal(checked.manifest.version,'0.4.0');await assert.rejects(validateFirmware(checked.binary,{...checked.manifest,file:'bonsai-8-9.9.9.bin'}),/filename/);});
test('invalid release never opens the chooser and duplicate calls cannot race validation',async()=>{let choices=0;await withSerial(async()=>{choices++;throw Error('chooser cancelled');},async()=>{const r=await release(10),flasher=new FirmwareFlasher();await assert.rejects(flasher.flash({...r,manifest:{...r.manifest,sha256:'f'.repeat(64)}}),/checksum/);assert.equal(choices,0);const first=flasher.flash(r);await assert.rejects(flasher.flash(r),/already running/);await assert.rejects(first,/chooser cancelled/);assert.equal(choices,1);assert.equal(flasher.active,false);});});
test('wrong device and corrupt CRC fail before an erase request',async()=>{for(const options of [{wrongDevice:true},{write(r,c){const damaged=packet(0x53,r.seq,[0,0,1,0,0]);damaged[1]^=1;c.enqueue(damaged);return true;}}]){const u=updater(options);await withSerial(async()=>u.port,async()=>{await assert.rejects(new FirmwareFlasher().flash(await release(10)),/bootloader|checksum/);assert.ok(!u.requests.some(r=>r.command===0x46));assert.equal(u.closed,1);});}});
test('disconnect during audio-independent application chunks prevents finalization and start',async()=>{const u=updater({write(r,c){if(r.command===0x45){c.close();return true;}}});await withSerial(async()=>u.port,async()=>{await assert.rejects(new FirmwareFlasher().flash(await release(1000)),/disconnected/);assert.ok(u.requests.some(r=>r.command===0x45));assert.ok(!u.requests.some(r=>[0x48,0x50].includes(r.command)));assert.equal(u.closed,1);});});
test('incomplete second finalization cannot start the application',async()=>{let finals=0;const u=updater({write(r,c){if(r.command===0x48&&++finals===2){c.enqueue(packet(0x49,r.seq,[0]));return true;}}});await withSerial(async()=>u.port,async()=>{await assert.rejects(new FirmwareFlasher().flash(await release(10)),/finalization/);assert.equal(finals,2);assert.ok(!u.requests.some(r=>r.command===0x50));assert.equal(u.closed,1);});});
test('a failed attempt releases its port and can retry a fresh updater session',async()=>{const bad=updater({write(r,c){c.close();return true;}}),good=updater({write(r,c){if(r.command===0x50){c.close();return true;}}});let attempts=0;await withSerial(async()=>attempts++?good.port:bad.port,async()=>{const flasher=new FirmwareFlasher(),r=await release(10);await assert.rejects(flasher.flash(r),/disconnected/);const result=await flasher.flash(r);assert.equal(result.version,'0.4.0');assert.equal(bad.closed,1);assert.equal(good.closed,1);assert.ok(good.requests.every(r=>[0x52,0x46,0x45,0x48,0x50].includes(r.command)),'no library format, initialization or storage writes');});});
test('missing updater response clears pending command and marks failure',async()=>{const flasher=new FirmwareFlasher();flasher.active=true;flasher.writer={write:async()=>{}};await assert.rejects(flasher.command(0x52,undefined,0x53,1),/stopped responding/);assert.equal(flasher.pending,null);assert.ok(flasher.readError);});

test('start keeps the port open until a queued reset disconnects, preserving application-only writes',async()=>{
 let resetTimer,resetApplied=false,cancelledReset=false;
 const u=updater({write(r,c){if(r.command===0x50){resetTimer=setTimeout(()=>{resetApplied=true;c.close();},30);return true;}}});
 const close=u.port.close;
 u.port.close=async()=>{if(!resetApplied){cancelledReset=true;clearTimeout(resetTimer);}await close();};
 await withSerial(async()=>u.port,async()=>{
  const flasher=new FirmwareFlasher(),result=await flasher.flash(await release(10));
  assert.equal(result.version,'0.4.0');
  assert.equal(resetApplied,true,'closing immediately would discard the queued application start');
  assert.equal(cancelledReset,false);
  assert.equal(flasher.pending,null);
  assert.equal(flasher.readError,null);
  assert.equal(u.closed,1);
  assert.equal(u.port.readable.locked,false);
  assert.equal(u.port.writable.locked,false);
  assert.ok(u.requests.every(r=>[0x52,0x46,0x45,0x48,0x50].includes(r.command)),'no song-storage command is sent');
 });
});

async function startSession(write,action){
 const u=updater({write}),flasher=new FirmwareFlasher();
 flasher.active=true;flasher.reader=u.port.readable.getReader();flasher.writer=u.port.writable.getWriter();
 flasher.readTask=flasher.read();
 try{await action(flasher,u);}finally{
  flasher.active=false;try{await flasher.reader.cancel();}catch{}await flasher.readTask;
  flasher.reader.releaseLock();flasher.writer.releaseLock();await u.port.close();
 }
}

test('start accepts a matching-sequence reply without assuming command 0x51',async()=>{
 await startSession((r,c)=>{c.enqueue(packet(0x50,r.seq));return true;},async(flasher,u)=>{
  await flasher.startApplication(100);
  assert.equal(flasher.pending,null);assert.equal(flasher.readError,null);
  assert.deepEqual(u.requests.map(r=>r.command),[0x50]);
 });
});

test('stale start replies cannot finish the wait; no reply has a bounded successful timeout',async()=>{
 for(const wrongSequence of [false,true])await startSession((r,c)=>{if(wrongSequence)c.enqueue(packet(0x51,(r.seq+1)&255));return true;},async(flasher)=>{
  let settled=false;const waiting=flasher.startApplication(30).then(()=>{settled=true;});
  await new Promise(resolve=>setTimeout(resolve,5));
  assert.equal(settled,false,'a stale or absent reply must not close the port immediately');
  assert.ok(flasher.pending);
  await waiting;
  assert.equal(settled,true);assert.equal(flasher.pending,null);assert.equal(flasher.readError,null);
 });
});

test('start allows a reset disconnect but does not swallow read or write failures',async()=>{
 await startSession((r,c)=>{c.error(new DOMException('Device removed','NetworkError'));return true;},async(flasher)=>{
  await flasher.startApplication(100);assert.equal(flasher.readError,null);assert.equal(flasher.pending,null);
 });
 for(const failure of ['read','checksum','write'])await startSession((r,c)=>{
  if(failure==='read'){c.error(new Error('invalid transport state'));return true;}
  if(failure==='checksum'){const damaged=packet(0x50,r.seq);damaged[1]^=1;c.enqueue(damaged);return true;}
  throw new Error('start write failed');
 },async(flasher)=>{
  await assert.rejects(flasher.startApplication(100),/invalid transport state|checksum|start write failed/);
  assert.equal(flasher.pending,null);assert.ok(flasher.readError);
 });
});

test('a recorded reader error before start prevents the start write',async()=>{
 await startSession(()=>true,async(flasher,u)=>{
  flasher.fail(new Error('earlier read failed'));
  await assert.rejects(flasher.startApplication(100),/earlier read failed/);
  assert.equal(u.requests.length,0);assert.equal(flasher.pending,null);
 });
});
