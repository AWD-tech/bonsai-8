// Version 1 of the Bonsai SE3A playlist extension; see docs/PLAYLISTS.md.
export const PLAYLIST_BLOCK=6;
const dv=b=>new DataView(b.buffer,b.byteOffset,b.byteLength);
export function crc32(bytes){let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
export function playlistIdentity(meta,x3,slot){
 if(meta.length!==1024||x3.length<1040||!Number.isInteger(slot)||slot<1||slot>16)throw Error('Invalid song metadata.');
 const i=slot-1,bytes=new Uint8Array(124);bytes.set(meta.slice(8+i*44,52+i*44));bytes.set(meta.slice(716+i*16,732+i*16),44);bytes.set(x3.slice(16+i*64,80+i*64),60);return crc32(bytes);
}
export function decodePlaylist(bank){
 if(bank.length!==512)return null;const d=dv(bank);
 if(d.getUint32(0,true)!==0x4c503842||d.getUint32(4,true)!==1||d.getUint32(12,true)||d.getUint32(508,true)!==crc32(bank.subarray(0,508))||bank.subarray(144,508).some(Boolean))return null;
 const rows=[];for(let i=0;i<16;i++){const r=d.getUint32(16+i*8,true),bpm=r&65535;if((r&~0x3ffff)||(bpm&&(bpm<2000||bpm>40000)))return null;rows.push({bpm:bpm/100,decks:(r>>>16)&3,identity:d.getUint32(20+i*8,true)});}return {generation:d.getUint32(8,true),rows};
}
export function playlistBank(banks){const [a,b]=banks.map(decodePlaylist);if(!a)return b?1:-1;if(!b)return 0;const delta=(b.generation-a.generation)>>>0;return delta&&delta<0x80000000?1:0;}
export function writablePlaylistBank(bank){return bank.length===512&&(bank.every(b=>b===0)||bank.every(b=>b===255)||(dv(bank).getUint32(0,true)===0x4c503842&&dv(bank).getUint32(4,true)===1));}
export function playlistRows(banks,meta,x3){const active=playlistBank(banks),saved=active<0?null:decodePlaylist(banks[active]);return Array.from({length:16},(_,i)=>{const identity=playlistIdentity(meta,x3,i+1),row=saved?.rows[i];return row?.identity===identity?{...row}:{bpm:0,decks:3,identity};});}
export function encodePlaylist(rows,generation){
 if(rows.length!==16||!Number.isInteger(generation)||generation<0||generation>0xffffffff)throw Error('Invalid playlist.');
 const bank=new Uint8Array(512),d=dv(bank);d.setUint32(0,0x4c503842,true);d.setUint32(4,1,true);d.setUint32(8,generation,true);
 rows.forEach((row,i)=>{const bpm=Math.round(row.bpm*100);if(!Number.isFinite(row.bpm)||(row.bpm!==0&&(row.bpm<20||row.bpm>400))||!Number.isInteger(row.decks)||row.decks<0||row.decks>3||!Number.isInteger(row.identity)||row.identity<0||row.identity>0xffffffff)throw Error('Use a BPM from 20 to 400, or leave it blank.');d.setUint32(16+i*8,bpm|(row.decks<<16),true);d.setUint32(20+i*8,row.identity,true);});d.setUint32(508,crc32(bank.subarray(0,508)),true);return bank;
}
export function sortedPlaylist(songs,deck){return songs.filter(s=>(s.decks??3)&(1<<deck)).sort((a,b)=>(a.bpm||Infinity)-(b.bpm||Infinity)||a.slot-b.slot);}
