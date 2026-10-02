import {setupLibrary} from './library-ui.js?v=20261002-bonsai-01';
import { AudioEngine } from './audio.js';
import { Device } from './device.js?v=20261002-bonsai-01';
import { NAMES, time, clamp, wav, decodeControls } from './core.js';
import { SP1Connection, selectedDeckControls } from './protocol.js?v=20261002-bonsai-01';
import { LiveSP1Audio, deviceProgress } from './live-audio.js?v=20261002-bonsai-01';
import { DEFAULT_BODY_HEIGHT } from './control-height.js?v=20261002-bonsai-01';
import { MirrorTimeline, MIRROR_KEYS } from './mirror.js?v=20261002-bonsai-01';
import { zip } from './vendor/fflate.js';
const $ = id => document.getElementById(id);
const audio = new AudioEngine(); let device, original, songName='', mode='empty', pitch=0, worker, busy=false, importBusy=false, toastTimer;
const colors=['#9c8273','#899377','#8e9587','#ac9f7f'];
const notify = message => { $('toast').textContent=message;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,6500); };
function fail(e){notify(e.message || String(e));console.error(e);}
const safe = fn => (...args) => Promise.resolve().then(()=>fn(...args)).catch(fail);
$('tracks').innerHTML=NAMES.map((name,i)=>`<div class="track" style="--track:${colors[i]}"><div class="track-heading"><span class="track-number">0${i+1}</span><span class="track-color"></span><label for="gain-${i}" class="track-name" id="track-name-${i}">${name}</label><output class="track-level" id="gain-value-${i}">100%</output><div class="track-buttons"><button id="mute-${i}" aria-label="Mute ${name}" aria-pressed="false">M</button><button id="solo-${i}" aria-label="Solo ${name}" aria-pressed="false">S</button></div></div><div class="track-slider"><canvas class="track-wave" id="wave-${i}" width="280" height="38" aria-label="${name} waveform"></canvas><input type="range" id="gain-${i}" min="0" max="100" value="100" aria-label="${name} volume"></div></div>`).join('');
$('stem-inputs').innerHTML=NAMES.map((name,i)=>`<label class="stem-file-row"><span>0${i+1} / ${name}</span><input id="stem-file-${i}" type="file" accept="audio/*,.flac,.wav,.mp3,.m4a" aria-label="${name} stem"></label>`).join('');
let libraryUI;
function gain(i,v){audio.setGain(i,v);device?.setGain(i,v);$(`gain-${i}`).value=Math.round(v*100);$(`gain-value-${i}`).value=`${Math.round(v*100)}%`;}
function updateMix(){for(let i=0;i<4;i++){gain(i,audio.gains[i]);for(const [type,values] of [['mute',audio.mutes],['solo',audio.solos]]){$(`${type}-${i}`).classList.toggle('active',values[i]);$(`${type}-${i}`).setAttribute('aria-pressed',String(values[i]));}}}
for(let i=0;i<4;i++){$(`gain-${i}`).addEventListener('input',e=>gain(i,Number(e.target.value)/100));$(`mute-${i}`).onclick=()=>{audio.mute(i);updateMix();};$(`solo-${i}`).onclick=()=>{audio.solo(i);updateMix();};}
let previousButtons=null, hardwareState=null;
const liveAudio=new LiveSP1Audio(()=>{$('live-audio-status').textContent='SP–1 audio disconnected.';$('listen-sp1').textContent='Listen here ↗';});
$('listen-sp1').onclick=async()=>{
 if(liveAudio.stream){liveAudio.stop();$('listen-sp1').textContent='Listen here ↗';$('live-audio-status').textContent='Listening stopped. Device progress remains live.';return;}
 $('listen-sp1').disabled=true;
 try{audio.pause();await liveAudio.start();$('listen-sp1').textContent='Stop listening';$('live-audio-status').textContent='Live stereo mix from SP–1 · 48 kHz';}
 catch(error){$('live-audio-status').textContent=error.message;notify(error.message);}
 finally{$('listen-sp1').disabled=!hardwareState?.usb_capture;}
};
const mirrorTimeline=new MirrorTimeline(frame=>{
 if(frame){device?.setHardwareFrame(frame);}
 else {MIRROR_KEYS.forEach(key=>device?.setButton(key,false));device?.setHardwareFrame({faders:null,buttons:{},trackLeds:[0,0,0,0],statusLeds:[0,0,0,0]});$('hardware-status').textContent='Waiting for live controls…';}
});
const meters=new Uint8Array(128), levels=[0,0,0,0];let lastFrame=0;
function frame(){
 mirrorTimeline.tick(performance.now());
 liveAudio.draw($('live-wave'));
 audio.tick();const now=performance.now();if(now-lastFrame<45)return;lastFrame=now;
 const pos=audio.position();$('time-current').textContent=time(pos);if(document.activeElement!==$('seek'))$('seek').value=audio.duration?Math.round(pos/audio.duration*1000):0;
 $('play').textContent=audio.playing?'Ⅱ':'▶';$('play').setAttribute('aria-label',audio.playing?'Pause':'Play');
 for(let i=0;i<4;i++){if(audio.playing&&audio.analysers){audio.analysers[i].getByteTimeDomainData(meters);let sum=0;for(const v of meters)sum+=((v-128)/128)**2;levels[i]=Math.min(1,Math.sqrt(sum/128)*6);}else levels[i]=audio.buffers[i]?.08:0;}
 if(!hardwareState)device?.lights(levels,audio.mutes);
}
try{device=new Device($('stage'),{gain:(i,v)=>{if(!hardwareState)gain(i,v);},down:key=>control(key),up:()=>{},frame,orbit:()=>{$('view').value='custom';}});}catch(e){$('render-error').hidden=false;setInterval(frame,70);console.error(e);}
device?.setBodyHeight(DEFAULT_BODY_HEIGHT);
function resetMix(){audio.mutes.fill(false);audio.solos.fill(false);for(let i=0;i<4;i++)gain(i,1);setPitch(0);audio.setVolume(.75);$('master').value=75;$('master-value').value='75%';updateMix();}
function setPitch(value){pitch=clamp(value,-12,12);$('pitch-value').value=pitch>0?`+${pitch}`:String(pitch);audio.rate(pitch).catch(fail);}
function control(key){
 if(hardwareState){notify('Use the physical player to change its mix. Songs can be loaded from On your player.');return;}
 if(key.startsWith('track')){audio.mute(Number(key.slice(5)));updateMix();}
 else if(key==='play')safe(()=>audio.toggle())();
 else if(key==='forward')setPitch(pitch+1);
 else if(key==='rewind')setPitch(pitch-1);
 else if(key==='function'){$('loop').click();}
 else if(key==='volumeUp'||key==='volumeDown'){audio.setVolume(audio.volume+(key==='volumeUp'?.05:-.05));$('master').value=Math.round(audio.volume*100);$('master-value').value=`${Math.round(audio.volume*100)}%`;}
}
$('play').onclick=safe(()=>audio.toggle());$('pitch-up').onclick=()=>setPitch(pitch+1);$('pitch-down').onclick=()=>setPitch(pitch-1);$('reset-mix').onclick=resetMix;
$('master').oninput=e=>{audio.setVolume(Number(e.target.value)/100);$('master-value').value=`${e.target.value}%`;};
$('seek').oninput=safe(e=>audio.seek(Number(e.target.value)/1000*audio.duration));
$('loop').onclick=()=>{audio.loop=!audio.loop;$('loop').classList.toggle('active',audio.loop);$('loop').setAttribute('aria-pressed',String(audio.loop));};
$('view').onchange=e=>device?.view(e.target.value);
$('view-reset').onclick=()=>{$('view').value='3d';device?.view('3d');};
document.addEventListener('keydown',e=>{if(hardwareState)return;if(['INPUT','TEXTAREA','SELECT','BUTTON'].includes(e.target.tagName)||document.querySelector('dialog[open]')||e.repeat||e.metaKey||e.ctrlKey||e.altKey)return;if(e.code==='Space'){e.preventDefault();safe(()=>audio.toggle())();}if(/^[1-4]$/.test(e.key))control(`track${Number(e.key)-1}`);});
function drawWaves(){for(let i=0;i<4;i++){const c=$(`wave-${i}`).getContext('2d');c.clearRect(0,0,280,38);c.strokeStyle=colors[i];c.lineWidth=2;const data=audio.buffers[i]?.getChannelData(0);c.beginPath();if(!data){c.moveTo(0,19);c.lineTo(280,19);}else{for(let x=0;x<140;x++){let max=0;const start=Math.floor(x/140*data.length),end=Math.floor((x+1)/140*data.length);const step=Math.max(1,Math.floor((end-start)/80));for(let s=start;s<end;s+=step)max=Math.max(max,Math.abs(data[s]));const h=Math.max(1,max*17);c.moveTo(x*2,19-h);c.lineTo(x*2,19+h);}}c.stroke();}}
async function setAudio(buffers,name,nextMode){await audio.load(buffers);songName=name;mode=nextMode;resetMix();$('song-name').textContent=name;$('song-sub').textContent=nextMode==='source'?'Original mix · ready to separate':nextMode==='demo'?'Original synth demo · 4 independent layers':`${buffers.filter(Boolean).length} stems · 44.1 kHz · local playback`;$('stem-state').textContent=nextMode==='source'?'Full mix':`${buffers.filter(Boolean).length} stems`;$('play').disabled=Boolean(hardwareState);$('seek').disabled=Boolean(hardwareState);$('download').disabled=nextMode==='source';$('time-duration').textContent=`/ ${time(audio.duration)}`;for(let i=0;i<4;i++)$(`track-name-${i}`).textContent=nextMode==='source'?(i===0?'Full mix':'Empty'):NAMES[i];drawWaves();libraryUI?.controls();}
function setBusy(value){busy=value;for(const id of ['demo','upload','import-open','split'])$(id).disabled=value;$('cancel-split').hidden=!value;$('split-progress').hidden=!value;if(value)$('split').hidden=true;libraryUI?.controls();}
async function importSong(file){
 if(!file||busy||importBusy)return;if(file.size>200*1024*1024)throw new Error('Choose an audio file smaller than 200 MB.');
 setBusy(true);$('separation').hidden=false;$('separation-title').textContent='Reading audio…';$('separation-info').textContent='Decoding on your device.';$('cancel-split').hidden=true;
 try{const decoded=await audio.decode(file);if(decoded.duration>600)throw new Error('Choose a song under 10 minutes.');if(decoded.numberOfChannels>2)throw new Error('Use a mono or stereo song. Load existing stems with “Have stems?”.');
 original=decoded;await setAudio([decoded,null,null,null],file.name.replace(/\.[^.]+$/,''),'source');
 $('separation-title').textContent='Your four stems are one click away.';$('separation-info').textContent='Audio stays on this device. First run downloads a 158 MB model. Processing may take several minutes.';$('split').hidden=false;
 }catch(e){$('separation-title').textContent='Could not load this file';$('separation-info').textContent=e.message;$('split').hidden=true;throw e;}
 finally{setBusy(false);$('file').value='';}
}
$('upload').onclick=()=>$('file').click();$('file').onchange=safe(e=>importSong(e.target.files[0]));
for(const name of ['dragenter','dragover'])$('upload').addEventListener(name,e=>{e.preventDefault();$('upload').classList.add('dragging');});
for(const name of ['dragleave','drop'])$('upload').addEventListener(name,e=>{e.preventDefault();$('upload').classList.remove('dragging');});
$('upload').addEventListener('drop',safe(e=>importSong(e.dataTransfer.files[0])));
document.addEventListener('dragover',e=>e.preventDefault());document.addEventListener('drop',e=>e.preventDefault());
$('demo').onclick=safe(async()=>{if(busy)return;await audio.init();original=null;$('separation').hidden=true;await setAudio(audio.demo(),'Soft circuit / studio demo','demo');await audio.play();notify('Original synth demo loaded. Slide a fader to hear each layer.');});
$('split').onclick=safe(async()=>{
 if(!original||busy)return;setBusy(true);audio.pause();$('split-progress').value=0;$('separation-title').textContent='Starting separation…';$('separation-info').textContent='Keep this tab open. You can cancel at any time.';
 worker=new Worker('./separate-worker.js',{type:'module'});const localWorker=worker;
 const channels=[original.getChannelData(0).slice(),original.getChannelData(Math.min(1,original.numberOfChannels-1)).slice()];
 const finishError=message=>{if(worker!==localWorker)return;worker.terminate();worker=null;setBusy(false);$('split').hidden=false;$('split').textContent='Retry separation ↗';$('separation-title').textContent='Separation could not finish';$('separation-info').textContent=`${message} You can retry or import existing stems.`;notify('Separation failed. Your original track is still available.');};
 worker.onerror=e=>finishError(e.message||'The separation worker stopped.');
 worker.onmessage=safe(async({data})=>{
 if(worker!==localWorker)return;
 if(data.type==='progress'){$('separation-title').textContent=data.message;$('split-progress').value=data.progress;}
 else if(data.type==='error')finishError(data.message);
 else if(data.type==='done'){
   try{await setAudio(data.stems.map(ch=>audio.fromChannels(ch)),songName,'separated');$('separation-title').textContent='Four stems. All yours.';$('separation-info').textContent='Mix, mute, solo, or export your stems.';$('split').hidden=true;notify('Separation complete. Your four stems are ready.');}
   finally{worker.terminate();worker=null;setBusy(false);}
 }
 });
 worker.postMessage({channels},channels.map(ch=>ch.buffer));
});
$('cancel-split').onclick=()=>{worker?.terminate();worker=null;setBusy(false);$('separation-title').textContent='Separation cancelled';$('separation-info').textContent='Your original track is still loaded.';$('split').hidden=false;};
$('load-stems').onclick=safe(async()=>{
 if(importBusy||busy)return;const files=Array.from({length:4},(_,i)=>$(`stem-file-${i}`).files[0]);if(!files.some(Boolean))throw new Error('Choose at least one stem file.');
 importBusy=true;$('load-stems').disabled=true;
 try{const buffers=[];for(const file of files){if(!file){buffers.push(null);continue;}if(file.size>200*1024*1024)throw new Error('Each stem must be smaller than 200 MB.');const b=await audio.decode(file);if(b.duration>600)throw new Error('Each stem must be under 10 minutes.');buffers.push(b);}
 original=null;$('separation').hidden=true;await setAudio(buffers,'Your stem session','imported');$('import-dialog').close();notify('Your stems are loaded and aligned.');}
 finally{importBusy=false;$('load-stems').disabled=false;libraryUI?.controls();}
});
$('download').onclick=safe(async()=>{
 if(mode==='source'||mode==='empty')return;$('download').disabled=true;
 try{notify('Preparing your WAV files…');const files={};for(let i=0;i<4;i++){const b=audio.buffers[i];if(b)files[`${i+1}-${NAMES[i].toLowerCase()}.wav`]=new Uint8Array(wav(Array.from({length:b.numberOfChannels},(_,c)=>b.getChannelData(c)),b.sampleRate));}
 files['READ-ME.txt']=new TextEncoder().encode('Exported from Virtual SP-1. These are the original separated stems, before mixer gain, mute, solo or pitch. Track order: vocals, drums, bass, other. Audio was processed locally.\n');
 const output=await new Promise((resolve,reject)=>zip(files,{level:0},(e,data)=>e?reject(e):resolve(data)));const url=URL.createObjectURL(new Blob([output],{type:'application/zip'}));const a=document.createElement('a');a.href=url;a.download=`${songName.replace(/[^a-z0-9 _-]/gi,'').trim()||'sp1'}-stems.zip`;a.click();setTimeout(()=>URL.revokeObjectURL(url),30000);notify('Your stem ZIP is ready.');}
 finally{$('download').disabled=false;}
});
const modalLinks=[['connect-open','connect-dialog'],['guide-open','guide-dialog'],['import-open','import-dialog'],['credits-open','credits-dialog']];
for(const [button,dialog] of modalLinks)$(button).onclick=()=>$(dialog).showModal();
for(const dialog of document.querySelectorAll('dialog')){dialog.querySelector('[data-close]').onclick=()=>dialog.close();dialog.addEventListener('click',e=>{if(e.target===dialog){const r=dialog.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)dialog.close();}});}
const hardwareKeys=['forward','volumeUp','rewind','volumeDown','track3','track2','track1','track0','play','function'];
async function mirrorDualDeck(state) {
 const firstStatus=hardwareState?.firmware!==state.firmware;
 const mix=selectedDeckControls(state),deck=mix.deck;hardwareState=state;libraryUI?.telemetry(state);device?.setHardwareMode(true);
 $('stem-state').textContent='Physical controls';for(let i=0;i<4;i++)$(`track-name-${i}`).textContent=NAMES[i];$('hardware-deck').hidden=false;$('mix-heading').textContent=`Physical deck ${deck?'B':'A'}`;
 $('hardware-deck').textContent=`Deck ${deck?'B':'A'} · Song ${state.slots[deck] || '—'} · ${state.playing[deck]?'Playing':'Paused'}`;
 $('connect-label').textContent=`Bonsai 8 · Deck ${deck?'B':'A'}`;
 audio.solos.fill(false);audio.mutes=mix.mutes;
 mix.gains.forEach((value,i)=>gain(i,value));
 updateMix();audio.setVolume(mix.volume);$('master').value=Math.round(audio.volume*100);$('master-value').value=`${Math.round(audio.volume*100)}%`;
 const nextPitch=12*Math.log2(mix.speed);
 if(Math.abs(audio.speed-mix.speed)>.000001){pitch=nextPitch;$('pitch-value').value=Number(pitch.toFixed(1));await audio.rate(pitch);}
 // Hardware monitoring never starts an unrelated browser song.
 audio.pause();
 $('live-player').hidden=false;$('listen-sp1').disabled=state.usb_capture!==1||liveAudio.busy;
 if(!state.usb_capture)$('live-audio-status').textContent='Live audio and song progress need compatible Bonsai 8 firmware.';
 else if(firstStatus&&!liveAudio.stream&&!liveAudio.busy)$('live-audio-status').textContent='Playback positions are live. Choose Listen here for USB audio.';
 for(let k=0;k<2;k++){
  const progress=deviceProgress(state,k);$('live-deck-'+k).textContent=`Deck ${k?'B':'A'} · Song ${state.slots[k]||'—'} · ${state.playing[k]?'Playing':'Paused'}`;
  $('live-time-'+k).value=progress?`${time(progress.seconds)} / ${time(progress.duration)}`:'— / —';
  $('live-progress-'+k).value=progress?.fraction||0;
 }
 // Physical controls and lamps come only from the timestamped hardware feed.
 // Older firmware can mirror mixer settings, but cannot report physical state.
 if(state.mirror!==1)device?.setHardwareFrame({faders:mix.gains,buttons:{},trackLeds:[0,0,0,0],statusLeds:[0,0,0,0]});
}
const hardware = new SP1Connection((f,b,state)=>{
 if(state)return mirrorDualDeck(state);
 const legacy=decodeControls(f,b);let values=legacy.gains;if($('reverse-faders').checked)values.reverse();values.forEach((v,i)=>gain(i,$('invert-faders').checked?1-v:v));
 legacy.buttons.forEach((pressed,i)=>{device?.setButton(hardwareKeys[i],pressed);if(previousButtons&&pressed&&!previousButtons[i])control(hardwareKeys[i]);});previousButtons=legacy.buttons;
},(message,connected=false)=>{$('hardware-status').textContent=message;$('connect-open').classList.toggle('connected',connected);$('connect-label').textContent=connected?'Bonsai 8 connected':'Connect SP–1';$('connect').hidden=connected;$('disconnect').hidden=!connected;libraryUI?.state(connected);for(const id of ['master','pitch-down','pitch-up','reset-mix','play','loop','seek',...Array.from({length:4},(_,i)=>`gain-${i}`),...Array.from({length:4},(_,i)=>`mute-${i}`),...Array.from({length:4},(_,i)=>`solo-${i}`)])$(id).disabled=connected;if(!connected){hardwareState=null;$('mix-heading').textContent='Browser mix';liveAudio.stop();$('live-player').hidden=true;$('listen-sp1').textContent='Listen here ↗';mirrorTimeline.reset();device?.setHardwareMode(false);$('hardware-deck').hidden=true;hardwareKeys.forEach(key=>device?.setButton(key,false));}},packet=>{
 if(mirrorTimeline.push(packet,performance.now()))$('hardware-status').textContent='Mirror resynchronized after a connection delay.';
});
libraryUI=setupLibrary({connection:hardware,audio,sourceBusy:()=>busy||importBusy,canUpload:()=>['demo','imported','separated'].includes(mode)&&!busy&&!importBusy,notify,stopMonitor:()=>{liveAudio.stop();$('listen-sp1').textContent='Listen here ↗';}});
$('connect').onclick=async()=>{
 $('connect').disabled=true;previousButtons=null;
 try{await audio.init();await hardware.connect($('firmware-mode').value);if(hardware.mode==='dual')await libraryUI.refresh();notify(hardware.fullMirror?'Physical lights, faders and button presses are connected.':'Mixer connected. Bonsai 8 is needed to mirror physical lights and button presses.');}
 catch(e){if(e.name!=='NotFoundError'){$('hardware-status').textContent=e.message;notify(e.message);}}
 finally{$('connect').disabled=false;}
};
$('disconnect').onclick=safe(()=>hardware.disconnect());
navigator.serial?.addEventListener('disconnect',()=>{if(hardware.port)safe(()=>hardware.disconnect())();});
if(!navigator.serial){$('hardware-status').textContent='Open this page in Chrome or Edge on a computer to connect your SP–1.';$('connect').disabled=true;}
drawWaves();updateMix();
