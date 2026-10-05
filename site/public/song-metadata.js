// Titles are local annotations, never evidence of what occupies a device slot.
// Reconnect persistence requires DDLIB? to supply device_id (unique MCU identity)
// and each occupied slot's audio_id (SHA-256 of the uploaded encoded stems:
// concatenate LE uint32 native frame count + all encoded sector bytes, per stem).
// Firmware must recompute/change audio_id on every replacement or recording and
// publish it atomically with the song index. VID/PID, duration, and slot number
// alone are not identities. Until that protocol exists, names last one session.
const KEY='bonsai8-song-titles-v1';
const hash=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value)?value:null;
const device=value=>typeof value==='string'&&/^[a-zA-Z0-9:_-]{8,128}$/.test(value)?value:null;
const validSlot=slot=>Number.isInteger(slot)&&slot>=1&&slot<=16;
const shape=song=>JSON.stringify([song.frames,song.present]);
function defaultStore(){try{return globalThis.localStorage;}catch{return null;}}
export function cleanTitle(value){return typeof value==='string'?value.normalize('NFC').replace(/[\u0000-\u001f\u007f-\u009f]/g,' ').replace(/\s+/g,' ').trim().slice(0,120):'';}
export function sourceTitle(value){return cleanTitle(value).replace(/\.(?:wav|mp3|aiff?|flac|m4a|aac|ogg|opus|zip)$/i,'').trim();}
export function songLabel(song){return cleanTitle(song.title)||`Song ${song.slot}`;}
export function exportName(song){const title=cleanTitle(song.title).replace(/[<>:"/\\|?*]/g,'-').replace(/[. ]+$/g,'').slice(0,100);return title?`${title}-song-${song.slot}`:`bonsai-8-song-${song.slot}`;}
export class SongMetadata {
 constructor({store=defaultStore()}={}){this.store=store;this.entries={};this.session=new Map();this.deviceId=null;try{const saved=JSON.parse(store?.getItem(KEY)||'null');if(saved?.version===1&&saved.entries&&typeof saved.entries==='object'&&!Array.isArray(saved.entries))for(const [key,title] of Object.entries(saved.entries).slice(-1024))if(cleanTitle(title))this.entries[key]=cleanTitle(title);}catch{}}
 resetSession(){this.session.clear();this.deviceId=null;}
 key(slot,audioId,deviceId=this.deviceId){return validSlot(slot)&&hash(audioId)&&device(deviceId)?JSON.stringify([deviceId,slot,audioId]):null;}
 save(){try{this.store?.setItem(KEY,JSON.stringify({version:1,entries:this.entries}));}catch{/* Storage may be disabled; session annotations still work. */}}
 // Call only after the transfer's publication, flush, readback and exit succeed.
 published(song,title,{audioId=song.audio_id}={}){title=cleanTitle(title);if(!validSlot(song.slot)||!title)return;this.session.set(song.slot,{title,audioId:hash(audioId),shape:shape(song)});const key=this.key(song.slot,audioId);if(key){this.entries[key]=title;this.save();}}
 // Call only after confirmed deletion. Failed or interrupted deletes keep names.
 removed(slot){this.session.delete(slot);if(this.deviceId){for(const key of Object.keys(this.entries)){try{const [id,s]=JSON.parse(key);if(id===this.deviceId&&s===slot)delete this.entries[key];}catch{}}this.save();}}
 decorate(snapshot){const id=device(snapshot.device_id);if(this.deviceId!==id)this.session.clear();this.deviceId=id;const occupied=new Set(snapshot.slots.map(song=>song.slot));for(const slot of this.session.keys())if(!occupied.has(slot))this.session.delete(slot);
  return snapshot.slots.map(song=>{const deviceTitle=cleanTitle(song.title),audioId=hash(song.audio_id),key=this.key(song.slot,audioId);let current=this.session.get(song.slot);if(current&&(current.shape!==shape(song)||(audioId&&current.audioId!==audioId))){this.session.delete(song.slot);current=null;}const title=deviceTitle||(key?cleanTitle(this.entries[key]):'')||current?.title||null;return {...song,title,titleSource:deviceTitle?'device':key&&this.entries[key]?'verified-local':current?'session-local':null};});
 }
}
