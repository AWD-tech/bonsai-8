import { clamp } from './core.js';

export const DEFAULT_AUDIO_BUDGET_BYTES = 768 * 1024 * 1024;
export function bufferBytes(buffer) {
  const length = Number(buffer?.length), channels = Number(buffer?.numberOfChannels);
  return Number.isSafeInteger(length) && length > 0 && Number.isSafeInteger(channels) && channels > 0 ? length * channels * 4 : 0;
}
export function uniqueBufferBytes(buffers) {
  return [...new Set(buffers.filter(Boolean))].reduce((total, buffer) => total + bufferBytes(buffer), 0);
}

const deckState = () => ({
  buffers: [], originalBuffers: [], sources: [], gains: [1, 1, 1, 1],
  mutes: [false, false, false, false], solos: [false, false, false, false],
  playing: false, offset: 0, started: 0, speed: 1, volume: 1,
  loop: true, duration: 0, token: 0,
  // Prepared pitch/stretched audio must preserve a shared deck timeline.
  // These fields deliberately do not control independent playbackRate values.
  stemPitch: [0, 0, 0, 0], preparedStems: [null, null, null, null],
  analysis: null, media: { name: '', mode: 'empty', original: null, libraryId: null },
});

export class AudioEngine {
  constructor({ contextFactory, memoryBudgetBytes = DEFAULT_AUDIO_BUDGET_BYTES } = {}) {
    this.ctx = null; this.selected = 0; this.decks = [deckState(), deckState()];
    if (!Number.isSafeInteger(memoryBudgetBytes) || memoryBudgetBytes <= 0) throw new RangeError('Set a positive decoded-audio memory budget.');
    this.volume = .75; this.contextFactory = contextFactory;
    this.memoryBudgetBytes = memoryBudgetBytes; this.sessionBuffers = () => [];
  }
  deck(index = this.selected) {
    if (index !== 0 && index !== 1) throw new RangeError('Choose deck A or B.');
    return this.decks[index];
  }
  select(index) { this.deck(index); this.selected = index; }
  referencedBuffers(includeSession = true) {
    const buffers = this.decks.flatMap(deck => [...deck.buffers, ...deck.originalBuffers, ...deck.preparedStems, deck.media.original]);
    if (includeSession) buffers.push(...this.sessionBuffers());
    return buffers.filter(Boolean);
  }
  memoryUsage(extraBuffers = []) { return uniqueBufferBytes([...this.referencedBuffers(), ...extraBuffers]); }
  ensureCapacity(extraBuffers = [], reserveBytes = 0) {
    const usage = this.memoryUsage(extraBuffers);
    if (!Number.isFinite(reserveBytes) || reserveBytes < 0 || usage + reserveBytes > this.memoryBudgetBytes) {
      throw new Error(`This tab has a ${Math.round(this.memoryBudgetBytes / 1024 / 1024)} MB decoded-audio budget. Remove unused songs from the session, or use shorter audio before loading more. Playing decks were kept intact.`);
    }
    return usage + reserveBytes;
  }
  get buffers() { return this.deck().buffers; }
  get sources() { return this.deck().sources; }
  get gains() { return this.deck().gains; }
  get mutes() { return this.deck().mutes; }
  set mutes(values) { this.deck().mutes = [...values]; this.refreshGains(); }
  get solos() { return this.deck().solos; }
  get playing() { return this.deck().playing; }
  get offset() { return this.deck().offset; }
  set offset(value) { this.deck().offset = value; }
  get speed() { return this.deck().speed; }
  get duration() { return this.deck().duration; }
  get loop() { return this.deck().loop; }
  set loop(value) {
    const deck = this.deck(); deck.loop = Boolean(value);
    deck.sources.forEach(source => { if (source) source.loop = deck.loop; });
  }
  get trackGains() { return this.deck().trackGains; }
  get analysers() { return this.deck().analysers; }
  async init() {
    if (!this.ctx) {
      this.ctx = this.contextFactory ? this.contextFactory() : new AudioContext({ sampleRate: 44100 });
      this.master = this.ctx.createGain(); this.master.gain.value = this.volume;
      // Eight full-scale, correlated stems cannot exceed unity before master.
      this.headroom = this.ctx.createGain(); this.headroom.gain.value = 1 / 8;
      this.headroom.connect(this.master);
      this.limiter = this.ctx.createDynamicsCompressor();
      // A 0 dB threshold avoids the native compressor’s automatic makeup
      // boost for ordinary below-threshold audio. Static headroom does the
      // normal mixing protection, so this stage is only an overs safety net.
      this.limiter.threshold.value = 0; this.limiter.knee.value = 0;
      this.limiter.ratio.value = 20; this.limiter.attack.value = .003;
      this.limiter.release.value = .12;
      this.master.connect(this.limiter); this.limiter.connect(this.ctx.destination);
      for (const deck of this.decks) {
        deck.bus = this.ctx.createGain(); deck.bus.gain.value = deck.volume;
        deck.bus.connect(this.headroom);
        deck.trackGains = Array.from({ length: 4 }, () => {
          const gain = this.ctx.createGain(); gain.connect(deck.bus); return gain;
        });
        deck.analysers = deck.trackGains.map(gain => {
          const analyser = this.ctx.createAnalyser(); analyser.fftSize = 256;
          gain.connect(analyser); return analyser;
        });
      }
      this.refreshGains(0); this.refreshGains(1);
    }
    await this.ctx.resume();
  }
  async decode(file) {
    this.ensureCapacity([], Number(file.size) || 0);
    await this.init(); const buffer = await this.ctx.decodeAudioData(await file.arrayBuffer());
    this.ensureCapacity([buffer]); return buffer;
  }
  async load(buffers) { return this.loadDeck(this.selected, buffers); }
  async loadDeck(index, buffers, media, { isCurrent = () => true } = {}) {
    const deck = this.deck(index);
    if (!Array.isArray(buffers) || buffers.length > 4 || buffers.some(b => b && (!Number.isFinite(b.duration) || b.duration <= 0))) {
      throw new Error('Load up to four valid audio stems.');
    }
    const token = ++deck.token;
    await this.init();
    if (token !== deck.token || !isCurrent()) return false;
    const incoming = Array.from({ length: 4 }, (_, i) => buffers[i] || null);
    const duration = Math.max(0, ...incoming.filter(Boolean).map(buffer => buffer.duration));
    const padding = incoming.reduce((total, buffer) => !buffer || buffer.duration >= duration - 1e-8 ? total :
      total + Math.ceil(duration * buffer.sampleRate) * buffer.numberOfChannels * 4, 0);
    this.ensureCapacity([...incoming, media?.original], padding);
    // Only the target deck is stopped. Other sources and clocks remain intact.
    this.pauseDeck(index); deck.offset = 0;
    deck.originalBuffers = Array.from({ length: 4 }, (_, i) => buffers[i] || null);
    deck.duration = Math.max(0, ...deck.originalBuffers.filter(Boolean).map(b => b.duration));
    // Native Web Audio loops run on the audio clock. Equal lengths keep a short
    // stem silent at the tail instead of independently looping it early.
    deck.buffers = deck.originalBuffers.map(buffer => {
      if (!buffer || buffer.duration >= deck.duration - 1e-8) return buffer;
      const padded = this.ctx.createBuffer(buffer.numberOfChannels,
        Math.ceil(deck.duration * buffer.sampleRate), buffer.sampleRate);
      for (let channel = 0; channel < buffer.numberOfChannels; channel++) {
        padded.copyToChannel(buffer.getChannelData(channel), channel);
      }
      return padded;
    });
    deck.preparedStems.fill(null); deck.stemPitch.fill(0); deck.analysis = null;
    if (media) deck.media = { ...deck.media, ...media };
    this.refreshGains(index); return true;
  }
  async installPreparedDeck(index, buffers, { stemPitch = [0, 0, 0, 0], analysis = null, isCurrent = () => true } = {}) {
    const deck = this.deck(index), originals = deck.originalBuffers;
    const loaded = await this.loadDeck(index, buffers, undefined, { isCurrent });
    if (!loaded) return false;
    deck.originalBuffers = originals;
    deck.preparedStems = [...deck.buffers];
    deck.stemPitch = [...stemPitch]; deck.analysis = analysis;
    deck.speed = 1; return true;
  }
  async play() { return this.playDeck(this.selected); }
  async playDeck(index, when) {
    const deck = this.deck(index);
    if (!deck.duration || deck.playing) return false;
    const token = ++deck.token;
    await this.init();
    if (token !== deck.token) return false;
    if (deck.offset >= deck.duration - .001) deck.offset = 0;
    if (when !== undefined && (!Number.isFinite(when) || when < 0)) throw new RangeError('Use a future audio-clock start time.');
    deck.started = when === undefined ? this.ctx.currentTime + .04 : Math.max(this.ctx.currentTime, when);
    deck.playing = true;
    deck.sources = deck.buffers.map((buffer, i) => {
      if (!buffer) return null;
      const source = this.ctx.createBufferSource();
      source.buffer = buffer; source.loop = deck.loop;
      source.loopStart = 0; source.loopEnd = deck.duration;
      source.playbackRate.value = deck.speed;
      source.connect(deck.trackGains[i]); source.start(deck.started, deck.offset);
      return source;
    });
    return true;
  }
  async playBoth() {
    await this.init(); const when = this.ctx.currentTime + .04;
    await Promise.all([this.playDeck(0, when), this.playDeck(1, when)]);
  }
  position(index = this.selected) {
    const deck = this.deck(index);
    const position = deck.playing ? deck.offset + Math.max(0, this.ctx.currentTime - deck.started) * deck.speed : deck.offset;
    return deck.loop && deck.playing && deck.duration ? position % deck.duration : Math.min(deck.duration, position);
  }
  pause() { this.pauseDeck(this.selected); }
  pauseDeck(index) {
    const deck = this.deck(index); ++deck.token;
    if (deck.playing) deck.offset = this.position(index);
    deck.playing = false;
    deck.sources.forEach(source => { try { source?.stop(); source?.disconnect(); } catch {} });
    deck.sources = [];
  }
  pauseAll() { this.pauseDeck(0); this.pauseDeck(1); }
  async seek(seconds) { return this.seekDeck(this.selected, seconds); }
  async seekDeck(index, seconds) {
    const deck = this.deck(index), running = deck.playing;
    this.pauseDeck(index);
    deck.offset = clamp(Number(seconds) || 0, 0, Math.max(0, deck.duration - .001));
    if (running) await this.playDeck(index);
  }
  cueDeck(index) { this.pauseDeck(index); this.deck(index).offset = 0; }
  async rate(semitones) { return this.rateDeck(this.selected, semitones); }
  async rateDeck(index, semitones) {
    const deck = this.deck(index), speed = 2 ** (clamp(Number(semitones) || 0, -12, 12) / 12);
    if (deck.playing) {
      deck.offset = this.position(index);
      deck.started = Math.max(this.ctx.currentTime, deck.started);
      for (const source of deck.sources) if (source) {
        if (source.playbackRate.setValueAtTime) source.playbackRate.setValueAtTime(speed, this.ctx.currentTime);
        else source.playbackRate.value = speed;
      }
    }
    deck.speed = speed;
  }
  async toggle() { return this.toggleDeck(this.selected); }
  async toggleDeck(index) { if (this.deck(index).playing) this.pauseDeck(index); else await this.playDeck(index); }
  tick() {
    for (let index = 0; index < 2; index++) {
      const deck = this.deck(index);
      if (deck.playing && !deck.loop && this.position(index) >= deck.duration) {
        this.pauseDeck(index); deck.offset = deck.duration;
      }
    }
  }
  refreshGains(index = this.selected) {
    const deck = this.deck(index), anySolo = deck.solos.some(Boolean);
    deck.trackGains?.forEach((gain, i) => gain.gain.setTargetAtTime(
      deck.gains[i] * (!deck.mutes[i] && (!anySolo || deck.solos[i]) ? 1 : 0), this.ctx.currentTime, .015));
  }
  setGain(i, value, index = this.selected) { this.deck(index).gains[i] = clamp(value); this.refreshGains(index); }
  mute(i, index = this.selected) { const deck = this.deck(index); deck.mutes[i] = !deck.mutes[i]; this.refreshGains(index); }
  solo(i, index = this.selected) { const deck = this.deck(index); deck.solos[i] = !deck.solos[i]; this.refreshGains(index); }
  setDeckVolume(index, value) {
    const deck = this.deck(index); deck.volume = clamp(value);
    deck.bus?.gain.setTargetAtTime(deck.volume, this.ctx.currentTime, .02);
  }
  setVolume(value) { this.volume = clamp(value); this.master?.gain.setTargetAtTime(this.volume, this.ctx.currentTime, .02); }
  resetDeck(index = this.selected) {
    const deck = this.deck(index); deck.mutes.fill(false); deck.solos.fill(false); deck.gains.fill(1);
    this.refreshGains(index); this.setDeckVolume(index, 1); return this.rateDeck(index, 0);
  }
  unloadDeck(index) {
    const deck = this.deck(index); this.pauseDeck(index);
    deck.buffers = []; deck.originalBuffers = []; deck.duration = deck.offset = 0;
    deck.preparedStems.fill(null); deck.stemPitch.fill(0); deck.analysis = null;
    deck.media = { name: '', mode: 'empty', original: null, libraryId: null };
    return this.resetDeck(index);
  }
  fromChannels(channels, sampleRate = 44100) {
    this.ensureCapacity([], channels.length * channels[0].length * 4);
    const buffer = this.ctx.createBuffer(channels.length, channels[0].length, sampleRate);
    channels.forEach((channel, i) => buffer.copyToChannel(channel, i)); return buffer;
  }
  demo({ variant = 0 } = {}) {
    const sr = 44100, length = sr * 16;
    const arrays = Array.from({ length: 4 }, () => new Float32Array(length));
    let seed = 420 + variant;
    const random = () => { seed = (1664525 * seed + 1013904223) >>> 0; return seed / 4294967296 * 2 - 1; };
    for (let i = 0; i < length; i++) {
      const t = i / sr, beat = t % .5, eighth = t % .25, bar = Math.floor(t / 2) % 4;
      const root = [110, 130.8128, 87.307, 97.999][bar] * (variant ? 2 : 1);
      arrays[0][i] = (Math.sin(t * 2 * Math.PI * root * 4) * .5 + Math.sin(t * 2 * Math.PI * root * 6) * .15) * Math.sin(Math.PI * beat / .5) ** 4 * .15;
      arrays[1][i] = Math.sin(2 * Math.PI * (48 * beat + 6 * (1 - Math.exp(-beat * 35)))) * Math.exp(-beat * 17) * .55 +
        (Math.floor(t / .5) % 2) * random() * Math.exp(-beat * 25) * .22 + random() * Math.exp(-eighth * 100) * .07;
      arrays[2][i] = (Math.sin(2 * Math.PI * root * t) + .2 * Math.sin(4 * Math.PI * root * t)) * Math.exp(-beat * 5) * .21;
      arrays[3][i] = [1, 1.25, 1.5].reduce((a, ratio) => a + Math.sin(2 * Math.PI * root * ratio * 2 * t), 0) * .035 * Math.min(1, t / .05, (16 - t) / .05);
    }
    return arrays.map(array => this.fromChannels([array, array]));
  }
}
