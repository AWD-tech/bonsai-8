import {packet,response} from './protocol.js?v=20261002-bonsai-01';
import {sha256,join,u32} from './device-library.js?v=20261003-bonsai-library-01';
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const APP_START=0x20000,APP_END=0xff000;
export const FLASH_PRESERVATION='This updater writes only the application flash region. It does not format or initialize song storage. Keeping those bytes does not make a stock-firmware library compatible with Bonsai 8; export a backup before switching firmware.';
export async function validateFirmware(binary,manifest){
 if(!(binary instanceof Uint8Array)||binary.length<8||binary.length>APP_END-APP_START||manifest?.bytes!==binary.length||manifest?.application_address!==APP_START||manifest?.product!=='Bonsai 8'||!/^\d+\.\d+\.\d+(?:[-.][a-zA-Z0-9.]+)?$/.test(manifest.version||'')||!['candidate','verified'].includes(manifest.status)||!/^[a-f0-9]{64}$/.test(manifest.sha256||''))throw new Error('This firmware package is not a supported Bonsai 8 application.');
 if(manifest.file!==undefined&&manifest.file!==`bonsai-8-${manifest.version}.bin`)throw new Error('Firmware filename does not match the release version. Nothing was written.');
 // Own the validated bytes so a caller cannot swap a release while the chooser is open.
 const copy=binary.slice(),release=Object.freeze({...manifest}),d=new DataView(copy.buffer),stack=d.getUint32(0,true),reset=d.getUint32(4,true);
 if(stack<=0x20000000||stack>0x20040000||stack%8||!(reset&1)||reset<APP_START||reset>=APP_START+copy.length)throw new Error('Firmware vectors are invalid. Nothing was written.');
 if(await sha256(copy)!==release.sha256)throw new Error('Firmware checksum does not match its release manifest. Nothing was written.');
 return {binary:copy,manifest:release};
}
export function firmwareChunks(binary){
 if(!(binary instanceof Uint8Array)||binary.length<8||binary.length>APP_END-APP_START)throw new Error('Firmware exceeds the application region or is incomplete.');
 const chunks=[];let counter=0;
 for(let page=0;page<Math.ceil(binary.length/4096);page++)for(let offset=0;offset<Math.min(4096,binary.length-page*4096);offset+=240){const start=page*4096+offset,data=binary.slice(start,Math.min(start+240,page*4096+4096,binary.length)),address=APP_START+start;if(address<APP_START||address+data.length>APP_END)throw new Error('Firmware exceeds the application region.');chunks.push({page,offset,address,payload:join(u32(++counter),u32(address),data)});}
 return chunks;
}
export class FirmwareFlasher {
 constructor(onProgress=()=>{}){this.onProgress=onProgress;this.seq=1;this.pending=null;this.active=false;this.readError=null;}
 fail(error){this.readError=error;if(this.pending){const p=this.pending;this.pending=null;clearTimeout(p.timer);p.reject(error);}}
 async write(data){if(this.readError)throw this.readError;if(!this.active||!this.writer)throw new Error('The updater is not connected.');await this.writer.write(data);if(this.readError)throw this.readError;}
 async command(cmd,payload=new Uint8Array(),expected,timeout=5000){
  if(this.pending)throw new Error('An updater command is already pending.');
  const seq=this.seq++&255;
  const result=new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.fail(new Error('The updater stopped responding. Leave the player in bootloader mode and retry the complete firmware update.'));},timeout);this.pending={seq,resolve,reject,timer};});
  const observed=result.then(reply=>({reply}),error=>({error}));
  try{await this.write(packet(cmd,seq,payload));}catch(error){this.fail(error);}
  const {reply,error}=await observed;if(error)throw error;
  if(reply.command!==expected)throw new Error('Unexpected updater response. The application was not started.');return reply.payload;
 }
 async read(){let buffer=[];try{while(this.active){const {value,done}=await this.reader.read();if(done){if(this.active)this.fail(new Error('The updater disconnected. Reconnect in bootloader mode and retry the complete update.'));break;}for(const byte of value){buffer.push(byte);if(byte===0){const r=response(new Uint8Array(buffer));buffer=[];if(!r)throw new Error('Updater frame checksum or format is invalid. The application was not started.');if(this.pending?.seq===r.seq){const p=this.pending;this.pending=null;clearTimeout(p.timer);p.resolve(r);}}else if(buffer.length>4096)throw new Error('Invalid updater frame.');}}}catch(error){if(this.active)this.fail(error);}}
 async flash(release){
  if(this.active)throw new Error('A firmware update is already running.');
  this.active=true;this.readError=null;this.readTask=null;
  try{
   const verified=await validateFirmware(release?.binary,release?.manifest),chunks=firmwareChunks(verified.binary);
   if(!globalThis.navigator?.serial)throw new Error('Use Chrome or Edge on a computer to update firmware.');
   this.port=await navigator.serial.requestPort({filters:[{usbVendorId:0x2367,usbProductId:0x1701}]});const info=this.port.getInfo();if(info.usbVendorId!==0x2367||info.usbProductId!==0x1701)throw new Error('Select the SP-1 bootloader.');
   await this.port.open({baudRate:115200});await this.port.setSignals({dataTerminalReady:true});this.reader=this.port.readable.getReader();this.writer=this.port.writable.getWriter();this.readTask=this.read();
   for(let i=0;i<3;i++){const state=await this.command(0x52,undefined,0x53);if(state.length!==5||!state.every((v,k)=>v===[0,0,1,0,0][k]))throw new Error('The device is not in a fresh bootloader session. No erase was requested.');await delay(150);}
   // Only application state/erase, bounded chunks, finalization and start commands.
   // No library initialization or song-storage command is sent by this updater.
   this.onProgress(0,'Writing Bonsai 8 application. Keep USB connected.');await this.command(0x46,undefined,0x47);
   for(let i=0;i<chunks.length;i++){const chunk=chunks[i];await this.write(packet(0x45,this.seq++&255,chunk.payload));await delay(chunk.offset===0&&chunk.page>0?100:5);if(!(i%8)||i===chunks.length-1)this.onProgress((i+1)/chunks.length,'Writing firmware');}
   await delay(150);const pages=Math.ceil(verified.binary.length/4096),first=await this.command(0x48,u32(0x20+pages-1),0x49);if(first.length!==4)throw new Error('Updater finalization is incomplete. The application was not started.');const counter=new DataView(first.buffer,first.byteOffset,first.byteLength).getUint32(0,true),second=await this.command(0x48,u32(counter),0x49);if(second.length!==4)throw new Error('Updater finalization is incomplete. The application was not started.');await delay(300);if(this.readError)throw this.readError;
   // A successful start write may immediately close the USB bootloader. That is
   // expected here; any earlier reader failure still prevents this command.
   await this.writer.write(packet(0x50,this.seq++&255));
   this.onProgress(1,'Update acknowledged. Hold FUNCTION to power on, then reconnect.');return {version:verified.manifest.version,sha256:verified.manifest.sha256,verification:'CRC-valid updater acknowledgements; application readback and running-version verification are still required.'};
  }finally{
   this.active=false;try{await this.reader?.cancel();}catch{}try{await this.readTask;}catch{}try{this.reader?.releaseLock();}catch{}try{this.writer?.releaseLock();}catch{}try{await this.port?.close();}catch{}this.port=this.reader=this.writer=null;this.readTask=null;
  }
 }
}
export async function fetchRelease(){const response=await fetch('./firmware/manifest.json',{cache:'no-store'});if(!response.ok)throw new Error('A Bonsai 8 release has not been published here yet.');const manifest=await response.json();if(typeof manifest.file!=='string'||manifest.file!==`bonsai-8-${manifest.version}.bin`||!/^bonsai-8-[a-zA-Z0-9.-]+\.bin$/.test(manifest.file))throw new Error('Invalid firmware release filename or version.');const binaryResponse=await fetch(`./firmware/${manifest.file}`,{cache:'no-store'});if(!binaryResponse.ok)throw new Error('The firmware download is unavailable.');return validateFirmware(new Uint8Array(await binaryResponse.arrayBuffer()),manifest);}
