import test from 'node:test';
import assert from 'node:assert/strict';
import { AudioEngine } from '../public/audio.js';
import { decodeStemFiles } from '../public/import-stems.js';

function fixture(memoryBudgetBytes) {
  const audio = new AudioEngine({ memoryBudgetBytes });
  const buffer = { length: 150, numberOfChannels: 2, sampleRate: 100, duration: 1.5 };
  let reads = 0, decodes = 0;
  audio.init = async () => {};
  audio.ctx = { async decodeAudioData() { decodes++; return { ...buffer }; } };
  const file = { size: 100, async arrayBuffer() { reads++; return new ArrayBuffer(100); } };
  return { audio, file, counts: () => ({ reads, decodes }) };
}

test('manual import accounts for previously decoded stems before reading the next file', async () => {
  const { audio, file, counts } = fixture(2400);
  const first = await audio.decode(file);
  const second = await audio.decode(file, { retainedBuffers: [first] });
  await assert.rejects(audio.decode(file, { retainedBuffers: [first, second] }), /decoded-audio budget/);
  assert.deepEqual(counts(), { reads: 2, decodes: 2 });
});

test('a newly decoded stem is checked together with pending imports before continuing', async () => {
  const { audio, file, counts } = fixture(2000);
  const first = await audio.decode(file);
  await assert.rejects(audio.decode(file, { retainedBuffers: [first] }), /decoded-audio budget/);
  assert.deepEqual(counts(), { reads: 2, decodes: 2 });
  assert.deepEqual(audio.decks.map(deck => deck.buffers), [[], []], 'an oversized import does not replace either deck');
});

test('manual import stops at the budget limit before decoding all four files', async () => {
  const { audio, file, counts } = fixture(2400), progress = [];
  await assert.rejects(decodeStemFiles(audio, [file, file, file, file], index => progress.push(index)), /decoded-audio budget/);
  assert.deepEqual(counts(), { reads: 2, decodes: 2 });
  assert.deepEqual(progress, [0, 1, 2]);
});

test('manual stem decoding preserves order, samples, silence and relative levels without DSP', async () => {
  const { audio, file } = fixture(10000);
  const left = Float32Array.from([0, .125, -.5, .75, 0]);
  const right = Float32Array.from([.1, -.25, 0, .4, -.6]);
  const decoded = { length: 5, numberOfChannels: 2, sampleRate: 44100, duration: 5 / 44100,
    getChannelData: channel => channel ? right : left };
  audio.ctx.decodeAudioData = async () => decoded;
  const result = await decodeStemFiles(audio, [file, null, file, null]);
  assert.deepEqual(result, [decoded, null, decoded, null]);
  assert.deepEqual([...result[0].getChannelData(0)], [...left]);
  assert.deepEqual([...result[2].getChannelData(1)], [...right]);
  assert.equal(audio.decks[0].analysis, null);
  assert.deepEqual(audio.decks[0].preparedStems, [null, null, null, null]);
});

test('manual import rejects multichannel audio instead of silently downmixing it', async () => {
  const { audio, file } = fixture(10000);
  audio.ctx.decodeAudioData = async () => ({ length: 100, sampleRate: 100, duration: 1, numberOfChannels: 6 });
  await assert.rejects(decodeStemFiles(audio, [file]), /not mixed down automatically/);
  assert.deepEqual(audio.decks.map(deck => deck.buffers), [[], []]);
});
