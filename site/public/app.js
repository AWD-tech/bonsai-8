import {setupLibrary} from './library-ui.js?v=20261003-recording-library-02';
import { AudioEngine } from './audio.js?v=20261003-two-decks-01';
import { setupBrowserMixer, exportSnapshot } from './browser-mixer.js?v=20261003-layout-02';
import { setupMatching } from './match.js?v=20261003-match-01';
import { Device } from './device.js?v=20261002-bonsai-01';
import { NAMES, time, clamp, wav, decodeControls } from './core.js';
import { SP1Connection, selectedDeckControls } from './protocol.js?v=20261003-load-ack-02';
import { LiveSP1Audio, deviceProgress } from './live-audio.js?v=20261003-monitor-cancel-04';
import { DEFAULT_BODY_HEIGHT } from './control-height.js?v=20261002-bonsai-01';
import { MirrorTimeline, MIRROR_KEYS } from './mirror.js?v=20261002-bonsai-01';
import { zip } from './vendor/fflate.js';
const $ = id => document.getElementById(id);
const audio = new AudioEngine(); let device, original, songName='', mode='empty', pitch=0, worker, busy=false, importBusy=false, exportBusy=false, toastTimer;
const colors=['#9c8273','#899377','#8e9587','#ac9f7f'];
const notify = message => { $('toast').textContent=message;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,6500); };
function fail(e){notify(e.message || String(e));console.error(e);}
const safe = fn => (...args) => Promise.resolve().then(()=>fn(...args)).catch(fail);
$('tracks').innerHTML=NAMES.map((name,i)=>`<div class="track" style="--track:${colors[i]}"><div class="track-heading"><span class="track-number">0${i+1}</span><span class="track-color"></span><label for="gain-${i}" class="track-name" id="track-name-${i}">${name}</label><output class="track-level" id="gain-value-${i}">100%</output><div class="track-buttons"><button id="mute-${i}" aria-label="Mute ${name}" aria-pressed="false">M</button><button id="solo-${i}" aria-label="Solo ${name}" aria-pressed="false">S</button></div></div><div class="track-slider"><canvas class="track-wave" id="wave-${i}" width="280" height="38" aria-label="${name} waveform"></canvas><input type="range" id="gain-${i}" min="0" max="100" value="100" aria-label="${name} volume"></div></div>`).join('');
$('stem-inputs').innerHTML=NAMES.map((name,i)=>`<label class="stem-file-row"><span>0${i+1} / ${name}</span><input id="stem-file-${i}" type="file" accept="audio/*,.flac,.wav,.mp3,.m4a" aria-label="${name} stem"></label>`).join('');
let libraryUI, browserMixer, matching; let browserConnected=false, matchingBusy=false;
function gain(i,v){if(!browserConnected&&!hardwareState)audio.setGain(i,v);device?.setGain(i,v);$(`gain-${i}`).value=Math.round(v*100);$(`gain-value-${i}`).value=`${Math.round(v*100)}%`;}
function updateMix(){for(let i=0;i<4;i++){gain(i,audio.gains[i]);for(const [type,values] of [['mute',audio.mutes],['solo',audio.solos]]){$(`${type}-${i}`).classList.toggle('active',values[i]);$(`${type}-${i}`).setAttribute('aria-pressed',String(values[i]));}}}
for(let i=0;i<4;i++){$(`gain-${i}`).addEventListener('input',e=>gain(i,Number(e.target.value)/100));$(`mute-${i}`).onclick=()=>{audio.mute(i);updateMix();};$(`solo-${i}`).onclick=()=>{audio.solo(i);updateMix();};}
let previousButtons=null, hardwareState=null, monitorRequest=0;
const liveAudio=new LiveSP1Audio(()=>{++monitorRequest;$('live-audio-status').textContent='SP–1 audio disconnected.';$('listen-sp1').textContent='Listen here';updateListenAvailability();});
function updateListenAvailability(){$('listen-sp1').disabled=hardwareState?.usb_capture!==1||liveAudio.busy;}
function stopLiveMonitor(){++monitorRequest;liveAudio.stop();$('listen-sp1').textContent='Listen here';updateListenAvailability();}
$('listen-sp1').onclick=async()=>{
 if(liveAudio.busy||hardwareState?.usb_capture!==1)return;
 if(liveAudio.stream){stopLiveMonitor();$('live-audio-status').textContent='Listening stopped. Device progress remains live.';return;}
 const request=++monitorRequest;
 $('listen-sp1').disabled=true;
 try{audio.pauseAll();await liveAudio.start();if(request!==monitorRequest||!liveAudio.stream)return;$('listen-sp1').textContent='Stop listening';$('live-audio-status').textContent='Live stereo mix from SP–1 · 48 kHz';}
 catch(error){if(request!==monitorRequest||error.name==='AbortError')return;$('live-audio-status').textContent=error.message;notify(error.message);}
 finally{if(request===monitorRequest)updateListenAvailability();}
};
const mirrorTimeline=new MirrorTimeline(frame=>{
 if(frame){device?.setHardwareFrame(frame);}
 else {MIRROR_KEYS.forEach(key=>device?.setButton(key,false));device?.setHardwareFrame({faders:null,buttons:{},trackLeds:[0,0,0,0],statusLeds:[0,0,0,0]});$('hardware-status').textContent='Waiting for live controls…';}
});
const meters=new Uint8Array(128), levels=[0,0,0,0];let lastFrame=0;
function frame(){
 mirrorTimeline.tick(performance.now());
 liveAudio.draw($('live-wave'));
 audio.tick();const now=performance.now();if(now-lastFrame<45)return;lastFrame=now;browserMixer?.sync();$('load-deck').disabled=browserConnected||busy||importBusy||Boolean(matching?.busy);if(Boolean(matching?.busy)!==matchingBusy){matchingBusy=Boolean(matching?.busy);for(const id of ['demo','upload','import-open','split'])$(id).disabled=busy||importBusy||matchingBusy;libraryUI?.controls();}
 const pos=audio.position();$('time-current').textContent=time(pos);if(document.activeElement!==$('seek'))$('seek').value=audio.duration?Math.round(pos/audio.duration*1000):0;
 $('play').classList.toggle('is-playing',audio.playing);$('play').setAttribute('aria-label',audio.playing?'Pause':'Play');
 for(let i=0;i<4;i++){if(audio.playing&&audio.analysers){audio.analysers[i].getByteTimeDomainData(meters);let sum=0;for(const v of meters)sum+=((v-128)/128)**2;levels[i]=Math.min(1,Math.sqrt(sum/128)*6);}else levels[i]=audio.buffers[i]?.08:0;}
 if(!hardwareState)device?.lights(levels,audio.mutes);
}
try{device=new Device($('stage'),{gain:(i,v)=>{if(!browserConnected)gain(i,v);},down:key=>control(key),up:()=>{},frame,orbit:()=>{$('view').value='custom';}});}catch(e){$('render-error').hidden=false;setInterval(frame,70);console.error(e);}
device?.setBodyHeight(DEFAULT_BODY_HEIGHT);
function resetMix(){audio.resetDeck().catch(fail);audio.setVolume(.75);$('master').value=75;$('master-value').value='75%';pitch=0;$('pitch-value').value='0';updateMix();browserMixer?.sync();}
function setPitch(value){pitch=clamp(value,-12,12);$('pitch-value').value=pitch>0?`+${pitch}`:String(pitch);audio.rate(pitch).catch(fail);}
function control(key){
 if(browserConnected){notify('Use the physical player to change its mix. Songs can be loaded from On your player.');return;}
 if(key.startsWith('track')){audio.mute(Number(key.slice(5)));updateMix();}
 else if(key==='play')safe(()=>audio.toggle())();
 else if(key==='forward')setPitch(pitch+1);
 else if(key==='rewind')setPitch(pitch-1);
 else if(key==='function'){browserMixer.select(audio.selected^1);}
 else if(key==='volumeUp'||key==='volumeDown'){audio.setVolume(audio.volume+(key==='volumeUp'?.05:-.05));$('master').value=Math.round(audio.volume*100);$('master-value').value=`${Math.round(audio.volume*100)}%`;}
}
$('play').onclick=safe(()=>audio.toggle());$('pitch-up').onclick=()=>setPitch(pitch+1);$('pitch-down').onclick=()=>setPitch(pitch-1);$('reset-mix').onclick=resetMix;
$('master').oninput=e=>{audio.setVolume(Number(e.target.value)/100);$('master-value').value=`${e.target.value}%`;};
$('seek').oninput=safe(e=>audio.seek(Number(e.target.value)/1000*audio.duration));
$('loop').onclick=()=>{audio.loop=!audio.loop;$('loop').classList.toggle('active',audio.loop);$('loop').setAttribute('aria-pressed',String(audio.loop));};
$('view').onchange=e=>device?.view(e.target.value);
$('view-reset').onclick=()=>{$('view').value='3d';device?.view('3d');};
document.addEventListener('keydown',e=>{if(browserConnected)return;if(['INPUT','TEXTAREA','SELECT','BUTTON'].includes(e.target.tagName)||document.querySelector('dialog[open]')||e.repeat||e.metaKey||e.ctrlKey||e.altKey)return;if(e.code==='Space'){e.preventDefault();safe(()=>e.shiftKey?(audio.decks.every(deck=>deck.playing)?audio.pauseAll():audio.playBoth()):audio.toggle())();}if(e.key.toLowerCase()==='a')browserMixer.select(0);if(e.key.toLowerCase()==='b')browserMixer.select(1);if(/^[1-4]$/.test(e.key))control(`track${Number(e.key)-1}`);});
function drawWaves(){for(let i=0;i<4;i++){const c=$(`wave-${i}`).getContext('2d');c.clearRect(0,0,280,38);c.strokeStyle=colors[i];c.lineWidth=2;const data=audio.buffers[i]?.getChannelData(0);c.beginPath();if(!data){c.moveTo(0,19);c.lineTo(280,19);}else{for(let x=0;x<140;x++){let max=0;const start=Math.floor(x/140*data.length),end=Math.floor((x+1)/140*data.length);const step=Math.max(1,Math.floor((end-start)/80));for(let s=start;s<end;s+=step)max=Math.max(max,Math.abs(data[s]));const h=Math.max(1,max*17);c.moveTo(x*2,19-h);c.lineTo(x*2,19+h);}}c.stroke();}}
function showRateDisplay(speed,preservesPitch=false,physical=false){
 const value=Number((preservesPitch?speed*100:12*Math.log2(speed)).toFixed(1));
 $('pitch-label').textContent=preservesPitch?'Deck tempo':'Speed / pitch';
 $('pitch-unit').textContent=preservesPitch?'%':'st';
 $('pitch-value').value=(!preservesPitch&&!physical&&value>0?'+':'')+value;
 $('pitch-down').setAttribute('aria-label',preservesPitch?'Decrease deck tempo':'Pitch down one semitone');
 $('pitch-up').setAttribute('aria-label',preservesPitch?'Increase deck tempo':'Pitch up one semitone');
 $('mix-help').textContent=preservesPitch?'This connected player preserves pitch while changing deck tempo. Adjust it on the physical player.':'The wheel changes speed and pitch together. Use Match songs to prepare tempo and independent stem pitch changes.';
}
function showBrowserDeck(){
 const deck=audio.deck(),media=deck.media;songName=media.name;mode=media.mode;original=media.original;pitch=12*Math.log2(deck.speed);
 if(browserConnected)return;
 $('mix-heading').textContent=`Deck ${audio.selected?'B':'A'} stems`;$('load-deck').value=String(audio.selected);
 $('song-name').textContent=songName||'No song loaded';
 $('song-sub').textContent=mode==='empty'?'Load music above or try both demos':mode==='source'?'Original mix · ready to separate':mode==='demo'?'Original synth demo · 4 independent layers':`${deck.buffers.filter(Boolean).length} stems · local playback`;
 $('stem-state').textContent=mode==='empty'?'No stems':mode==='source'?'Full mix':`${deck.buffers.filter(Boolean).length} stems`;
 $('play').disabled=!deck.duration;$('seek').disabled=!deck.duration;$('download').disabled=exportBusy||['source','empty'].includes(mode);
 $('time-duration').textContent=`/ ${time(deck.duration)}`;showRateDisplay(deck.speed);
 $('loop').disabled=!deck.duration;$('loop').classList.toggle('active',deck.loop);$('loop').setAttribute('aria-pressed',String(deck.loop));
 $('master').value=Math.round(audio.volume*100);$('master-value').value=`${Math.round(audio.volume*100)}%`;
 for(let i=0;i<4;i++)$(`track-name-${i}`).textContent=mode==='source'?(i===0?'Full mix':'Empty'):NAMES[i];
 $('separation').hidden=mode!=='source'||!original;
 if(mode==='source'&&!busy){$('separation-title').textContent='Your four stems are one click away.';$('separation-info').textContent='Audio stays on this device. First run downloads a 158 MB model. Processing may take several minutes.';$('split').hidden=false;}
 updateMix();drawWaves();libraryUI?.controls();matching?.refresh();
}
async function setAudio(buffers,name,nextMode,index=audio.selected,source=null){
 const existing=audio.deck(index).media.libraryId;
 await audio.loadDeck(index,buffers,{name,mode:nextMode,original:source});await audio.resetDeck(index);
 browserMixer.add(index,buffers,{name,mode:nextMode,original:source},nextMode==='separated'?existing:null);
 if(index===audio.selected)showBrowserDeck();else matching?.refresh();libraryUI?.controls();
}
browserMixer=setupBrowserMixer({audio,root:$('browser-mixer'),notify,isBusy:()=>busy||importBusy||Boolean(matching?.busy),onSelect:showBrowserDeck,onLoad:index=>{if(index===audio.selected)showBrowserDeck();else matching?.refresh();},onDemo:async()=>{
 await audio.init();await setAudio(audio.demo(),'Soft circuit / demo A','demo',0);await setAudio(audio.demo({variant:1}),'Upper circuit / demo B','demo',1);await audio.playBoth();notify('Two demo songs are playing. Select A or B to control its four stems.');
}});
matching=setupMatching({audio,container:$('browser-matching'),isHardwareConnected:()=>browserConnected,notify,onPrepared:({index,name,buffers})=>{
 const deck=audio.deck(index);deck.media={...deck.media,name,mode:'prepared'};
 browserMixer.add(index,buffers,deck.media);
 browserMixer.select(index);libraryUI?.controls();setTimeout(()=>{browserMixer.sync();libraryUI?.controls();},0);
}});
function setBusy(value){busy=value;for(const id of ['demo','upload','import-open','split'])$(id).disabled=value;$('cancel-split').hidden=!value;$('split-progress').hidden=!value;if(value)$('split').hidden=true;browserMixer?.sync();libraryUI?.controls();}
async function importSong(file){
 if(!file||busy||importBusy||matching?.busy)return;const targetDeck=audio.selected;if(file.size>200*1024*1024)throw new Error('Choose an audio file smaller than 200 MB.');
 setBusy(true);$('separation').hidden=false;$('separation-title').textContent='Reading audio…';$('separation-info').textContent='Decoding on your device.';$('cancel-split').hidden=true;
 try{const decoded=await audio.decode(file);if(decoded.duration>600)throw new Error('Choose a song under 10 minutes.');if(decoded.numberOfChannels>2)throw new Error('Use a mono or stereo song. Load existing stems with “Have stems?”.');
 await setAudio([decoded,null,null,null],file.name.replace(/\.[^.]+$/,''),'source',targetDeck,decoded);
 $('separation-title').textContent='Your four stems are one click away.';$('separation-info').textContent='Audio stays on this device. First run downloads a 158 MB model. Processing may take several minutes.';$('split').hidden=false;
 }catch(e){$('separation-title').textContent='Could not load this file';$('separation-info').textContent=e.message;$('split').hidden=true;throw e;}
 finally{setBusy(false);$('file').value='';}
}
$('upload').onclick=()=>$('file').click();$('file').onchange=safe(e=>importSong(e.target.files[0]));
for(const name of ['dragenter','dragover'])$('upload').addEventListener(name,e=>{e.preventDefault();$('upload').classList.add('dragging');});
for(const name of ['dragleave','drop'])$('upload').addEventListener(name,e=>{e.preventDefault();$('upload').classList.remove('dragging');});
$('upload').addEventListener('drop',safe(e=>importSong(e.dataTransfer.files[0])));
document.addEventListener('dragover',e=>e.preventDefault());document.addEventListener('drop',e=>e.preventDefault());
$('demo').onclick=safe(async()=>{if(busy||importBusy||matching?.busy)return;await audio.init();original=null;$('separation').hidden=true;await setAudio(audio.demo(),'Soft circuit / studio demo','demo');await audio.play();notify('Original synth demo loaded. Slide a fader to hear each layer.');});
$('split').onclick=safe(async()=>{
 if(!original||busy||matching?.busy)return;const targetDeck=audio.selected,sourceAudio=original,sourceName=songName;audio.ensureCapacity([],sourceAudio.length*2*4);setBusy(true);audio.pauseDeck(targetDeck);$('split-progress').value=0;$('separation-title').textContent='Starting separation…';$('separation-info').textContent='Keep this tab open. You can cancel at any time.';
 worker=new Worker('./separate-worker.js',{type:'module'});const localWorker=worker;
 const channels=[sourceAudio.getChannelData(0).slice(),sourceAudio.getChannelData(Math.min(1,sourceAudio.numberOfChannels-1)).slice()];
 const finishError=message=>{if(worker!==localWorker)return;worker.terminate();worker=null;setBusy(false);$('split').hidden=false;$('split').textContent='Retry separation';$('separation-title').textContent='Separation could not finish';$('separation-info').textContent=`${message} You can retry or import existing stems.`;notify('Separation failed. Your original track is still available.');};
 worker.onerror=e=>finishError(e.message||'The separation worker stopped.');
 worker.onmessage=safe(async({data})=>{
 if(worker!==localWorker)return;
 if(data.type==='progress'){$('separation-title').textContent=data.message;$('split-progress').value=data.progress;}
 else if(data.type==='error')finishError(data.message);
 else if(data.type==='done'){
   try{audio.ensureCapacity([],data.stems.reduce((bytes,channels)=>bytes+channels.reduce((sum,channel)=>sum+channel.length*4,0),0));await setAudio(data.stems.map(ch=>audio.fromChannels(ch)),sourceName,'separated',targetDeck,sourceAudio);$('separation-title').textContent='Four stems. All yours.';$('separation-info').textContent='Mix, mute, solo, or export your stems.';$('split').hidden=true;notify('Separation complete. Your four stems are ready.');}
   catch(error){finishError(error.message||String(error));}
   finally{localWorker.terminate();if(worker===localWorker)worker=null;setBusy(false);}
 }
 });
 worker.postMessage({channels},channels.map(ch=>ch.buffer));
});
$('cancel-split').onclick=()=>{worker?.terminate();worker=null;setBusy(false);$('separation-title').textContent='Separation cancelled';$('separation-info').textContent='Your original track is still loaded.';$('split').hidden=false;};
$('load-stems').onclick=safe(async()=>{
 if(importBusy||busy||matching?.busy)return;const targetDeck=audio.selected;const files=Array.from({length:4},(_,i)=>$(`stem-file-${i}`).files[0]);if(!files.some(Boolean))throw new Error('Choose at least one stem file.');
 importBusy=true;browserMixer.sync();$('load-stems').disabled=true;
 try{const buffers=[];for(const file of files){if(!file){buffers.push(null);continue;}if(file.size>200*1024*1024)throw new Error('Each stem must be smaller than 200 MB.');const b=await audio.decode(file);if(b.duration>600)throw new Error('Each stem must be under 10 minutes.');buffers.push(b);}
 original=null;$('separation').hidden=true;await setAudio(buffers,'Your stem session','imported',targetDeck);$('import-dialog').close();notify('Your stems are loaded and aligned.');}
 finally{importBusy=false;browserMixer.sync();$('load-stems').disabled=false;libraryUI?.controls();}
});
$('download').onclick=safe(async()=>{
 if(exportBusy||mode==='source'||mode==='empty')return;const exported=exportSnapshot(audio);audio.ensureCapacity([],exported.buffers.reduce((bytes,buffer)=>bytes+(buffer?44+buffer.length*buffer.numberOfChannels*2:0),0)*2);exportBusy=true;$('download').disabled=true;
 try{notify('Preparing your WAV files…');const files={};for(let i=0;i<4;i++){const b=exported.buffers[i];if(b)files[`${i+1}-${NAMES[i].toLowerCase()}.wav`]=new Uint8Array(wav(Array.from({length:b.numberOfChannels},(_,c)=>b.getChannelData(c)),b.sampleRate));}
 files['READ-ME.txt']=new TextEncoder().encode(`Exported from Bonsai 8. ${exported.mode==='prepared'?'These stems include the prepared tempo and per-stem pitch changes.':'These are the source stems.'} Browser mixer gain, mute, solo, deck level and tape-speed moves are not included. Track order: vocals, drums, bass, other. Audio was processed locally.\n`);
 const output=await new Promise((resolve,reject)=>zip(files,{level:0},(e,data)=>e?reject(e):resolve(data)));const url=URL.createObjectURL(new Blob([output],{type:'application/zip'}));const a=document.createElement('a');a.href=url;a.download=`${exported.name.replace(/[^a-z0-9 _-]/gi,'').trim()||'sp1'}-stems.zip`;a.click();setTimeout(()=>URL.revokeObjectURL(url),30000);notify('Your stem ZIP is ready.');}
 finally{exportBusy=false;$('download').disabled=['source','empty'].includes(mode);}
});
$('load-deck').onchange=e=>{if(browserConnected||busy||importBusy||matching?.busy){e.target.value=String(audio.selected);return;}browserMixer.select(Number(e.target.value));};
const modalLinks=[['compact-connect','connect-dialog'],['connect-open','connect-dialog'],['import-open','import-dialog'],['credits-open','credits-dialog']];
for(const [button,dialog] of modalLinks)$(button).onclick=()=>$(dialog).showModal();
for(const dialog of document.querySelectorAll('dialog')){dialog.querySelector('[data-close]').onclick=()=>dialog.close();dialog.addEventListener('click',e=>{if(e.target===dialog){const r=dialog.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)dialog.close();}});}
const hardwareKeys=['forward','volumeUp','rewind','volumeDown','track3','track2','track1','track0','play','function'];
function showHardwareSync(state){
 const readout=$('hardware-sync');let text='';
 const pair=(key,lo,hi)=>Array.isArray(state?.[key])&&state[key].length===2&&state[key].every(v=>Number.isInteger(v)&&v>=lo&&v<=hi);
 if(state?.pitch_preserving===1&&[0,1].includes(state.deck)&&pair('grid_bpm',0,300000)&&pair('grid_valid',0,1)&&pair('sync',0,3)&&pair('sync_error',0,9)&&pair('tap_count',0,4)&&state.grid_valid.every((valid,k)=>!valid||state.grid_bpm[k]>=20000)){
  const k=state.deck,error=state.sync_error[k],taps=state.tap_count[k];
  const source=state.grid_valid[k]?`Source ${Number((state.grid_bpm[k]/1000).toFixed(1))} BPM`:'Source BPM unknown';
  const guidance={2:'Tap four steady beats for both songs on the player.',3:'Start both decks, then retry sync.',4:'Choose songs closer in tempo, then retry sync.',5:'Song changed; tap its beat again, then retry sync.',6:'Tap four steady beats again while the song plays.',7:'Playback moved; retry sync on the player.',8:'Enable sync on the selected deck first.',9:'Retry on the player.'};
  let status;
  if(error>=2)status=`${error===8?'Manual tempo':'Sync unavailable'}. ${guidance[error]}`;
  else if(taps>0&&taps<4)status=`Tap ${taps}/4. Keep tapping steady beats on the player.`;
  else if(error===1)status='Waiting for the player.';
  else if(state.sync[k]>0&&state.sync[k]<3&&!state.grid_valid.every(Boolean))status='Sync unavailable. Tap beats for both songs on the player.';
  else status=['Manual tempo','Aligning beats','Beat locked','Sync unavailable. Retry on the player.'][state.sync[k]];
  text=`Deck ${k?'B':'A'} · ${source} · ${status}`;
 }
 // Unchanged telemetry must not repeatedly announce the same live status.
 if(readout.textContent!==text)readout.textContent=text;
 readout.hidden=!text;
}
async function mirrorDualDeck(state) {
 const firstStatus=hardwareState?.firmware!==state.firmware;
 const mix=selectedDeckControls(state),deck=mix.deck;hardwareState=state;libraryUI?.telemetry(state);device?.setHardwareMode(true);
 $('stem-state').textContent='Physical controls';for(let i=0;i<4;i++)$(`track-name-${i}`).textContent=NAMES[i];$('hardware-deck').hidden=false;$('mix-heading').textContent=`Physical deck ${deck?'B':'A'}`;
 $('song-name').textContent=`Song ${state.slots[deck]||'—'}`;$('song-sub').textContent='Live controls from your player';
 $('hardware-deck').textContent=`Deck ${deck?'B':'A'} · Song ${state.slots[deck] || '—'} · ${state.playing[deck]?'Playing':'Paused'}`;showHardwareSync(state);
 $('connect-label').textContent=`Bonsai 8 · Deck ${deck?'B':'A'}`;
 mix.gains.forEach((value,i)=>gain(i,value));
 for(let i=0;i<4;i++){for(const type of ['mute','solo']){const value=type==='mute'&&mix.mutes[i];$(`${type}-${i}`).classList.toggle('active',value);$(`${type}-${i}`).setAttribute('aria-pressed',String(value));}}
 $('master').value=Math.round(mix.volume*100);$('master-value').value=`${Math.round(mix.volume*100)}%`;
 // Only an explicit runtime capability changes speed from tape pitch to tempo.
 showRateDisplay(mix.speed,state.pitch_preserving===1,true);
 // Hardware monitoring never starts an unrelated browser song.
 audio.pauseAll();
 $('live-player').hidden=false;updateListenAvailability();
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
},(message,connected=false)=>{browserConnected=connected;browserMixer.connected(connected);matching?.refresh();$('hardware-status').textContent=message;$('connect-open').classList.toggle('connected',connected);$('connect-label').textContent=connected?'Bonsai 8 connected':'Connect SP–1';$('compact-connect-label').textContent=connected?'Player connected':'Connect player';$('compact-connect').classList.toggle('connected',connected);$('load-deck').disabled=connected;$('connect').hidden=connected;$('disconnect').hidden=!connected;libraryUI?.state(connected);for(const id of ['master','pitch-down','pitch-up','reset-mix','play','loop','seek',...Array.from({length:4},(_,i)=>`gain-${i}`),...Array.from({length:4},(_,i)=>`mute-${i}`),...Array.from({length:4},(_,i)=>`solo-${i}`)])$(id).disabled=connected;if(!connected){hardwareState=null;showHardwareSync(null);$('mix-heading').textContent='Browser mix';stopLiveMonitor();$('live-player').hidden=true;mirrorTimeline.reset();device?.setHardwareMode(false);$('hardware-deck').hidden=true;hardwareKeys.forEach(key=>device?.setButton(key,false));showBrowserDeck();}},packet=>{
 if(mirrorTimeline.push(packet,performance.now()))$('hardware-status').textContent='Mirror resynchronized after a connection delay.';
});
libraryUI=setupLibrary({connection:hardware,audio,sourceTitle:()=>audio.deck().media.name||songName,sourceBusy:()=>busy||importBusy||Boolean(matching?.busy),canUpload:()=>['demo','imported','separated','prepared'].includes(mode)&&!busy&&!importBusy&&!matching?.busy,notify,stopMonitor:stopLiveMonitor});
$('connect').onclick=async()=>{
 $('connect').disabled=true;previousButtons=null;
 try{await audio.init();audio.pauseAll();await hardware.connect($('firmware-mode').value);if(hardware.mode==='dual')await libraryUI.refresh();notify(hardware.fullMirror?'Physical lights, faders and button presses are connected.':'Mixer connected. Bonsai 8 is needed to mirror physical lights and button presses.');}
 catch(e){if(e.name!=='NotFoundError'){$('hardware-status').textContent=e.message;notify(e.message);}}
 finally{$('connect').disabled=false;}
};
$('disconnect').onclick=safe(()=>hardware.disconnect());
navigator.serial?.addEventListener('disconnect',()=>{if(hardware.port)safe(()=>hardware.disconnect())();});
if(!navigator.serial){$('hardware-status').textContent='Open this page in Chrome or Edge on a computer to connect your SP–1.';$('connect').disabled=true;}
showBrowserDeck();
