import {sortedPlaylist} from './playlists.js?v=20261017-playlists-01';
import {DeviceLibrary,prepareStems,estimatePreparationBytes} from './device-library.js?v=20261020-upload-01';
import {FirmwareFlasher,fetchRelease,FLASH_PRESERVATION} from './firmware-flash.js?v=20261003-bonsai-library-01';
import {SongMetadata,sourceTitle,songLabel,exportName} from './song-metadata.js?v=20261003-bonsai-library-01';
import {zip} from './vendor/fflate.js';
import {time,NAMES} from './core.js';
const $=id=>document.getElementById(id);
const download=(data,name,type)=>{const url=URL.createObjectURL(new Blob([data],{type})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),30000);};
const recordingFailure=error=>({1:'Storage could not keep up with recording.',2:'All song slots are occupied.',3:'Song storage is unavailable or incompatible.',4:'The player could not write the recorded audio.',5:'The saved audio or song metadata could not be verified.'})[error]||'The player could not finish this take.';
export function setupLibrary({connection,audio,canUpload,sourceBusy=()=>false,sourceTitle:getSourceTitle=()=>'',notify,stopMonitor}){
 let songs=[],busy=false,connected=false,deleteSong=null,release=null,flashing=false,recordBusy=false,lastRecordState=0,epoch=0,recordedSlot=null,pendingFirmware=null;
 let playlistSupported=false,playlistDirty=false,playlistUnavailable=false,preparation=null;
 const metadata=new SongMetadata();
 const library=new DeviceLibrary(connection,{onProgress:(value,message)=>{$('library-progress').value=value;$('library-status').textContent=message;}});
 function controls(){
  const locked=!connected||busy||flashing||recordBusy;
  $('playlist-save').disabled=locked||!playlistSupported||!playlistDirty||songs.some(s=>!Number.isFinite(s.bpm)||(s.bpm!==0&&(s.bpm<20||s.bpm>400)));
  for(const input of $('playlist-rows').querySelectorAll('input'))input.disabled=locked;
  for(const button of $('playlist-preview').querySelectorAll('button'))button.disabled=locked;

  $('library-refresh').disabled=!connected||busy||flashing;$('library-upload').hidden=!connected;$('send-stems').disabled=!connected||busy||flashing||recordBusy||!canUpload()||!$('upload-slot').value;
  $('upload-slot').disabled=busy||flashing||recordBusy;for(const id of ['demo','upload','import-open','split','load-stems'])$(id).disabled=busy||flashing||sourceBusy();for(const button of $('device-songs').querySelectorAll('button'))button.disabled=busy||flashing||recordBusy||!connected;
  $('connect').disabled=busy||flashing;$('disconnect').disabled=busy||flashing;$('firmware-open').disabled=busy||flashing||recordBusy;$('flash-firmware').disabled=!release||!$('firmware-consent').checked||flashing||busy||recordBusy;
 }
 function render(){
  $('device-songs').replaceChildren();const selected=$('upload-slot').value;$('upload-slot').replaceChildren();
  for(let slot=1;slot<=16;slot++)if(!songs.some(s=>s.slot===slot)){const option=document.createElement('option');option.value=slot;option.textContent=`Empty slot ${slot}`;$('upload-slot').append(option);}
  if([...$('upload-slot').options].some(o=>o.value===selected))$('upload-slot').value=selected;
  for(const song of songs){const row=document.createElement('article');row.className='device-song';const heading=document.createElement('div');heading.className='device-song-heading';const name=document.createElement('strong');name.textContent=songLabel(song);if(song.titleSource==='session-local')name.title='Name saved for this connection. This firmware does not report song names.';else if(song.titleSource==='verified-local')name.title='Name saved in this browser for this player and audio.';const duration=document.createElement('small');duration.textContent=`${song.title?`Song ${song.slot} · `:''}${time(song.frames/48000)} · ${song.present.filter(Boolean).length} ${song.present.filter(Boolean).length===1?'track':'tracks'}`;heading.append(name,duration);const actions=document.createElement('div');actions.className='device-song-actions';
   for(const [label,run,style] of [['Load A',()=>load(0,song)],['Load B',()=>load(1,song)],['Export',()=>exportSong(song)],['Delete',()=>{$('delete-title').textContent=`Delete ${songLabel(song)}${song.title?` (Song ${song.slot})`:''}?`;deleteSong=song;$('delete-dialog').showModal();},'delete-song']]){const button=document.createElement('button');button.textContent=label;button.setAttribute('aria-label',`${label} ${songLabel(song)}${song.title?`, Song ${song.slot}`:''}`);if(style)button.className=style;button.onclick=run;actions.append(button);}row.append(heading,actions);$('device-songs').append(row);
  }
  renderPlaylists();controls();
 }
 function acceptSnapshot(snapshot){
  playlistSupported=snapshot.playlists===1&&snapshot.playlist_state>=0;playlistUnavailable=snapshot.playlists===1&&snapshot.playlist_state<0;playlistDirty=false;
  songs=metadata.decorate(snapshot).map(s=>({...s,bpm:(s.bpm_x100||0)/100,decks:s.decks??3}));
 }
 function renderPlaylistPreview(){
  $('playlist-preview').replaceChildren();
  for(let deck=0;deck<2;deck++){
   const column=document.createElement('section'),heading=document.createElement('h3'),list=document.createElement('ol');heading.textContent=`Deck ${deck?'B':'A'}`;
   const ordered=sortedPlaylist(songs,deck);
   for(const song of ordered){const item=document.createElement('li'),button=document.createElement('button');button.textContent=`${songLabel(song)} · ${song.bpm?`${song.bpm} BPM`:'BPM unknown'}`;button.setAttribute('aria-label',`Load ${songLabel(song)} on Deck ${deck?'B':'A'}`);button.onclick=()=>load(deck,song);item.append(button);list.append(item);}
   column.append(heading,list);if(!ordered.length){const empty=document.createElement('p');empty.textContent='No songs selected.';column.append(empty);}$('playlist-preview').append(column);
  }
 }
 function renderPlaylists(){
  $('device-playlists').hidden=!connected;
  $('playlist-editor').hidden=!playlistSupported;
  $('playlist-help').textContent=playlistSupported?'Choose songs for each deck and enter their source BPM. Lowest BPM comes first; unknown BPM comes last. Save to keep these playlists on the player.':playlistUnavailable?'The player could not read compatible song metadata. Reconnect and refresh; playlist saving is disabled.':'Saved physical playlists are in the 0.4.6 test build. This editor becomes available after installing it. Check Install Bonsai 8 for the published version.';
  $('playlist-rows').replaceChildren();if(!playlistSupported)return;
  for(const song of songs){
   const row=document.createElement('div');row.className='playlist-row';const name=document.createElement('span');name.textContent=songLabel(song);row.append(name);
   const tempo=document.createElement('label'),label=document.createElement('span'),input=document.createElement('input');label.textContent='BPM';input.type='number';input.min='20';input.max='400';input.step='0.01';input.placeholder='Unknown';input.value=song.bpm||'';input.setAttribute('aria-label',`${songLabel(song)} source BPM`);
   input.oninput=()=>{song.bpm=input.value.trim()===''?0:Number(input.value);playlistDirty=true;renderPlaylistPreview();controls();};tempo.append(label,input);row.append(tempo);
   for(let deck=0;deck<2;deck++){const member=document.createElement('label'),check=document.createElement('input'),text=document.createElement('span');check.type='checkbox';check.checked=!!(song.decks&(1<<deck));text.textContent=deck?'B':'A';check.setAttribute('aria-label',`${songLabel(song)} in Deck ${deck?'B':'A'} playlist`);check.onchange=()=>{song.decks=check.checked?song.decks|(1<<deck):song.decks&~(1<<deck);playlistDirty=true;renderPlaylistPreview();controls();};member.append(check,text);row.append(member);}
   $('playlist-rows').append(row);
  }
  renderPlaylistPreview();
 }
 $('playlist-save').onclick=async()=>{
  if(!playlistSupported||!playlistDirty||$('playlist-save').disabled)return;
  const entries=songs.map(({slot,bpm,decks,identity})=>({slot,bpm,decks,identity}));
  await operation(async current=>{await library.savePlaylists(entries);if(!current())return;const snapshot=await connection.library();if(!current())return;acceptSnapshot(snapshot);render();$('library-status').textContent='Playlists saved and verified. Tap FUNCTION to select A or B; hold FUNCTION and use the rocker to browse. Both decks are paused.';});
 };
 async function refresh(){
  if(!connected||busy||flashing)return;const started=epoch;busy=true;controls();$('library-status').textContent='Reading songs…';
  try{const snapshot=await connection.library();if(!connected||epoch!==started)return;acceptSnapshot(snapshot);if(recordedSlot!==null){const recorded=songs.find(song=>song.slot===recordedSlot);if(recorded&&!recorded.title){metadata.published(recorded,'Recorded mix');acceptSnapshot(snapshot);}recordedSlot=null;}render();$('library-status').textContent=songs.length?`${songs.length} of 16 slots used. Load any song into either deck; then press PLAY on that deck.`:'No songs yet. Prepare a song below and upload it to an empty slot.';}catch(error){if(epoch===started)$('library-status').textContent=`${error.message} Song browsing needs Bonsai 8 0.4 or later.`;}finally{busy=false;controls();}
 }
 async function operation(action){if(busy||flashing||recordBusy||!connected)return;const started=epoch;busy=true;controls();stopMonitor();audio.pause();$('library-progress').hidden=false;$('library-progress').value=0;$('library-cancel').hidden=false;$('library-cancel').disabled=true;$('library-cancel').textContent='Keep this tab open and the computer awake';try{await action(()=>connected&&epoch===started);}catch(error){$('library-status').textContent=error.message;notify(error.message);}finally{preparation=null;busy=false;$('library-progress').hidden=true;$('library-cancel').hidden=true;controls();}}
 async function load(deck,song){if(busy||flashing||recordBusy||!connected)return;const started=epoch;busy=true;controls();try{await connection.loadSong(deck,song.slot);if(connected&&epoch===started)$('library-status').textContent=`${songLabel(song)} is cued on Deck ${deck?'B':'A'}. The other deck continues. Press PLAY on the chosen deck.`;}catch(error){notify(error.message);if(epoch===started)$('library-status').textContent=error.message;}finally{busy=false;controls();}}
 async function exportSong(song){await operation(async current=>{const stems=await library.export(song.slot);if(!current())return;const filename=exportName(song);if(stems.length===1)download(stems[0].wav,`${filename}.wav`,'audio/wav');else{const files={};for(const stem of stems)files[`${stem.stem+1}-${NAMES[stem.stem].toLowerCase()}.wav`]=stem.wav;const data=await new Promise((resolve,reject)=>zip(files,{level:0},(e,r)=>e?reject(e):resolve(r)));if(!current())return;download(data,`${filename}-stems.zip`,'application/zip');}$('library-status').textContent='Export ready. Both decks are paused; press PLAY to resume.';});}
 $('delete-confirm').onclick=async()=>{if(!deleteSong||busy||flashing||recordBusy||!connected)return;const song=deleteSong;deleteSong=null;$('delete-dialog').close();await operation(async current=>{await library.remove(song.slot);if(!current())return;metadata.removed(song.slot);songs=songs.filter(s=>s.slot!==song.slot);render();$('library-status').textContent=`Song ${song.slot} removed. Other songs are unchanged. Both decks are paused.`;});};
 $('library-refresh').onclick=refresh;
 $('library-cancel').onclick=()=>{if(!preparation)return;preparation.abort();$('library-cancel').disabled=true;$('library-status').textContent='Preparation cancelled. No song was uploaded.';};
 $('send-stems').onclick=async()=>{const slot=Number($('upload-slot').value);if(!slot||!canUpload()||busy||flashing||recordBusy)return;const title=sourceTitle(getSourceTitle());await operation(async current=>{audio.ensureCapacity([],estimatePreparationBytes(audio.buffers));preparation=new AbortController();const controller=preparation;$('library-cancel').disabled=false;$('library-cancel').textContent='Cancel preparation';$('library-status').textContent='Preparing stereo stems at 24 kHz…';let stems;
 try{stems=await prepareStems(audio.buffers,(p,message)=>{if(!current()||controller.signal.aborted)return;$('library-progress').value=p;if(message)$('library-status').textContent=message;},{signal:controller.signal});}catch(error){if(!controller.signal.aborted)throw error;}
 if(!current())return;if(controller.signal.aborted){$('library-status').textContent='Preparation cancelled. No song was uploaded.';return;}
 preparation=null;$('library-cancel').disabled=true;$('library-cancel').textContent='Keep this tab open and the computer awake';$('library-status').textContent='Checking prepared audio…';const song=await library.upload(slot,stems);if(!current())return;metadata.published(song,title);songs=songs.filter(s=>s.slot!==slot);songs.push({...song,title:title||null,titleSource:title?'session-local':null});songs.sort((a,b)=>a.slot-b.slot);if(playlistSupported){const snapshot=await connection.library();if(!current())return;acceptSnapshot(snapshot);}render();$('library-status').textContent=`${songLabel(songs.find(s=>s.slot===slot))} uploaded and verified. Both decks are paused. Load it into A or B to play.`;});};
 $('firmware-open').onclick=async()=>{if(busy||flashing||recordBusy)return;$('firmware-dialog').showModal();$('firmware-consent').checked=false;release=null;controls();$('firmware-release').textContent='Checking the published release…';$('firmware-status').textContent=FLASH_PRESERVATION;try{release=await fetchRelease();$('firmware-release').textContent=`Bonsai 8 ${release.manifest.version} · ${release.manifest.status==='verified'?'hardware-verified release':'test candidate — hardware verification pending'}. SHA-256 verified.${release.manifest.status==='candidate'?' Eight-stem playback, USB audio and recording are not fully verified.':''}${Array.isArray(release.manifest.known_issues)?' '+release.manifest.known_issues.filter(x=>typeof x==='string').join(' '):''}`;}catch(error){$('firmware-release').textContent=error.message;}controls();};
 $('firmware-consent').onchange=controls;
 $('flash-firmware').onclick=async()=>{if(!release||!$('firmware-consent').checked||flashing||busy||recordBusy)return;flashing=true;controls();const dialog=$('firmware-dialog'),close=dialog.querySelector('[data-close]');close.disabled=true;$('firmware-consent').disabled=true;$('firmware-progress').hidden=false;$('firmware-progress').value=0;stopMonitor();
  try{await connection.disconnect();const flasher=new FirmwareFlasher((progress,message)=>{$('firmware-progress').value=progress;$('firmware-status').textContent=message;});const result=await flasher.flash(release);pendingFirmware=result;$('firmware-status').textContent=`Bonsai 8 ${result.version} transfer acknowledged. Power on with FUNCTION and reconnect to check the running version. Application readback is unavailable; song storage was not written.`;notify(`Bonsai 8 ${result.version} transfer acknowledged. Power on and reconnect to verify its version.`);}catch(error){$('firmware-status').textContent=error.message;notify(error.message);}finally{flashing=false;close.disabled=false;$('firmware-consent').disabled=false;$('firmware-consent').checked=false;controls();}
 };
 $('firmware-dialog').addEventListener('cancel',event=>{if(flashing)event.preventDefault();});
 window.addEventListener('beforeunload',event=>{if(busy||flashing){event.preventDefault();event.returnValue='';}});
 return {
  telemetry(state){
   if(pendingFirmware&&typeof state.firmware==='string'){const expected=`bonsai-8-${pendingFirmware.version}`;if(state.firmware===expected){$('firmware-status').textContent=`The player reports Bonsai 8 ${pendingFirmware.version}. The downloaded file's SHA-256 and updater acknowledgements were checked; flash readback is unavailable.`;pendingFirmware=null;}else if(!pendingFirmware.reported){pendingFirmware.reported=true;const message=`Expected ${expected}, but the player reports ${state.firmware}. The update is not verified.`;$('firmware-status').textContent=message;notify(message);}}
   const recordState=state.record?.state||0;
   if(recordState===4&&[1,2,3].includes(lastRecordState)){recordedSlot=state.record.slot;$('library-status').textContent=`Recording saved in Song ${state.record.slot}.`;setTimeout(()=>refresh(),0);}
   recordBusy=[1,2,3].includes(recordState);
   if(recordState===5&&lastRecordState!==5){
    recordedSlot=null;
    const message=`Recording failed. ${recordingFailure(state.record.error)} Check the library before using this take.`;
    $('library-status').textContent=message;notify(message);
   }else if(recordBusy)$('library-status').textContent=recordState===1?'Recording on the player. Stop recording, then pause both decks to save.':state.playing?.some(Boolean)?'Finishing recording. Pause both decks to save it before managing songs.':'Saving recording… Keep the player powered on.';
   lastRecordState=recordState;controls();
  },
  state(isConnected){if(connected!==isConnected){connected=isConnected;epoch++;if(!connected){preparation?.abort();recordBusy=false;lastRecordState=0;recordedSlot=null;deleteSong=null;metadata.resetSession();songs=[];playlistSupported=false;playlistDirty=false;render();$('library-status').textContent='Connect Bonsai 8 to view its songs.';}}controls();},refresh,controls,get busy(){return busy||flashing;}
 };
}
