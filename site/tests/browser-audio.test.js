import test from 'node:test';
import assert from 'node:assert/strict';
import {AudioEngine,bufferBytes,uniqueBufferBytes} from '../public/audio.js';
import {SessionLibrary,loadSessionSong,exportSnapshot} from '../public/browser-mixer.js';

class Param {
  constructor(value=1){this.value=value;this.changes=[];}
  setTargetAtTime(value,when,timeConstant){this.value=value;this.changes.push({value,when,timeConstant});}
  setValueAtTime(value,when){this.value=value;this.changes.push({value,when});}
}
class Node {
  constructor(){this.gain=new Param();this.playbackRate=new Param();this.threshold={};this.knee={};this.ratio={};this.attack={};this.release={};this.links=[];}
  connect(node){this.links.push(node);}
  disconnect(){this.disconnected=true;}
  start(when,offset){this.startTime=when;this.offset=offset;}
  stop(){this.stopped=true;}
}
class Buffer {
  constructor(channels,length,sampleRate){this.numberOfChannels=channels;this.length=length;this.sampleRate=sampleRate;this.duration=length/sampleRate;this.data=Array.from({length:channels},()=>new Float32Array(length));}
  getChannelData(channel){return this.data[channel];}
  copyToChannel(data,channel){this.data[channel].set(data);}
}
class Context {
  constructor(){this.currentTime=10;this.destination=new Node();this.createdSources=[];}
  async resume(){}
  createGain(){return new Node();}
  createDynamicsCompressor(){return new Node();}
  createAnalyser(){return new Node();}
  createBufferSource(){const source=new Node();this.createdSources.push(source);return source;}
  createBuffer(...args){return new Buffer(...args);}
}
const fixture=()=>{const ctx=new Context();const audio=new AudioEngine({contextFactory:()=>ctx});return {ctx,audio};};
const stems=(ctx,seconds=20)=>Array.from({length:4},()=>ctx.createBuffer(2,seconds*100,100));

test('A and B share one output context but have independent transport clocks',async()=>{
 const {audio,ctx}=fixture();await audio.loadDeck(0,stems(ctx));await audio.loadDeck(1,stems(ctx,30));
 await audio.playBoth();assert.equal(ctx.createdSources.length,8);
 assert.ok(ctx.createdSources.every(source=>source.startTime===10.04));
 ctx.currentTime=12.04;assert.equal(audio.position(0),2);assert.equal(audio.position(1),2);
 audio.pauseDeck(1);ctx.currentTime=13.04;
 assert.equal(audio.position(0),3);assert.equal(audio.position(1),2);
 assert.equal(audio.decks[0].playing,true);assert.equal(audio.decks[1].playing,false);
});
test('loading another B song never restarts, pauses or changes A',async()=>{
 const {audio,ctx}=fixture();await audio.loadDeck(0,stems(ctx));await audio.loadDeck(1,stems(ctx));await audio.playBoth();
 const sources=[...audio.decks[0].sources];ctx.currentTime=12;
 await audio.loadDeck(1,stems(ctx,40),{name:'Another song'});
 assert.deepEqual(audio.decks[0].sources,sources);assert.ok(sources.every(source=>!source.stopped));
 assert.equal(audio.decks[0].playing,true);assert.equal(audio.decks[1].playing,false);
 assert.equal(audio.decks[1].offset,0);assert.equal(audio.decks[1].duration,40);
 ctx.currentTime=13.04;assert.equal(audio.position(0),3);
});
test('selected-deck faders, mute and solo preserve the other deck',async()=>{
 const {audio}=fixture();await audio.init();audio.setGain(0,.4);audio.mute(1);audio.solo(0);
 audio.select(1);audio.setGain(0,.8);audio.solo(2);
 assert.deepEqual(audio.decks[0].gains,[.4,1,1,1]);assert.deepEqual(audio.decks[0].mutes,[false,true,false,false]);
 assert.equal(audio.decks[0].trackGains[0].gain.value,.4);assert.equal(audio.decks[1].trackGains[2].gain.value,1);
 assert.equal(audio.decks[1].trackGains[0].gain.value,0);
 audio.select(0);assert.equal(audio.gains[0],.4);assert.equal(audio.solos[0],true);
});
test('deck volume and combined master use separate buses with eight-stem headroom',async()=>{
 const {audio}=fixture();await audio.init();audio.setDeckVolume(1,.3);audio.setVolume(.6);
 assert.equal(audio.decks[0].bus.gain.value,1);assert.equal(audio.decks[1].bus.gain.value,.3);assert.equal(audio.master.gain.value,.6);
 for(const deck of audio.decks){assert.equal(deck.bus.links[0],audio.headroom);for(const gain of deck.trackGains)assert.equal(gain.links[0],deck.bus);}
 assert.equal(audio.headroom.links[0],audio.master);assert.equal(audio.master.links[0],audio.limiter);
 assert.equal(audio.limiter.threshold.value,0,'avoid native compressor makeup boosting below-threshold levels');
 assert.equal(audio.headroom.gain.value*8,1,'correlated unity stems fit before master/limiter');
});
test('short stems pad with silence and loop natively on one common song period',async()=>{
 const {audio,ctx}=fixture();const long=ctx.createBuffer(2,1000,100),short=ctx.createBuffer(2,400,100);short.data[0].fill(.7);short.data[1].fill(-.3);
 await audio.loadDeck(0,[long,short,null,null]);await audio.play();
 const padded=audio.buffers[1];assert.equal(padded.length,1000);assert.equal(padded.duration,10);
 assert.equal(padded.data[0][399],short.data[0][399]);assert.equal(padded.data[0][400],0);assert.equal(padded.data[1][999],0);
 assert.equal(audio.sources[0].loop,true);assert.equal(audio.sources[1].loop,true);assert.equal(audio.sources[1].loopEnd,10);
 ctx.currentTime=42.04;assert.ok(Math.abs(audio.position()-2)<1e-9);
 const sources=ctx.createdSources.length;audio.tick();assert.equal(ctx.createdSources.length,sources,'looping must not depend on a UI tick/source restart');
});
test('speed changes preserve live source nodes and do not add restart gaps',async()=>{
 const {audio,ctx}=fixture();await audio.load(stems(ctx));await audio.play();ctx.currentTime=12.04;
 const sources=[...audio.sources];await audio.rate(12);ctx.currentTime=13.04;
 assert.equal(audio.position(),4);assert.deepEqual(audio.sources,sources);assert.ok(sources.every(source=>!source.stopped&&source.playbackRate.value===2));
 assert.deepEqual(audio.decks[0].stemPitch,[0,0,0,0],'tape speed must not pretend to provide independent pitch preservation');
});
test('seek and cue affect only their target deck',async()=>{
 const {audio,ctx}=fixture();await audio.loadDeck(0,stems(ctx));await audio.loadDeck(1,stems(ctx));await audio.playBoth();
 ctx.currentTime=11.04;const aSources=[...audio.decks[0].sources];await audio.seekDeck(1,8);
 assert.equal(audio.position(1),8);assert.deepEqual(audio.decks[0].sources,aSources);
 audio.cueDeck(1);assert.equal(audio.position(1),0);assert.equal(audio.decks[1].playing,false);assert.equal(audio.decks[0].playing,true);
});
test('pausing during pending AudioContext resume cancels that play request',async()=>{
 const {audio,ctx}=fixture();await audio.load(stems(ctx));let resume;
 ctx.resume=()=>new Promise(resolve=>{resume=resolve;});const playing=audio.play();audio.pauseAll();resume();await playing;
 assert.equal(audio.playing,false);assert.equal(ctx.createdSources.length,0);
});
test('unloading during pending context resume cancels a pending song load',async()=>{
 const {audio,ctx}=fixture();await audio.init();let resume;ctx.resume=()=>new Promise(resolve=>{resume=resolve;});
 const loading=audio.loadDeck(1,stems(ctx));await audio.unloadDeck(1);resume();assert.equal(await loading,false);assert.equal(audio.decks[1].duration,0);
});
test('non-looping completion preserves end position until replay, with no other deck changes',async()=>{
 const {audio,ctx}=fixture();await audio.loadDeck(0,stems(ctx,2));await audio.loadDeck(1,stems(ctx,4));audio.loop=false;await audio.playBoth();ctx.currentTime=12.1;audio.tick();
 assert.equal(audio.decks[0].playing,false);assert.equal(audio.position(0),2);assert.equal(audio.decks[1].playing,true);await audio.playDeck(0);assert.equal(audio.position(0),0);
});
test('unload clears only one deck and releases its buffer references',async()=>{
 const {audio,ctx}=fixture();await audio.loadDeck(0,stems(ctx));await audio.loadDeck(1,stems(ctx));await audio.playBoth();const b=audio.decks[1].buffers;
 await audio.unloadDeck(0);assert.equal(audio.decks[0].buffers.length,0);assert.equal(audio.decks[0].originalBuffers.length,0);assert.equal(audio.decks[0].duration,0);
 assert.equal(audio.decks[1].buffers,b);assert.equal(audio.decks[1].playing,true);
});
test('session library keeps distinct songs and replaces a separated source without duplication',()=>{
 const library=new SessionLibrary();const a=library.save([{}],{name:'<script>title</script>',mode:'source'});const b=library.save([{}],{name:'B'});
 library.save([{},{}],{name:'A separated',mode:'separated'},a.id);assert.equal(library.values().length,2);assert.equal(library.get(a.id).buffers.length,2);
 assert.equal(library.get(b.id).name,'B');library.remove(a.id);assert.equal(library.values().length,1);library.clear();assert.equal(library.values().length,0);
});
test('invalid deck indices and invalid stem buffers fail before changing transports',async()=>{
 const {audio}=fixture();assert.throws(()=>audio.select(2),/deck/);await assert.rejects(audio.loadDeck(0,[{duration:NaN}]),/valid/);await assert.rejects(audio.loadDeck(1,Array(5).fill({duration:1})),/four/);
});

test('prepared pitch audio preserves original source buffers and metadata without independent playback-rate drift',async()=>{
 const {audio,ctx}=fixture();const originals=stems(ctx,20);await audio.loadDeck(0,originals,{name:'Original',mode:'imported'});
 audio.decks[0].speed=1.2;const prepared=stems(ctx,16),analysis={bpm:120,firstBeat:.2};
 await audio.installPreparedDeck(0,prepared,{stemPitch:[2,0,-1,3],analysis});
 assert.deepEqual(audio.decks[0].originalBuffers,originals);assert.equal(audio.duration,16);assert.equal(audio.speed,1);
 assert.deepEqual(audio.decks[0].preparedStems,prepared);assert.deepEqual(audio.decks[0].stemPitch,[2,0,-1,3]);assert.equal(audio.decks[0].analysis,analysis);
 await audio.play();assert.ok(audio.sources.every(source=>source.playbackRate.value===1));
 assert.equal(audio.decks[0].media.name,'Original');
});
test('scheduled launches clamp past times to the actual audio clock and reject invalid times',async()=>{
 const {audio,ctx}=fixture();await audio.loadDeck(0,stems(ctx));await audio.playDeck(0,2);
 assert.equal(audio.decks[0].started,10);assert.equal(audio.sources[0].startTime,10);audio.pause();
 await assert.rejects(audio.playDeck(0,NaN),/audio-clock/);
});

test('aggregate decoded-buffer budget counts unique loaded, original, prepared and session references',async()=>{
 const ctx=new Context();const audio=new AudioEngine({contextFactory:()=>ctx,memoryBudgetBytes:4000});
 const a=ctx.createBuffer(2,100,100),b=ctx.createBuffer(2,100,100),c=ctx.createBuffer(2,100,100);
 await audio.loadDeck(0,[a,a]);audio.sessionBuffers=()=>[a,b,b];
 assert.equal(bufferBytes(a),800);assert.equal(uniqueBufferBytes([a,a,b]),1600);assert.equal(audio.memoryUsage(),1600);
 audio.decks[0].preparedStems=[c];audio.decks[0].media.original=b;assert.equal(audio.memoryUsage(),2400);
 assert.equal(audio.ensureCapacity([a,b,c],1600),4000);assert.throws(()=>audio.ensureCapacity([a,b,c],1601),/budget/);
});
test('capacity rejection keeps both playing decks and their source references intact',async()=>{
 const ctx=new Context();const audio=new AudioEngine({contextFactory:()=>ctx,memoryBudgetBytes:4000});
 const a=ctx.createBuffer(2,100,100),b=ctx.createBuffer(2,100,100);await audio.loadDeck(0,[a]);await audio.loadDeck(1,[b]);await audio.playBoth();
 const sources=audio.decks.map(deck=>deck.sources);const large=ctx.createBuffer(2,400,100);
 await assert.rejects(audio.loadDeck(1,[large]),/budget/);
 assert.equal(audio.decks[0].sources,sources[0]);assert.equal(audio.decks[1].sources,sources[1]);
 assert.ok(audio.decks.every(deck=>deck.playing));assert.equal(audio.decks[1].buffers[0],b);
});
test('session budget rejects new entries without silently evicting existing songs',()=>{
 const ctx=new Context(),library=new SessionLibrary({memoryBudgetBytes:1000});const buffer=ctx.createBuffer(2,100,100);
 const first=library.save([buffer],{name:'Keep this song'});
 assert.throws(()=>library.save([ctx.createBuffer(2,100,100)],{name:'Too large'}),/budget/);
 assert.equal(library.get(first.id).name,'Keep this song');assert.equal(library.values().length,1);
 library.remove(first.id);assert.equal(library.save([buffer],{name:'Fits now'}).name,'Fits now');
});
test('silent padding is reserved before allocation and preserves the target on failure',async()=>{
 const ctx=new Context(),audio=new AudioEngine({contextFactory:()=>ctx,memoryBudgetBytes:2000});
 const original=ctx.createBuffer(2,50,100);await audio.loadDeck(1,[original]);await audio.playDeck(1);
 const long=ctx.createBuffer(2,150,100),short=ctx.createBuffer(2,20,100);
 await assert.rejects(audio.loadDeck(1,[long,short]),/budget/);
 assert.equal(audio.decks[1].buffers[0],original);assert.equal(audio.decks[1].playing,true);
});

test('reloading then unloading a prepared session song cannot erase its cached pitch metadata',async()=>{
 const {audio,ctx}=fixture(),library=new SessionLibrary();const originals=stems(ctx),prepared=stems(ctx,16);
 const song=library.save(prepared,{name:'Prepared',mode:'prepared',originalBuffers:originals,preparedStems:prepared,stemPitch:[2,0,-1,3],analysis:{prepared:true,bpm:120}});
 await loadSessionSong(audio,1,song);await audio.unloadDeck(1);
 assert.deepEqual(song.stemPitch,[2,0,-1,3]);assert.deepEqual(song.preparedStems,prepared);
 await loadSessionSong(audio,1,song);assert.deepEqual(audio.decks[1].stemPitch,[2,0,-1,3]);assert.equal(audio.decks[1].analysis.bpm,120);
});
test('export snapshots stay attached to their prepared song while selection changes',async()=>{
 const {audio,ctx}=fixture();const buffers=stems(ctx);await audio.loadDeck(0,buffers,{name:'Prepared A',mode:'prepared'});
 const snapshot=exportSnapshot(audio);await audio.loadDeck(1,stems(ctx),{name:'B',mode:'demo'});audio.select(1);await audio.unloadDeck(0);
 assert.deepEqual(snapshot.buffers,buffers);assert.equal(snapshot.name,'Prepared A');assert.equal(snapshot.mode,'prepared');assert.equal(snapshot.deck,0);
});

test('cancelling prepared installation during context resume preserves existing playing audio',async()=>{
 const {audio,ctx}=fixture();const originals=stems(ctx),prepared=stems(ctx,16);await audio.loadDeck(1,originals,{name:'B'});await audio.playDeck(1);
 const sources=audio.decks[1].sources;let current=true,resume;ctx.resume=()=>new Promise(resolve=>{resume=resolve;});
 const installing=audio.installPreparedDeck(1,prepared,{stemPitch:[2,0,0,0],isCurrent:()=>current});current=false;resume();
 assert.equal(await installing,false);assert.equal(audio.decks[1].sources,sources);assert.deepEqual(audio.decks[1].buffers,originals);assert.equal(audio.decks[1].playing,true);
});
