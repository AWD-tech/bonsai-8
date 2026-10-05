import {SoundTouch} from './vendor/soundtouch-core-2.1.1/index.js';
export const MATCH_LIMITS=Object.freeze({maxSeconds:480,maxWorkingBytes:384*1024*1024,minTempo:.5,maxTempo:2,minPitch:-12,maxPitch:12});
export function validateRender({channels,sampleRate,tempo=1,semitones=0}){
 if(!Array.isArray(channels)||channels.length<1||channels.length>2||channels.some(c=>!(c instanceof Float32Array)||c.length!==channels[0].length)||!channels[0].length||!Number.isInteger(sampleRate)||sampleRate<8000||sampleRate>96000)throw Error('Matching needs mono or stereo PCM at 8–96 kHz.');
 if(!Number.isFinite(tempo)||tempo<MATCH_LIMITS.minTempo||tempo>MATCH_LIMITS.maxTempo||!Number.isFinite(semitones)||semitones<-12||semitones>12)throw Error('Use a tempo between half and double speed, and pitch within one octave.');
 const outputFrames=Math.round(channels[0].length/tempo);
 if(channels[0].length/sampleRate>MATCH_LIMITS.maxSeconds||outputFrames/sampleRate>MATCH_LIMITS.maxSeconds)throw Error('Source and prepared songs must fit within eight minutes.');
 if((channels[0].length+outputFrames)*channels.length*4>MATCH_LIMITS.maxWorkingBytes)throw Error('This stem is too large to prepare safely in the browser.');
 return outputFrames;
}
// Genuine WSOLA stretch plus Lanczos rate transposition. Rate changes pitch;
// compensating stretch sets duration independently. Both stereo channels share
// every overlap decision. Web Audio playbackRate is not used for preparation.
export function renderMatched({channels,sampleRate,tempo=1,semitones=0},progress=()=>{}){
 const frames=validateRender({channels,sampleRate,tempo,semitones}),length=channels[0].length;
 if(tempo===1&&semitones===0){for(const channel of channels)for(const value of channel)if(!Number.isFinite(value))throw Error('Audio contains invalid samples.');return channels.map(c=>c.slice());}
 const pitch=2**(semitones/12),st=new SoundTouch({sampleRate,sampleBufferType:'circular',interpolationStrategy:{id:'lanczos',params:{zeroCrossings:4,normalize:true}}});
 st.pitch=pitch;st.stretch.tempo=tempo/pitch;
 st.setStretchParameters({sequenceMs:60,seekWindowMs:15,overlapMs:8,quickSeek:false});
 const lead=Math.ceil(sampleRate*.12),skip=Math.round(lead/tempo),result=channels.map(()=>new Float32Array(frames)),chunk=2048,input=new Float32Array(chunk*2),output=new Float32Array(chunk*2);
 let fed=0,received=0,written=0;const maximumInput=lead+length+sampleRate*2;
 while(written<frames&&fed<maximumInput){
  input.fill(0);const n=Math.min(chunk,maximumInput-fed);
  for(let f=0;f<n;f++){const source=fed+f-lead;if(source>=0&&source<length){const left=channels[0][source],right=(channels[1]||channels[0])[source];if(!Number.isFinite(left)||!Number.isFinite(right))throw Error('Audio contains invalid samples.');input[f*2]=left;input[f*2+1]=right;}}
  st.inputBuffer.putSamples(input,0,n);fed+=n;st.process();
  while(st.outputBuffer.frameCount&&written<frames){const count=Math.min(chunk,st.outputBuffer.frameCount);st.outputBuffer.extract(output,0,count,true);for(let f=0;f<count&&written<frames;f++,received++){if(received<skip)continue;result[0][written]=output[f*2];if(result[1])result[1][written]=output[f*2+1];written++;}}
  progress(Math.min(1,Math.max(0,fed-lead)/length));
 }
 if(written!==frames)throw Error('The stretch processor could not finish this stem. Original audio is unchanged.');
 for(const channel of result)for(const value of channel)if(!Number.isFinite(value))throw Error('The stretch processor produced an invalid sample.');
 return result;
}
export function preparationBudget(buffers,tempo=1){
 if(!Number.isFinite(tempo)||tempo<.5||tempo>2)throw Error('Use a tempo between half and double speed.');
 if(!Array.isArray(buffers)||buffers.length!==4||!buffers.some(Boolean))throw Error('Load four stem slots before preparing a match.');
 let outputs=0,largestInput=0,largestOutput=0;const duration=Math.max(...buffers.filter(Boolean).map(b=>b.duration));
 for(const b of buffers.filter(Boolean)){const sampleRate=b.sampleRate,length=b.length,channels=b.numberOfChannels;if(!Number.isFinite(duration)||duration>480||duration/tempo>480||!Number.isInteger(sampleRate)||sampleRate<8000||sampleRate>96000||!Number.isInteger(length)||length<1||![1,2].includes(channels))throw Error('Matching needs mono/stereo stems up to eight minutes at 8–96 kHz.');const out=Math.round(duration*sampleRate/tempo)*channels*4;outputs+=out;largestOutput=Math.max(largestOutput,out);largestInput=Math.max(largestInput,length*channels*4);}
 const bytes=outputs+largestInput+largestOutput+8*1024*1024;
 if(bytes>MATCH_LIMITS.maxWorkingBytes)throw Error('Preparing these stems needs too much memory. Use a shorter song or lower sample rate.');
 return {bytes,duration,outputDuration:duration/tempo};
}
export function nextBeatTime({now,position,bpm,firstBeat=0,speed=1,lookAhead=.1,sampleRate=48000,duration=Infinity,loop=false}){
 if(![now,position,bpm,firstBeat,speed,lookAhead,sampleRate].every(Number.isFinite)||bpm<20||bpm>300||speed<=0||sampleRate<8000)throw Error('Set a valid beat grid before aligning decks.');
 const period=60/bpm,minimum=position+lookAhead*speed,beat=Math.max(0,Math.ceil((minimum-firstBeat)/period-1e-10));let sourceTime=firstBeat+beat*period;
 if(sourceTime>=duration){if(!loop||!Number.isFinite(duration)||duration<=0||firstBeat>=duration)throw Error('The master ends before another beat. Loop it or cue an earlier position.');const untilWrap=duration-position,afterWrap=Math.max(0,lookAhead*speed-untilWrap),cycles=Math.floor(afterWrap/duration),inCycle=afterWrap-cycles*duration;let gridBeat=firstBeat+Math.max(0,Math.ceil((inCycle-firstBeat)/period-1e-10))*period,wraps=cycles;if(gridBeat>=duration){wraps++;gridBeat=firstBeat;}sourceTime=position+untilWrap+wraps*duration+gridBeat;}
 return Math.ceil((now+(sourceTime-position)/speed)*sampleRate)/sampleRate;
}
export function keyShift(master,follower){if(!master||!follower||!Number.isInteger(master.root)||!Number.isInteger(follower.root)||master.root<0||master.root>11||follower.root<0||follower.root>11||!['major','minor'].includes(master.mode)||master.mode!==follower.mode)return null;return ((master.root-follower.root+18)%12)-6;}
