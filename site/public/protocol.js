import {mirrorPacket} from './mirror.js?v=20260930-live-03';
// CRC lookup table from SP-1 Tape Looper, Copyright (c) 2026 chattock, MIT.
const CRC8_TABLE = new Uint8Array([
  0xea,0xd4,0x96,0xa8,0x12,0x2c,0x6e,0x50,0x7f,0x41,0x03,0x3d,0x87,0xb9,0xfb,0xc5,
  0xa5,0x9b,0xd9,0xe7,0x5d,0x63,0x21,0x1f,0x30,0x0e,0x4c,0x72,0xc8,0xf6,0xb4,0x8a,
  0x74,0x4a,0x08,0x36,0x8c,0xb2,0xf0,0xce,0xe1,0xdf,0x9d,0xa3,0x19,0x27,0x65,0x5b,
  0x3b,0x05,0x47,0x79,0xc3,0xfd,0xbf,0x81,0xae,0x90,0xd2,0xec,0x56,0x68,0x2a,0x14,
  0xb3,0x8d,0xcf,0xf1,0x4b,0x75,0x37,0x09,0x26,0x18,0x5a,0x64,0xde,0xe0,0xa2,0x9c,
  0xfc,0xc2,0x80,0xbe,0x04,0x3a,0x78,0x46,0x69,0x57,0x15,0x2b,0x91,0xaf,0xed,0xd3,
  0x2d,0x13,0x51,0x6f,0xd5,0xeb,0xa9,0x97,0xb8,0x86,0xc4,0xfa,0x40,0x7e,0x3c,0x02,
  0x62,0x5c,0x1e,0x20,0x9a,0xa4,0xe6,0xd8,0xf7,0xc9,0x8b,0xb5,0x0f,0x31,0x73,0x4d,
  0x58,0x66,0x24,0x1a,0xa0,0x9e,0xdc,0xe2,0xcd,0xf3,0xb1,0x8f,0x35,0x0b,0x49,0x77,
  0x17,0x29,0x6b,0x55,0xef,0xd1,0x93,0xad,0x82,0xbc,0xfe,0xc0,0x7a,0x44,0x06,0x38,
  0xc6,0xf8,0xba,0x84,0x3e,0x00,0x42,0x7c,0x53,0x6d,0x2f,0x11,0xab,0x95,0xd7,0xe9,
  0x89,0xb7,0xf5,0xcb,0x71,0x4f,0x0d,0x33,0x1c,0x22,0x60,0x5e,0xe4,0xda,0x98,0xa6,
  0x01,0x3f,0x7d,0x43,0xf9,0xc7,0x85,0xbb,0x94,0xaa,0xe8,0xd6,0x6c,0x52,0x10,0x2e,
  0x4e,0x70,0x32,0x0c,0xb6,0x88,0xca,0xf4,0xdb,0xe5,0xa7,0x99,0x23,0x1d,0x5f,0x61,
  0x9f,0xa1,0xe3,0xdd,0x67,0x59,0x1b,0x25,0x0a,0x34,0x76,0x48,0xf2,0xcc,0x8e,0xb0,
  0xd0,0xee,0xac,0x92,0x28,0x16,0x54,0x6a,0x45,0x7b,0x39,0x07,0xbd,0x83,0xc1,0xff,
]);
export function crc8(data) { let crc = 0; for (const value of data) crc = CRC8_TABLE[crc ^ value]; return crc; }
export function encodeCOBS(bytes) {
  const out = [0]; let code = 1, at = 0;
  for (const byte of bytes) {
    if (byte === 0) { out[at] = code; at = out.length; out.push(0); code = 1; }
    else { out.push(byte); if (++code === 255) { out[at] = code; at = out.length; out.push(0); code = 1; } }
  }
  out[at] = code; out.push(0); return new Uint8Array(out);
}
export function decodeCOBS(frame) {
  const out = []; const length = frame.at(-1) === 0 ? frame.length - 1 : frame.length;
  for (let i = 0; i < length;) {
    const count = frame[i++]; if (!count || i + count - 1 > length) throw new Error('Invalid serial frame.');
    for (let j = 1; j < count; j++) out.push(frame[i++]);
    if (count < 255 && i < length) out.push(0);
  }
  return new Uint8Array(out);
}
export function packet(command, seq, payload = []) {
  if (payload.length > 255) throw new Error('Payload too long.');
  const body = new Uint8Array([0x51, seq & 255, command, payload.length, ...payload]);
  return encodeCOBS([...body, crc8(body)]);
}
export function response(frame) {
  try {
    const bytes = decodeCOBS(frame);
    if (bytes.length < 5 || bytes.length !== bytes[3] + 5 || crc8(bytes.subarray(0, -1)) !== bytes.at(-1)) return null;
    return { seq: bytes[1], command: bytes[2], payload: bytes.slice(4, -1) };
  } catch { return null; }
}
const delay = (ms) => new Promise(r => setTimeout(r, ms));
export function dualDeckStatus(line) {
  let state; try { state = JSON.parse(line); } catch { return null; }
  const vector = (key, size, lo, hi) => Array.isArray(state?.[key]) && state[key].length === size && state[key].every(v => Number.isInteger(v) && v >= lo && v <= hi);
  if (!state || !/^(sp1-dual-deck-|bonsai-8-)/.test(state.firmware) || ![0,1].includes(state.deck) ||
      !Number.isInteger(state.master) || state.master < 0 || state.master > 256 ||
      !vector('gains',8,0,256) || !vector('mute',2,0,15) || !vector('playing',2,0,1) ||
      !vector('speed',2,32768,81920) || !vector('slots',2,0,16)) return null;
  return state;
}
export function libraryPacket(line) {
 let result;try{result=JSON.parse(line);}catch{return null;}
 if(result?.library!==1||result.sample_rate!==48000||!Array.isArray(result.slots)||result.slots.length>16)return null;
 const seen=new Set();
 for(const song of result.slots){if(!Number.isInteger(song.slot)||song.slot<1||song.slot>16||seen.has(song.slot)||!Array.isArray(song.present)||song.present.length!==4||song.present.some(v=>![0,1].includes(v))||!song.present.some(Boolean)||!Number.isInteger(song.frames)||song.frames<0||song.frames>24100000||!(song.title===null||typeof song.title==='string'))return null;seen.add(song.slot);}
 return result;
}
export function loadPacket(line){let r;try{r=JSON.parse(line);}catch{return null;}if(r?.loaded===0&&typeof r.error==='string')return r;return r?.loaded===1&&[0,1].includes(r.deck)&&Number.isInteger(r.slot)&&r.slot>=1&&r.slot<=16?r:null;}
export function selectedDeckControls(state) {
  const deck=state.deck;
  return {deck,slot:state.slots[deck],gains:state.gains.slice(deck*4,deck*4+4).map(v=>v/256),
    mutes:Array.from({length:4},(_,i)=>Boolean(state.mute[deck]&(1<<i))),
    volume:state.master/256,speed:state.speed[deck]/65536,playing:Boolean(state.playing[deck])};
}
export class SP1Connection {
  constructor(onControls, onStatus, onMirror = () => {}) { this.onControls = onControls; this.onStatus = onStatus; this.onMirror = onMirror; this.seq = 0; this.alive = false; this.pending = null; this.exclusive=false; this.rawMode=false; this.rawBytes=[]; }
  async connect(mode = 'dual') {
    if (!navigator.serial) throw new Error('Hardware connection needs Chrome or Edge on a computer.');
    if (this.port) throw new Error('Disconnect the current player first.');
    this.mode = mode; this.fullMirror = false;
    this.port = await navigator.serial.requestPort();
    try {
      try { await this.port.open({ baudRate: 115200 }); }
      catch { throw new Error('The USB port is busy or unavailable. Wait for the song upload to finish and close other SP-1 connections.'); }
      await this.port.setSignals({ dataTerminalReady: true });
      this.reader = this.port.readable.getReader(); this.writer = this.port.writable.getWriter();
      this.alive = true; this.readTask = this.readLoop();
      this.onStatus('Checking Bonsai 8…');
      if (this.mode === 'dual') {
        const state = await this.statusCommand(15000);
        await this.onControls(null, null, state);
        this.fullMirror=state.mirror===1;
        if(this.fullMirror)this.onMirror(await this.mirrorCommand());
        this.onStatus(this.fullMirror?'Connected · physical lights and controls live':'Connected · mixer only. Install Bonsai 8 for physical lights and button presses.', true);
        this.pollTask = this.poll();
        return;
      }
      const state = await this.command(0x52, [], 3000);
      if (state.command !== 0x53) throw new Error('SP-1 status was not recognized.');
      const mode = Array.from(state.payload).join('');
      if (!['00100', '10510'].includes(mode)) throw new Error('This firmware does not expose the stock SP-1 control protocol.');
      if (mode === '00100') {
        this.onStatus('Opening control monitor…');
        // Existing stock app transfer mode, as used by Solderless device info.
        // No flash, erase, song-write, or firmware-write commands are used.
        await this.command(0x70, [1]); await delay(200);
        await this.command(0x50); await delay(2000);
      }
      const f = await this.command(0x64);
      const b = await this.command(0x5c);
      this.validate(f, b);
      this.onStatus('Connected · controls live', true); this.onControls(f.payload, b.payload);
      this.pollTask = this.poll();
    } catch (error) { await this.disconnect(); throw error; }
  }
  validate(f, b) {
    if (f.command !== 0x65 || b.command !== 0x5d || f.payload.length !== 4 || b.payload.length !== 10) throw new Error('Live controls are unavailable. The original SP-1 transfer protocol is required; custom firmware may not support it.');
  }
  async readLoop() {
    let buffer = [];
    try {
      while (this.alive) {
        const { value, done } = await this.reader.read(); if (done) break;
        for (const byte of value) {
          if(this.rawMode){this.rawBytes.push(byte);this.resolveRaw();continue;}
          if (this.mode === 'dual') {
            if (byte === 10) {
              const line = new TextDecoder().decode(new Uint8Array(buffer)); buffer = [];
              const parser={mirror:mirrorPacket,status:dualDeckStatus,library:libraryPacket,load:loadPacket}[this.pending?.kind];const state=parser?.(line);
              if (state && ['status','mirror','library','load'].includes(this.pending?.kind)) {
                if(this.pending.kind==='status')this.recordSaving=[1,2,3].includes(state.record?.state);
                const pending = this.pending; this.pending = null; clearTimeout(pending.timer); pending.resolve(state);
              }
            } else if (buffer.length < 4096) buffer.push(byte);
            else buffer = [];
            continue;
          }
          buffer.push(byte);
          if (byte === 0) {
            const decoded = response(new Uint8Array(buffer)); buffer = [];
            if (decoded && this.pending && decoded.seq === this.pending.seq) {
              const pending = this.pending; this.pending = null; clearTimeout(pending.timer); pending.resolve(decoded);
            }
          } else if (buffer.length > 4096) buffer = [];
        }
      }
    } catch (e) { if (this.alive) this.onStatus('USB disconnected'); }
    finally { this.alive = false; this.rejectPending(new Error('USB connection closed.')); }
  }
  rejectPending(e) { if (this.pending) { clearTimeout(this.pending.timer); this.pending.reject(e); this.pending = null; } }
  statusCommand(timeout = this.recordSaving?15000:3000) { return this.jsonCommand('status','DDSTAT?\n',timeout); }
  mirrorCommand(timeout = this.recordSaving?15000:1600) { return this.jsonCommand('mirror','DDMIR?\n',timeout); }
  jsonCommand(kind, text, timeout) {
    if (!this.alive || this.pending) return Promise.reject(new Error('Serial connection is unavailable or busy.'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(new Error('No Dual Deck response. Finish any upload, reconnect normally, and hold FUNCTION for 1.5 seconds.')); }, timeout);
      this.pending = { kind, resolve, reject, timer };
      this.writer.write(new TextEncoder().encode(text)).catch(e => this.rejectPending(e));
    });
  }
  command(command, payload = [], timeout = 1600) {
    if (!this.alive) return Promise.reject(new Error('SP-1 disconnected.'));
    if (this.pending) return Promise.reject(new Error('Serial command already in progress.'));
    const seq = this.seq++ & 255;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(new Error('SP-1 did not answer. Reconnect in bootloader mode.')); }, timeout);
      this.pending = { seq, resolve, reject, timer };
      this.writer.write(packet(command, seq, payload)).catch(e => this.rejectPending(e));
    });
  }
  async poll() {
    let lastStatus=0;
    try {
      while (this.alive && !this.exclusive) {
        if (this.mode === 'dual') {
          if(this.fullMirror){
            this.onMirror(await this.mirrorCommand());
            if(Date.now()-lastStatus>=500){await this.onControls(null,null,await this.statusCommand());lastStatus=Date.now();}
            await delay(30);
          }else{await this.onControls(null, null, await this.statusCommand());await delay(120);}
          continue;
        }
        const f = await this.command(0x64), b = await this.command(0x5c);
        this.validate(f, b); this.onControls(f.payload, b.payload); await delay(60);
      }
    } catch (e) { if (this.port) { await this.disconnect(); this.onStatus(e.message, false); } }
  }
  async withExclusive(work) {
    if(!this.alive||this.mode!=='dual')throw new Error('Connect the powered-on Bonsai 8 player first.');
    if(this.exclusive)throw new Error('Another device operation is already in progress.');
    this.exclusive=true;
    try{await this.pollTask;if(!this.alive)throw new Error('The player disconnected.');return await work();}
    finally{this.exclusive=false;if(this.alive)this.pollTask=this.poll();}
  }
  library(){return this.withExclusive(()=>this.jsonCommand('library','DDLIB?\n',5000));}
  loadSong(deck,slot){
    if(![0,1].includes(deck)||!Number.isInteger(slot)||slot<1||slot>16)return Promise.reject(new Error('Invalid deck or song.'));
    return this.withExclusive(async()=>{const result=await this.jsonCommand('load',`DDLOAD ${deck} ${slot}\n`,5000);if(!result.loaded)throw new Error(result.error);return result;});
  }
  resolveRaw(){if(this.pending?.kind!=='raw'||this.rawBytes.length<this.pending.count)return;const pending=this.pending;this.pending=null;clearTimeout(pending.timer);pending.resolve(new Uint8Array(this.rawBytes.splice(0,pending.count)));}
  rawCommand(request,count,timeout=6000){
    if(!this.alive||!this.rawMode||this.pending||this.rawBytes.length)return Promise.reject(new Error('Unexpected data on the transfer connection. Reconnect before continuing.'));
    return new Promise((resolve,reject)=>{const timer=setTimeout(()=>this.rejectPending(new Error('Transfer timed out. Reconnect and retry the same upload to verify its saved progress.')),timeout);this.pending={kind:'raw',count,resolve,reject,timer};this.writer.write(request).catch(e=>this.rejectPending(e));});
  }
  withTransfer(work){return this.withExclusive(async()=>{
    const state=await this.statusCommand();if([1,2,3].includes(state.record?.state))throw new Error('Stop recording, then pause both decks to save before managing songs.');
    this.rawBytes=[];this.rawMode=true;
    try{const result=await work({exchange:(request,count)=>this.rawCommand(request,count)});this.rawMode=false;this.rawBytes=[];return result;}
    catch(error){await this.disconnect();throw error;}
  });}
  async disconnect() {
    this.alive = false; this.rejectPending(new Error('Disconnected.'));
    try { await this.reader?.cancel(); } catch {}
    try { await this.readTask; this.reader?.releaseLock(); this.writer?.releaseLock(); await this.port?.close(); } catch {}
    this.reader = null; this.writer = null; this.port = null; this.onStatus('Not connected', false);
  }
}
