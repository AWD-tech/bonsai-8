// Import existing stems directly. Separation and optional Match songs are
// separate user actions; no analysis, gain change, or time alignment runs here.
export async function decodeStemFiles(audio, files, onProgress = () => {}) {
  if (!Array.isArray(files) || files.length > 4 || !files.some(Boolean)) throw new Error('Choose one to four stem files.');
  const buffers = [];
  for (let index = 0; index < files.length; index++) {
    const file = files[index];
    if (!file) { buffers.push(null); continue; }
    if (file.size > 200 * 1024 * 1024) throw new Error('Each stem must be smaller than 200 MB.');
    onProgress(index);
    const buffer = await audio.decode(file, { retainedBuffers: buffers });
    if (buffer.duration > 600) throw new Error('Each stem must be under 10 minutes.');
    if (buffer.numberOfChannels > 2) throw new Error('Use a mono or stereo file for each stem. Multichannel stems are not mixed down automatically.');
    buffers.push(buffer);
  }
  return buffers;
}
