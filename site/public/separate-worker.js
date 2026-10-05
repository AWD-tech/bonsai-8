import { transitionWindow } from './core.js';
const MODEL = 'https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx';
const N = 343980, OVERLAP = Math.floor(N / 4), STEP = N - OVERLAP;
const status = (message, progress) => postMessage({ type: 'progress', message, progress });
let ort, session, phase = 'starting';
async function modelBytes() {
  let cache; try { cache = await caches.open('sp1-model-v1'); const cached = await cache.match(MODEL); if (cached) { status('Loading saved separation model…', .12); return cached.arrayBuffer(); } } catch {}
  status('Downloading separation model · 158 MB', .01);
  const res = await fetch(MODEL); if (!res.ok) throw new Error(`Model download failed (${res.status}). Please retry.`);
  const reader = res.body.getReader(), chunks = []; let bytes = 0;
  while (true) { const { done, value } = await reader.read(); if (done) break; chunks.push(value); bytes += value.length; status(`Downloading model · ${Math.round(bytes / 1048576)} / 158 MB`, .02 + .18 * bytes / 165612636); }
  const merged = new Uint8Array(bytes); let offset = 0; for (const ch of chunks) { merged.set(ch, offset); offset += ch.length; }
  try { await cache?.put(MODEL, new Response(merged)); } catch { /* Low disk space must not prevent inference. */ }
  return merged.buffer;
}
self.onmessage = async ({ data }) => {
  try {
    if (!session) {
      ort = await import('https://cdn.jsdelivr.net/npm/onnxruntime-web@1.22.0/dist/ort.wasm.min.mjs');
      ort.env.wasm.wasmPaths = 'https://cdn.jsdelivr.net/npm/onnxruntime-web@1.22.0/dist/';
      ort.env.wasm.numThreads = 1; ort.env.wasm.proxy = false;
      const bytes = await modelBytes(); status('Preparing the model. First use can take a minute…', .22);
      phase = 'loading model';
      session = await ort.InferenceSession.create(bytes, { executionProviders: ['wasm'], graphOptimizationLevel: 'disabled', enableMemPattern: false, executionMode: 'sequential' });
    }
    const channels = data.channels, length = channels[0].length;
    const output = Array.from({length:4}, () => [new Float32Array(length), new Float32Array(length)]);
    const weights = new Float32Array(length), window = transitionWindow(N, OVERLAP);
    const count = Math.max(1, Math.ceil(Math.max(0, length - N) / STEP) + 1);
    for (let k = 0; k < count; k++) {
      status(`Separating · section ${k + 1} of ${count}`, .25 + .73 * k / count);
      const start = k * STEP, size = Math.min(N, length - start), input = new Float32Array(2 * N);
      for (let c = 0; c < 2; c++) input.set(channels[c].subarray(start, start + size), c * N);
      const tensor = new ort.Tensor('float32', input, [1, 2, N]);
      phase = 'separating audio';
      const result = await session.run({ mix: tensor }); const values = result.stems.data;
      if (values.length !== 8 * N) throw new Error('Unexpected model output.');
      // Model rows: drums, bass, other, vocals. Player rows: vocals, drums, bass, other.
      [3, 0, 1, 2].forEach((source, track) => {
        for (let c = 0; c < 2; c++) for (let i = 0; i < size; i++) output[track][c][start + i] += values[(source * 2 + c) * N + i] * window[i];
      });
      for (let i = 0; i < size; i++) weights[start + i] += window[i];
      tensor.dispose(); Object.values(result).forEach(t => t.dispose());
    }
    for (const stem of output) for (const channel of stem) for (let i = 0; i < length; i++) {
      channel[i] /= Math.max(weights[i], 1e-8);
      if (!Number.isFinite(channel[i])) throw new Error('The model returned invalid audio.');
    }
    postMessage({ type: 'done', stems: output }, output.flat().map(ch => ch.buffer));
  } catch (error) { postMessage({ type: 'error', message: `${phase}: ${String(error.message || error)}` }); }
};
