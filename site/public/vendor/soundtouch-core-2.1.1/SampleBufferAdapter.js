import CircularSampleBuffer from './CircularSampleBuffer.js';
import FifoSampleBuffer from './FifoSampleBuffer.js';
class FifoSampleBufferAdapter {
    inputBuffer;
    constructor() {
        this.inputBuffer = null;
    }
    get frameCount() {
        return this.inputBuffer?.frameCount ?? 0;
    }
    clear() {
        this.inputBuffer = null;
    }
    syncFromInputBuffer(inputBuffer) {
        this.inputBuffer = inputBuffer;
    }
    extract(target, sourceFrameOffset, frameCount) {
        const buffer = this.inputBuffer;
        if (buffer === null) {
            return 0;
        }
        const availableFrames = Math.max(0, buffer.frameCount - sourceFrameOffset);
        const framesToExtract = Math.max(0, Math.min(frameCount, availableFrames));
        if (framesToExtract === 0) {
            return 0;
        }
        buffer.extract(target, sourceFrameOffset, framesToExtract);
        return framesToExtract;
    }
    receive(frameCount) {
        this.inputBuffer?.receive(frameCount);
    }
}
class CircularSampleBufferAdapter {
    circularBuffer;
    scratch;
    constructor() {
        this.circularBuffer = new CircularSampleBuffer();
        this.scratch = new Float32Array(0);
    }
    get frameCount() {
        return this.circularBuffer.frameCount;
    }
    clear() {
        this.circularBuffer.clear();
    }
    syncFromInputBuffer(inputBuffer) {
        if (inputBuffer instanceof FifoSampleBuffer) {
            const frames = inputBuffer.frameCount;
            if (frames === 0) {
                return;
            }
            this.circularBuffer.pushSamples(inputBuffer.vector, inputBuffer.position, frames);
            inputBuffer.receive(frames);
            return;
        }
        const frames = inputBuffer.frameCount;
        if (frames === 0) {
            return;
        }
        const sampleCount = frames * 2;
        if (this.scratch.length < sampleCount) {
            this.scratch = new Float32Array(sampleCount);
        }
        inputBuffer.extract(this.scratch, 0, frames);
        this.circularBuffer.pushSamples(this.scratch, 0, frames);
        inputBuffer.receive(frames);
    }
    extract(target, sourceFrameOffset, frameCount) {
        return this.circularBuffer.extract(target, sourceFrameOffset, frameCount, false);
    }
    receive(frameCount) {
        this.circularBuffer.dropFrames(frameCount);
    }
}
/** Creates an adapter that reads directly from any `SampleBuffer` contract. */
export const createFifoSampleBufferAdapter = () => new FifoSampleBufferAdapter();
/**
 * Creates an adapter that stages source frames in a circular buffer for
 * efficient repeated reads.
 */
export const createCircularSampleBufferAdapter = () => new CircularSampleBufferAdapter();
//# sourceMappingURL=SampleBufferAdapter.js.map