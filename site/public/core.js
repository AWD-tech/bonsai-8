export const NAMES = ['Vocals', 'Drums', 'Bass', 'Other'];
export const clamp = (v, lo = 0, hi = 1) => Math.max(lo, Math.min(hi, v));
export const time = (seconds) => `${Math.floor(seconds / 60).toString().padStart(2, '0')}:${Math.floor(seconds % 60).toString().padStart(2, '0')}`;
export function wav(channels, sampleRate, bits = 16) {
  if (!channels.length || channels.some(c => c.length !== channels[0].length)) throw new Error('Channels must have equal lengths.');
  const frames = channels[0].length, bytes = bits / 8, block = channels.length * bytes;
  const data = new ArrayBuffer(44 + frames * block), v = new DataView(data);
  const str = (p, s) => [...s].forEach((c, i) => v.setUint8(p + i, c.charCodeAt(0)));
  str(0, 'RIFF'); v.setUint32(4, data.byteLength - 8, true); str(8, 'WAVE'); str(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, channels.length, true);
  v.setUint32(24, sampleRate, true); v.setUint32(28, sampleRate * block, true);
  v.setUint16(32, block, true); v.setUint16(34, bits, true); str(36, 'data'); v.setUint32(40, frames * block, true);
  let p = 44;
  for (let i = 0; i < frames; i++) for (const ch of channels) {
    const value = clamp(ch[i], -1, 1);
    if (bits === 24) { const n = Math.round(value * (value < 0 ? 8388608 : 8388607)); v.setUint8(p, n & 255); v.setUint8(p + 1, (n >> 8) & 255); v.setUint8(p + 2, (n >> 16) & 255); }
    else v.setInt16(p, Math.round(value * (value < 0 ? 32768 : 32767)), true);
    p += bytes;
  }
  return data;
}
export function decodeControls(faders, buttons) {
  if (faders.length !== 4 || buttons.length !== 10) throw new Error('Unexpected SP-1 control packet.');
  // The bootloader reports sliders in reverse physical order. In the upright
  // device orientation, high values are at the top of each slot.
  return { gains: Array.from(faders).reverse().map(v => v / 255), buttons: Array.from(buttons, Boolean) };
}
export function transitionWindow(n, overlap) {
  return Float32Array.from({ length: n }, (_, i) => Math.min(1, (i + 1) / overlap, (n - i) / overlap));
}
