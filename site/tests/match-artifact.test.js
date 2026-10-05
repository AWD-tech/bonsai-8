import test from 'node:test';
import assert from 'node:assert/strict';
import {renderMatched} from '../public/match-dsp.js';
import {wav} from '../public/core.js';
import {prepareStems,DeviceLibrary,STORAGE,join,u32} from '../public/device-library.js';
const text=s=>new TextEncoder().encode(s),dv=a=>new DataView(a.buffer,a.byteOffset,a.byteLength);
function buffer(channels,sampleRate=24000){return {sampleRate,length:channels[0].length,numberOfChannels:channels.length,duration:channels[0].length/sampleRate,getChannelData:i=>channels[i]};}
function waveFrequency(bytes){const d=dv(bytes),rate=d.getUint32(24,true),channels=d.getUint16(22,true),frames=(bytes.length-44)/(channels*2),start=Math.floor(.1*rate),end=frames-start;let crossings=0;for(let i=start+1;i<end;i++)if(d.getInt16(44+(i-1)*channels*2,true)<0&&d.getInt16(44+i*channels*2,true)>=0)crossings++;return crossings/((end-start)/rate);}
function storage(){const meta=new Uint8Array(1024),x3=new Uint8Array(1536);dv(meta).setUint32(0,STORAGE.magic,true);dv(x3).setUint32(0,STORAGE.x3Magic,true);dv(x3).setUint16(4,1,true);const sectors=new Map([[0,meta.slice(0,512)],[1,meta.slice(512)],[3,x3.slice(0,512)],[4,x3.slice(512,1024)],[5,x3.slice(1024)]]);return {withTransfer:async work=>work({exchange:async req=>{const command=String.fromCharCode(req[0]);if(command==='S')return join(text('SP1!'),...[512,16,4,4096,86016,STORAGE.magic].map(u32));if(command==='R')return join(text('r'),sectors.get(dv(req).getUint32(1,true))||new Uint8Array(512));if(command==='B'){sectors.set(dv(req).getUint32(1,true),req.slice(6));return text('b');}if(command==='F')return text('f');if(command==='X')return text('x');throw Error('Unexpected storage command');}})};}
test('prepared pitch/tempo audio survives WAV export, P14S upload and device export',async()=>{
 const sr=24000,source=Float32Array.from({length:sr},(_,i)=>.25*Math.sin(2*Math.PI*440*i/sr)),prepared=renderMatched({channels:[source,source],sampleRate:sr,tempo:1.25,semitones:7}),buffers=[buffer(prepared),null,null,null];
 const exported=new Uint8Array(wav(prepared,sr));assert.equal(dv(exported).getUint32(24,true),24000);assert.equal((exported.length-44)/4,19200);assert.ok(Math.abs(waveFrequency(exported)-440*2**(7/12))<3);
 // At 24 kHz no resampling is needed. This boundary stub verifies the real
 // prepareStems encoder and actual uploader/exporter without a hardware port.
 const previous=globalThis.OfflineAudioContext;
 globalThis.OfflineAudioContext=class {constructor(channels,frames,rate){this.frames=frames;this.rate=rate;this.destination={};}createBufferSource(){this.source={connect(){},start(){}};return this.source;}async startRendering(){assert.equal(this.rate,24000);assert.equal(this.source.buffer.sampleRate,24000);assert.equal(this.frames,this.source.buffer.length);return this.source.buffer;}};
 try{const encoded=await prepareStems(buffers),library=new DeviceLibrary(storage(),{checkpointStore:null}),receipt=await library.upload(7,encoded);assert.equal(receipt.slot,7);assert.equal(receipt.frames,38400);assert.deepEqual(receipt.present,[1,0,0,0]);const restored=await library.export(7);assert.equal(restored.length,1);assert.equal((restored[0].wav.length-44)/4,19200);assert.ok(Math.abs(waveFrequency(restored[0].wav)-440*2**(7/12))<3);}finally{if(previous===undefined)delete globalThis.OfflineAudioContext;else globalThis.OfflineAudioContext=previous;}
});
