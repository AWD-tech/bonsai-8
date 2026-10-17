import {DeviceLibrary,STORAGE,join,u32} from './device-library.js';

const check=(condition,message)=>{if(!condition)throw new Error(message);};
const text=value=>new TextEncoder().encode(value);
const view=bytes=>new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);

// These sectors come from the host-compiled recorder adapter, never an SP-1.
export async function run(){
 const [meta,x3,audio,expected]=await Promise.all(['meta.bin','x3.bin','audio.bin','expected.pcm'].map(async name=>new Uint8Array(await (await fetch(`/recording-fixture/${name}`)).arrayBuffer())));
 check(meta.length===1024&&x3.length===1536&&audio.length%512===0&&expected.length>0&&expected.length%4===0,'Invalid synthetic recorder fixture');
 const slot=12,base=STORAGE.base+(slot-1)*4*STORAGE.trackBlocks,reads=[],commands=[];
 const connection={async withTransfer(work){return work({async exchange(request,count){
  const command=String.fromCharCode(request[0]);commands.push(command);
  if(command==='S'){
   check(new TextDecoder().decode(request)==='SP1XFER!P'&&count===28,'Invalid transfer handshake');
   return join(text('SP1!'),...[512,16,4,4096,86016,STORAGE.magic].map(u32));
  }
  if(command==='X'){check(count===1,'Invalid transfer exit');return text('x');}
  check(command==='R'&&count===513,'Export attempted a write or unsupported command');
  const block=view(request).getUint32(1,true);reads.push(block);
  let data;
  if(block<2)data=meta.slice(block*512,(block+1)*512);
  else if(block>=3&&block<6)data=x3.slice((block-3)*512,(block-2)*512);
  else if(block>=base&&block<base+audio.length/512)data=audio.slice((block-base)*512,(block-base+1)*512);
  check(data?.length===512,'Export read outside fixture sectors');return join(text('r'),data);
 }});}};
 const exported=await new DeviceLibrary(connection,{checkpointStore:null}).export(slot);
 check(exported.length===1&&exported[0].stem===0,'Saved mix should export one stereo WAV');
 const wav=exported[0].wav,header=view(wav),pcm=wav.subarray(44);
 check(header.getUint16(22,true)===2&&header.getUint32(24,true)===24000,'WAV is not 24 kHz stereo');
 check(header.getUint32(40,true)===expected.length&&pcm.length===expected.length,'WAV did not preserve the exact saved length');
 check(pcm.every((byte,index)=>byte===expected[index]),'WAV PCM differs from host recorder output');
 check(expected.some(byte=>byte!==0),'Saved synthetic mix was silent');
 const expectedReads=[0,1,3,4,5,...Array.from({length:audio.length/512},(_,index)=>base+index)];
 check(reads.length===expectedReads.length&&reads.every((block,index)=>block===expectedReads[index])&&commands.at(-1)==='X','Export did not read the complete take and exit transfer mode');
 const decoder=new OfflineAudioContext(2,1,24000),decoded=await decoder.decodeAudioData(wav.buffer.slice(wav.byteOffset,wav.byteOffset+wav.byteLength));
 const frames=expected.length/4,reference=view(expected);let maximumPcmError=0,worstSample=null;const energy=[0,0];
 check(decoded.numberOfChannels===2&&decoded.sampleRate===24000&&decoded.length===frames,'Native browser decode changed channel count, sample rate, or length');
 check(Math.abs(decoded.duration-frames/24000)<1e-12,'Native browser decode changed duration');
 for(let channel=0;channel<2;channel++)for(let frame=0;frame<frames;frame++){
  const actual=decoded.getChannelData(channel)[frame],wanted=reference.getInt16(frame*4+channel*2,true)/32768;
  const error=Math.abs(actual-wanted);if(error>maximumPcmError){maximumPcmError=error;worstSample={channel,frame,actual,wanted};}energy[channel]+=actual*actual;
 }
 // Native WAV decoders may normalize positive int16 by 32767 rather than
 // 32768. Byte-exact WAV preservation is checked above; decoded float samples
 // must remain within one 16-bit PCM step, with the measured error reported.
 const decodedFloatTolerance=1/32768;
 check(maximumPcmError<=decodedFloatTolerance,`Native browser WAV decode exceeds one PCM step: ${JSON.stringify({maximumPcmError,worstSample})}`);
 return {pass:true,origin:'Host-compiled C recorder adapter and eight-stem engine; synthetic storage, no physical SP-1',slot,audioSectors:audio.length/512,stereoFrames:frames,wavBytes:wav.length,sampleRate:decoded.sampleRate,durationSeconds:decoded.duration,outputRms:energy.map(value=>Math.sqrt(value/frames)),rawWavPcmExact:true,maximumPcmError,decodedFloatTolerance,storageReads:reads.length,storageWrites:0,transferExited:commands.at(-1)==='X'};
}
