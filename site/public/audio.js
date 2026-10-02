import { clamp } from './core.js';
export class AudioEngine {
  constructor() {
    this.ctx = null; this.buffers = []; this.sources = []; this.gains = [1, 1, 1, 1]; this.mutes = [false, false, false, false]; this.solos = [false, false, false, false]; this.playing = false; this.offset = 0; this.speed = 1; this.volume = .75; this.loop = true; this.duration = 0;
  }
  async init() {
    if (!this.ctx) {
      this.ctx = new AudioContext({ sampleRate: 44100 }); this.master = this.ctx.createGain(); this.master.gain.value = this.volume;
      this.limiter = this.ctx.createDynamicsCompressor(); this.limiter.threshold.value = -2; this.limiter.knee.value = 0; this.limiter.ratio.value = 20; this.limiter.attack.value = .003; this.limiter.release.value = .12;
      this.master.connect(this.limiter); this.limiter.connect(this.ctx.destination);
      this.trackGains = Array.from({length:4}, () => { const g = this.ctx.createGain(); g.connect(this.master); return g; });
      this.analysers = this.trackGains.map(g => { const a = this.ctx.createAnalyser(); a.fftSize = 256; g.connect(a); return a; });
    }
    await this.ctx.resume();
  }
  async decode(file) { await this.init(); return this.ctx.decodeAudioData(await file.arrayBuffer()); }
  async load(buffers) {
    await this.init(); this.pause(); this.offset = 0; this.buffers = buffers;
    this.duration = Math.max(0, ...buffers.filter(Boolean).map(b => b.duration)); this.refreshGains();
  }
  async play() {
    if (!this.duration || this.playing) return;
    await this.init(); this.playing = true; this.started = this.ctx.currentTime;
    const when = this.ctx.currentTime + .04; this.started = when;
    this.sources = this.buffers.map((buffer, i) => {
      if (!buffer) return null;
      const source = this.ctx.createBufferSource(); source.buffer = buffer; source.loop = false; source.playbackRate.value = this.speed;
      source.connect(this.trackGains[i]);
      if (this.offset < buffer.duration) source.start(when, this.offset); return source;
    });
  }
  position() { return this.playing ? Math.min(this.duration, this.offset + Math.max(0, this.ctx.currentTime - this.started) * this.speed) : this.offset; }
  pause() { if (this.playing) this.offset = this.position(); this.playing = false; this.sources.forEach(s => { try { s?.stop(); s?.disconnect(); } catch {} }); this.sources = []; }
  async seek(seconds) { const running = this.playing; this.pause(); this.offset = clamp(seconds, 0, Math.max(0, this.duration - .001)); if (running) await this.play(); }
  async rate(semitones) { const running = this.playing; this.pause(); this.speed = 2 ** (semitones / 12); if (running) await this.play(); }
  async toggle() { if (this.playing) this.pause(); else { if (this.offset >= this.duration - .01) this.offset = 0; await this.play(); } }
  tick() { if (this.playing && this.position() >= this.duration) { this.pause(); this.offset = 0; if (this.loop) this.play(); } }
  refreshGains() { const anySolo = this.solos.some(Boolean); this.trackGains?.forEach((g,i) => g.gain.setTargetAtTime(this.gains[i] * (!this.mutes[i] && (!anySolo || this.solos[i]) ? 1 : 0), this.ctx.currentTime, .015)); }
  setGain(i, value) { this.gains[i] = clamp(value); this.refreshGains(); }
  mute(i) { this.mutes[i] = !this.mutes[i]; this.refreshGains(); }
  solo(i) { this.solos[i] = !this.solos[i]; this.refreshGains(); }
  setVolume(value) { this.volume = clamp(value); if (this.ctx) this.master.gain.setTargetAtTime(this.volume, this.ctx.currentTime, .02); }
  fromChannels(channels) { const b = this.ctx.createBuffer(channels.length, channels[0].length, 44100); channels.forEach((ch,i) => b.copyToChannel(ch, i)); return b; }
  demo() {
    const sr = 44100, length = sr * 16;
    const arrays = Array.from({length:4}, () => new Float32Array(length));
    let seed = 420;
    const random = () => { seed = (1664525 * seed + 1013904223) >>> 0; return seed / 4294967296 * 2 - 1; };
    for (let i = 0; i < length; i++) {
      const t = i / sr, beat = t % .5, eighth = t % .25, bar = Math.floor(t / 2) % 4;
      const root = [110, 130.8128, 87.307, 97.999][bar];
      arrays[0][i] = (Math.sin(t * 2 * Math.PI * root * 4) * .5 + Math.sin(t * 2 * Math.PI * root * 6) * .15) * Math.sin(Math.PI * beat / .5) ** 4 * .15;
      const kick = Math.sin(2 * Math.PI * (48 * beat + 6 * (1 - Math.exp(-beat * 35)))) * Math.exp(-beat * 17) * .55;
      const snare = (Math.floor(t / .5) % 2) * random() * Math.exp(-beat * 25) * .22;
      const hat = random() * Math.exp(-eighth * 100) * .07;
      arrays[1][i] = kick + snare + hat;
      arrays[2][i] = (Math.sin(2 * Math.PI * root * t) + .2 * Math.sin(4 * Math.PI * root * t)) * Math.exp(-beat * 5) * .21;
      arrays[3][i] = [1, 1.25, 1.5].reduce((a, r) => a + Math.sin(2 * Math.PI * root * r * 2 * t), 0) * .035 * Math.min(1, t / .05, (16 - t) / .05);
    }
    return arrays.map(a => this.fromChannels([a, a]));
  }
}
