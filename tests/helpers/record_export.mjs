// Synthetic native-recorder output passes through the real site's transfer
// framing, metadata validation, decoder, exact-length export and WAV writer.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {DeviceLibrary, STORAGE, join, u32} from '../../site/public/device-library.js';

const directory=process.argv[2],text=s=>new TextEncoder().encode(s);
const [meta,x3,audio,expected]=await Promise.all(['meta.bin','x3.bin','audio.bin','expected.pcm'].map(name=>readFile(`${directory}/${name}`)));
const slot=12,base=STORAGE.base+(slot-1)*4*STORAGE.trackBlocks;
const reads=[],commands=[];
const connection={async withTransfer(work){return work({async exchange(request,count){
 const command=String.fromCharCode(request[0]);commands.push(command);
 if(command==='S'){assert.equal(new TextDecoder().decode(request),'SP1XFER!P');assert.equal(count,28);return join(text('SP1!'),...[512,16,4,4096,86016,STORAGE.magic].map(u32));}
 if(command==='X'){assert.equal(count,1);return text('x');}
 assert.equal(command,'R','export must not write storage');assert.equal(count,513);
 const block=new DataView(request.buffer,request.byteOffset,request.byteLength).getUint32(1,true);reads.push(block);
 const data=block<2?meta.subarray(block*512,(block+1)*512):block>=3&&block<6?x3.subarray((block-3)*512,(block-2)*512):audio.subarray((block-base)*512,(block-base+1)*512);
 assert.equal(data.length,512);return join(text('r'),data);
 }});}};
const result=await new DeviceLibrary(connection,{checkpointStore:null}).export(slot);
assert.equal(result.length,1);assert.equal(result[0].stem,0);
const wav=result[0].wav,view=new DataView(wav.buffer,wav.byteOffset,wav.byteLength);
assert.equal(view.getUint16(22,true),2);assert.equal(view.getUint32(24,true),24000);
assert.equal(view.getUint32(40,true),expected.length);
assert.deepEqual(Buffer.from(wav.subarray(44)),expected);
assert.ok(expected.some(byte=>byte!==0));
assert.deepEqual(reads,[0,1,3,4,5,...Array.from({length:audio.length/512},(_,i)=>base+i)]);
assert.equal(commands.at(-1),'X');
console.log(`PASS: actual C recorder to site WAV export, ${expected.length/4} exact stereo frames, ${audio.length/512} sectors, no storage writes`);
