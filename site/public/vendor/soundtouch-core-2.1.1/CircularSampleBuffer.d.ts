import type { SampleBuffer } from './SampleBuffer.js';
/**
 * Circular frame buffer for interleaved stereo audio samples.
 *
 * @remarks
 * Implements a ring buffer for stereo audio, where each frame consists of two contiguous float values (left, right).
 * Maintains a movable read cursor and appends at the logical end. Capacity grows automatically as needed while preserving frame order.
 * Used for efficient, low-latency audio processing where buffer wraparound and dynamic resizing are required.
 */
export default class CircularSampleBuffer implements SampleBuffer {
    private _buffer;
    private _capacityFrames;
    private _readFrame;
    private _frameCount;
    /**
     * @param capacityFrames Initial frame capacity before automatic growth.
     */
    constructor(capacityFrames?: number);
    /**
     * Allocated capacity expressed in frames.
     * @returns The number of frames the buffer can currently hold without resizing.
     */
    get capacityFrames(): number;
    /**
     * Number of buffered frames currently readable.
     * @returns The number of frames available for reading.
     */
    get frameCount(): number;
    /**
     * Clears the buffer without shrinking allocated capacity.
     *
     * @remarks
     * Resets the read cursor and frame count, but does not deallocate the underlying storage.
     */
    clear(): void;
    /**
     * Ensures the internal storage can hold at least `minCapacityFrames`.
     *
     * @param minCapacityFrames Minimum frame capacity required.
     * @remarks
     * Grows the buffer if needed, preserving all readable frames in order.
     */
    ensureCapacity(minCapacityFrames: number): void;
    /**
     * Appends source frames to the end of the ring.
     *
     * @param source Interleaved stereo source samples.
     * @param sourceFrameOffset Source offset in frames.
     * @param frameCount Number of frames to append; defaults to all complete remaining frames.
     * @remarks
     * Automatically grows the buffer if needed. Only complete frames are appended.
     */
    pushSamples(source: Float32Array, sourceFrameOffset?: number, frameCount?: number): void;
    /**
     * Contract alias for `pushSamples`.
     *
     * @param source Interleaved stereo source samples.
     * @param sourceFrameOffset Source offset in frames.
     * @param frameCount Number of frames to append.
     */
    putSamples(source: Float32Array, sourceFrameOffset?: number, frameCount?: number): void;
    /**
     * Extracts frames from the ring into `target`.
     *
     * @param target Destination array for interleaved stereo samples.
     * @param sourceFrameOffset Read offset in frames.
     * @param frameCount Number of frames requested.
     * @param consume When true, consumed frames are dropped from the front.
     * @returns Number of frames copied.
     * @remarks
     * If `consume` is true, the extracted frames are removed from the buffer.
     */
    extract(target: Float32Array, sourceFrameOffset?: number, frameCount?: number, consume?: boolean): number;
    /**
     * Reads a single sample value by logical sample index.
     *
     * @param sampleIndex Logical sample index relative to the readable head.
     * @returns Sample value, or `0` when the index falls outside readable data.
     * @remarks
     * Used for random access to individual samples within the readable region.
     */
    readSample(sampleIndex: number): number;
    /**
     * Drops frames from the front of the ring.
     *
     * @param frameCount Maximum number of frames to remove.
     * @returns Number of frames removed.
     * @remarks
     * Advances the read cursor and reduces the frame count. If all frames are dropped, resets the read cursor.
     */
    dropFrames(frameCount: number): number;
    /**
     * Contract alias for `dropFrames`.
     *
     * @param frameCount Number of frames to consume.
     */
    receive(frameCount?: number): void;
}
//# sourceMappingURL=CircularSampleBuffer.d.ts.map