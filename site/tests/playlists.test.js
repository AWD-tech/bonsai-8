import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {encodePlaylist,decodePlaylist,playlistBank,playlistIdentity,playlistRows,writablePlaylistBank,sortedPlaylist} from '../public/playlists.js';
const meta=Uint8Array.from({length:1024},(_,i)=>i),x3=Uint8Array.from({length:1040},(_,i)=>i*3);
const rows=()=>Array.from({length:16},(_,i)=>({bpm:i?0:123.45,decks:i?3:1,identity:playlistIdentity(meta,x3,i+1)}));
test('firmware and website share the exact persisted format, fingerprints and generation-wrap rule',{skip:!existsSync(new URL('../../firmware/src/bonsai_playlist.c',import.meta.url))},()=>{
 const dir=mkdtempSync(join(tmpdir(),'bonsai-playlist-')),root=fileURLToPath(new URL('../../',import.meta.url));
 try{const banks=[encodePlaylist(rows(),0xffffffff),encodePlaylist(rows(),0)];writeFileSync(join(dir,'banks.bin'),Buffer.concat(banks));execFileSync('cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',join(root,'firmware/src'),join(root,'tests/test_playlist.c'),join(root,'firmware/src/bonsai_playlist.c'),'-o',join(dir,'test')]);execFileSync(join(dir,'test'),[join(dir,'banks.bin')]);assert.equal(playlistBank(banks),1);}finally{rmSync(dir,{recursive:true,force:true});}
});
test('corrupt/unknown versions fail closed; only blank or known-version storage can be replaced',()=>{
 const valid=encodePlaylist(rows(),4),bad=valid.slice();bad[100]^=1;assert.equal(decodePlaylist(bad),null);assert.equal(playlistBank([valid,bad]),0);
 const future=valid.slice();future[4]=2;assert.equal(writablePlaylistBank(future),false);assert.equal(decodePlaylist(future),null);
 assert.equal(writablePlaylistBank(new Uint8Array(512).fill(255)),true);assert.equal(writablePlaylistBank(new Uint8Array(512).fill(1)),false);
 assert.throws(()=>encodePlaylist(rows().map(r=>({...r,bpm:401})),1));assert.throws(()=>encodePlaylist(rows().map(r=>({...r,decks:4})),1));
});
test('changed slot descriptors reset only that song; explicit empty playlists stay empty',()=>{
 const bank=encodePlaylist(rows(),1),before=playlistRows([bank,bank],meta,x3);assert.equal(before[0].bpm,123.45);
 const changed=meta.slice();changed[12]^=1;const after=playlistRows([bank,bank],changed,x3);assert.equal(after[0].bpm,0);assert.equal(after[0].decks,3);assert.deepEqual(after[1],before[1]);
 assert.deepEqual(sortedPlaylist([{slot:1,bpm:100,decks:1}],1),[]);
});
test('each deck sorts ascending BPM, ties by slot, unknowns last without mutating the library',()=>{
 const songs=[{slot:1,bpm:120,decks:1},{slot:2,bpm:90,decks:3},{slot:3,bpm:0,decks:3},{slot:4,bpm:90,decks:2},{slot:5,bpm:120,decks:1}];
 assert.deepEqual(sortedPlaylist(songs,0).map(s=>s.slot),[2,1,5,3]);assert.deepEqual(sortedPlaylist(songs,1).map(s=>s.slot),[2,4,3]);assert.equal(songs[0].slot,1);
});
