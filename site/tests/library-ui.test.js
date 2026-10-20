import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
import * as names from '../public/song-metadata.js';
import {sortedPlaylist} from '../public/playlists.js';
const song=(slot=2)=>({slot,frames:6720,present:[1,1,0,0],title:null});
const receipt=slot=>({...song(slot),audio_id:'a'.repeat(64)});
function harness(){
 class Element {constructor(tag='div'){this.tag=tag;this.children=[];this.textContent='';this.disabled=false;this.hidden=false;this.checked=false;this._value='';}get value(){return this._value||(this.tag==='select'?this.children[0]?.value||'':'');}set value(v){this._value=String(v);}get options(){return this.children;}append(...children){this.children.push(...children);}replaceChildren(...children){this.children=children;this._value='';}querySelectorAll(selector){return this.children.flatMap(c=>[...(selector===c.tag?[c]:[]),...c.querySelectorAll(selector)]);}querySelector(){return this.closeButton??=new Element('button');}setAttribute(key,value){this[key]=value;}showModal(){this.open=true;}close(){this.open=false;}addEventListener(){}click(){downloads.push(this.download);}}
 const elements=new Map(),downloads=[],messages=[],timers=[],store=new Map(),backend={slots:[song()],uploadError:null,removeError:null,flashError:null,loads:[],release:{manifest:{version:'0.4.1',status:'candidate'}},query:null,playlists:0,playlist_state:0,saves:[],saveError:null};
 const document={getElementById(id){if(!elements.has(id))elements.set(id,new Element(id==='upload-slot'?'select':'div'));return elements.get(id);},createElement:tag=>new Element(tag)};
 const connection={async library(){if(backend.query)return backend.query();return {slots:backend.slots,playlists:backend.playlists,playlist_state:backend.playlist_state};},async loadSong(deck,slot){backend.loads.push([deck,slot]);},async disconnect(){ui.state(false);}};
 class Library {async savePlaylists(entries){if(backend.saveError)throw backend.saveError;backend.saves.push(entries);backend.slots=backend.slots.map(s=>{const row=entries.find(r=>r.slot===s.slot);return {...s,bpm_x100:Math.round(row.bpm*100),decks:row.decks};});}async upload(slot){if(backend.uploadError)throw backend.uploadError;backend.slots.push(song(slot));return receipt(slot);}async remove(slot){if(backend.removeError)throw backend.removeError;backend.slots=backend.slots.filter(s=>s.slot!==slot);}async export(){return [{stem:0,wav:new Uint8Array(44)}];}}
 class Metadata extends names.SongMetadata {constructor(){super({store:{getItem:k=>store.get(k),setItem:(k,v)=>store.set(k,v)}});}}
 class Flasher {async flash(){if(backend.flashError)throw backend.flashError;return {version:'0.4.1',sha256:'a'.repeat(64)};}}
 const context=vm.createContext({...names,sortedPlaylist,SongMetadata:Metadata,DeviceLibrary:Library,prepareStems:async(...args)=>backend.prepare?backend.prepare(...args):[],estimatePreparationBytes:()=>42,AbortController,FirmwareFlasher:Flasher,fetchRelease:async()=>backend.release,FLASH_PRESERVATION:'No song formatting; stock-library compatibility is separate.',document,window:{addEventListener(){}},URL:{createObjectURL:()=>'',revokeObjectURL(){}},Blob,Uint8Array,time:()=> '0:00',NAMES:['Vocals','Drums','Bass','Other'],zip:(_files,_options,callback)=>callback(null,new Uint8Array()),setTimeout:fn=>{timers.push(fn);return 1;}});
 vm.runInContext(readFileSync(new URL('../public/library-ui.js',import.meta.url),'utf8').replace(/^import .*;\n/gm,'').replace('export function setupLibrary','function setupLibrary')+'\nglobalThis.create=setupLibrary;',context);
 const ui=context.create({connection,audio:{buffers:[],pause(){},ensureCapacity(_buffers,bytes){backend.reserved=bytes;if(backend.capacityError)throw backend.capacityError;}},canUpload:()=>true,sourceTitle:()=> 'Summer vocal.wav',notify:message=>messages.push(message),stopMonitor(){}});
 const el=id=>document.getElementById(id),rows=()=>el('device-songs').children,title=slot=>rows().find(row=>row.querySelectorAll('button')[0]['aria-label'].includes(`Song ${slot}`))?.children[0].children[0].textContent;
 return {ui,el,backend,messages,downloads,rows,title,async start(){ui.state(true);await ui.refresh();},async upload(slot=3){el('upload-slot').value=slot;await el('send-stems').onclick();},async timers(){while(timers.length)await timers.shift()();}};
}
test('upload names appear only after successful publication and drop honestly on reconnect',async()=>{const h=harness();await h.start();assert.equal(h.title(2),'Song 2');h.backend.uploadError=Error('publication failed');await h.upload();assert.equal(h.rows().length,1);assert.match(h.el('library-status').textContent,/publication failed/);h.backend.uploadError=null;await h.upload();assert.equal(h.title(3),'Summer vocal');await h.ui.refresh();assert.equal(h.title(3),'Summer vocal');h.ui.state(false);assert.equal(h.rows().length,0);h.ui.state(true);await h.ui.refresh();assert.equal(h.title(3),'Song 3');});
test('cancelling preparation stops before opening a device transfer, even if preparation resolves late',async()=>{
 const h=harness();await h.start();let finish,signal;
 h.backend.prepare=(_buffers,_progress,options)=>{signal=options?.signal;return new Promise(resolve=>{finish=resolve;});};
 const pending=h.upload();assert.equal(h.el('library-cancel').disabled,false);assert.match(h.el('library-cancel').textContent,/Cancel preparation/);
 h.el('library-cancel').onclick();assert.equal(signal.aborted,true);finish([]);await pending;
 assert.equal(h.backend.slots.length,1);assert.match(h.el('library-status').textContent,/cancelled/);assert.equal(h.ui.busy,false);
});
test('disconnect cancels preparation and cannot upload into a later connection',async()=>{
 const h=harness();await h.start();let finish,signal;
 h.backend.prepare=(_buffers,_progress,options)=>{signal=options?.signal;return new Promise(resolve=>{finish=resolve;});};
 const pending=h.upload();h.ui.state(false);assert.equal(signal.aborted,true);h.ui.state(true);finish([]);await pending;
 assert.equal(h.backend.slots.length,1);assert.equal(h.ui.busy,false);
});
test('failed deletion retains the row/title; successful deletion removes it and slot reuse is unnamed',async()=>{const h=harness();await h.start();await h.upload();const remove=()=>h.rows().find(row=>row.children[0].children[0].textContent==='Summer vocal').querySelectorAll('button').find(b=>b.textContent==='Delete').onclick();remove();h.backend.removeError=Error('delete was not confirmed');await h.el('delete-confirm').onclick();assert.equal(h.title(3),'Summer vocal');remove();h.backend.removeError=null;await h.el('delete-confirm').onclick();assert.equal(h.title(3),undefined);h.backend.slots.push(song(3));await h.ui.refresh();assert.equal(h.title(3),'Song 3');});
test('a response arriving after disconnect cannot repopulate a previous player library',async()=>{const h=harness();await h.start();let resolve;h.backend.query=()=>new Promise(r=>{resolve=r;});const pending=h.ui.refresh();h.ui.state(false);resolve({slots:[song(10)]});await pending;assert.equal(h.rows().length,0);assert.match(h.el('library-status').textContent,/Connect Bonsai/);assert.equal(h.ui.busy,false);});
test('a newly completed recording gets a mix label and filename; stale saved state does not',async()=>{const h=harness();await h.start();h.ui.telemetry({record:{state:4,slot:2}});await h.timers();assert.equal(h.title(2),'Song 2');h.ui.telemetry({record:{state:1,slot:3}});h.backend.slots.push(song(3));h.ui.telemetry({record:{state:4,slot:3}});await h.timers();assert.equal(h.title(3),'Recorded mix');await h.rows().at(-1).querySelectorAll('button').find(b=>b.textContent==='Export').onclick();assert.deepEqual(h.downloads,['Recorded mix-song-3.wav']);});
test('a failed recording replaces stale recording status, notifies once and leaves the library manageable',async()=>{
 const h=harness();await h.start();
 h.ui.telemetry({record:{state:1,slot:3,error:0},playing:[1,1]});
 assert.match(h.el('library-status').textContent,/Recording on the player/);
 assert.equal(h.rows()[0].querySelectorAll('button')[0].disabled,true);
 h.ui.telemetry({record:{state:5,slot:3,error:1},playing:[1,1]});
 assert.match(h.el('library-status').textContent,/Recording failed/);
 assert.match(h.el('library-status').textContent,/keep up/);
 assert.equal(h.messages.length,1);assert.match(h.messages[0],/Recording failed/);
 assert.equal(h.rows()[0].querySelectorAll('button')[0].disabled,false);
 assert.equal(h.rows().length,1,'failed take must not create a phantom saved song');
 await h.ui.refresh();const refreshed=h.el('library-status').textContent;
 h.ui.telemetry({record:{state:5,slot:3,error:1},playing:[1,1]});
 assert.equal(h.messages.length,1,'status polling must not repeatedly notify the same failure');
 assert.equal(h.el('library-status').textContent,refreshed,'later failure polls must not overwrite library operations');
 h.ui.telemetry({record:{state:1,slot:3,error:0},playing:[1,1]});
 h.ui.telemetry({record:{state:5,slot:3,error:5},playing:[0,0]});
 assert.equal(h.messages.length,2,'a new failed take should notify again');
 assert.match(h.el('library-status').textContent,/verified/);
 assert.doesNotMatch(h.el('library-status').textContent,/saved in Song/);
});
test('unknown recording errors are reported honestly and a later successful take still refreshes',async()=>{
 const h=harness();await h.start();
 h.ui.telemetry({record:{state:3,slot:3,error:0},playing:[0,0]});
 h.ui.telemetry({record:{state:5,slot:3,error:99},playing:[0,0]});
 assert.match(h.el('library-status').textContent,/Recording failed/);
 assert.match(h.el('library-status').textContent,/Check the library/);
 assert.doesNotMatch(h.el('library-status').textContent,/not published|songs.*unchanged|saved in Song/i);
 h.ui.telemetry({record:{state:1,slot:3,error:0}});h.backend.slots.push(song(3));
 h.ui.telemetry({record:{state:4,slot:3,error:0}});await h.timers();
 assert.equal(h.title(3),'Recorded mix');
});
test('flash UI separates transfer acknowledgement from running-version confirmation',async()=>{const h=harness();await h.start();await h.el('firmware-open').onclick();assert.match(h.el('firmware-status').textContent,/stock-library/);h.el('firmware-consent').checked=true;await h.el('flash-firmware').onclick();assert.match(h.el('firmware-status').textContent,/transfer acknowledged/);assert.doesNotMatch(h.el('firmware-status').textContent,/installed/);h.ui.telemetry({firmware:'bonsai-8-0.4.0'});assert.match(h.el('firmware-status').textContent,/update is not verified/);h.ui.telemetry({firmware:'bonsai-8-0.4.1'});assert.match(h.el('firmware-status').textContent,/player reports Bonsai 8 0.4.1/);assert.match(h.el('firmware-status').textContent,/readback is unavailable/);});

test('playlist editing is firmware gated, independently sorted and saved to the physical device',async()=>{
 const h=harness();await h.start();assert.equal(h.el('playlist-editor').hidden,true);assert.equal(h.el('playlist-save').disabled,true);
 h.backend.playlists=1;h.backend.slots=[{...song(1),bpm_x100:12000,decks:3,identity:123},{...song(2),bpm_x100:9000,decks:2,identity:456}];await h.ui.refresh();
 assert.equal(h.el('playlist-editor').hidden,false);
 const inputs=h.el('playlist-rows').querySelectorAll('input');inputs[0].value='85.5';inputs[0].oninput();inputs[2].checked=false;inputs[2].onchange();
 assert.equal(h.el('playlist-save').disabled,false);
 const previews=h.el('playlist-preview').children;assert.match(previews[0].querySelectorAll('button')[0].textContent,/Song 1.*85.5 BPM/);assert.match(previews[1].querySelectorAll('button')[0].textContent,/Song 2.*90 BPM/);
 await previews[1].querySelectorAll('button')[0].onclick();assert.deepEqual(h.backend.loads,[[1,2]]);
 await h.el('playlist-save').onclick();assert.equal(h.backend.saves.length,1);assert.equal(h.backend.saves[0][0].bpm,85.5);assert.equal(h.backend.saves[0][0].decks,1);assert.match(h.el('library-status').textContent,/saved and verified/);assert.equal(h.el('playlist-save').disabled,true);
});
test('invalid BPM, failed saves, recording and disconnect cannot publish stale playlist edits',async()=>{
 const h=harness();h.backend.playlists=1;h.backend.slots=[{...song(),bpm_x100:12000,decks:3,identity:123}];await h.start();
 const input=h.el('playlist-rows').querySelectorAll('input')[0];input.value='999';input.oninput();assert.equal(h.el('playlist-save').disabled,true);
 input.value='100';input.oninput();h.backend.saveError=Error('readback failed');await h.el('playlist-save').onclick();assert.match(h.el('library-status').textContent,/readback failed/);assert.equal(input.value,'100');assert.equal(h.el('playlist-save').disabled,false);
 h.ui.telemetry({record:{state:1}});assert.equal(h.el('playlist-save').disabled,true);assert.equal(input.disabled,true);
 h.ui.state(false);assert.equal(h.el('device-playlists').hidden,true);assert.equal(h.el('playlist-save').disabled,true);assert.equal(h.backend.saves.length,0);
});

test('preparation memory is reserved before decoding or opening transfer',async()=>{
 const h=harness();await h.start();h.backend.capacityError=Error('Not enough memory');let called=false;h.backend.prepare=()=>{called=true;return [];};
 await h.upload();assert.equal(h.backend.reserved,42);assert.equal(called,false);assert.equal(h.backend.slots.length,1);assert.match(h.el('library-status').textContent,/Not enough memory/);
});
