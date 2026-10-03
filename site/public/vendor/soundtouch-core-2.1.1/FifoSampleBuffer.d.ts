/**
 * Resizable interleaved sample buffer for audio processing.
 *
 * @remarks
 * Stores stereo audio samples in a contiguous Float32Array and provides methods for efficient buffer management and sample transfer.
 * Uses ES2024 ArrayBuffer for zero-allocation growth. Suitable for scenarios where buffer size may need to grow dynamically during audio processing.
 */
export default class FifoSampleBuffer {
    /**
     * Backing ArrayBuffer for sample storage.
     * @remarks
     * Underlying memory for the buffer, which may be resized as needed.
     */
    private _buffer;
    /**
     * Float32Array view of the buffer.
     * @remarks
     * Provides direct access to the sample data for reading and writing.
     */
    private _vector;
    /**
     * Current read position (frame index).
     * @remarks
     * Indicates the logical start of readable data within the buffer.
     */
    private _position;
    /**
     * Number of frames currently stored.
     * @remarks
     * Represents the number of complete stereo frames available for reading.
     */
    private _frameCount;
    /**
     * Creates a new FifoSampleBuffer.
     * @param maxFrames Maximum number of frames for buffer allocation.
     */
    constructor(maxFrames?: number);
    /**
     * Returns the Float32Array view of the buffer.
     * @returns The Float32Array containing the sample data.
     */
    get vector(): Float32Array;
    /**
     * Returns the current read position (frame index).
     * @returns The current frame index for reading.
     */
    get position(): number;
    /**
     * Returns the start sample index for reading.
     * @returns The sample index corresponding to the start of readable data.
     */
    get startIndex(): number;
    /**
     * Returns the number of frames currently stored.
     * @returns The number of complete frames available for reading.
     */
    get frameCount(): number;
    /**
     * Returns the end sample index for reading.
     * @returns The sample index corresponding to the end of readable data.
     */
    get endIndex(): number;
    /**
     * Clears the buffer and resets position and frame count.
     * @remarks
     * Fills the buffer with zeros and resets all internal state.
     */
    clear(): void;
    /**
     * Adds empty frames to the buffer.
     * @param numFrames Number of frames to add.
     */
    put(numFrames: number): void;
    /**
     * Adds samples to the buffer from a Float32Array.
     * @param samples Source samples (interleaved stereo).
     * @param position Start frame index in source.
     * @param numFrames Number of frames to copy (default: all available).
     * @remarks
     * Automatically grows the buffer if needed. Only complete frames are appended.
     */
    putSamples(samples: Float32Array, position?: number, numFrames?: number): void;
    /**
     * Adds samples from another FifoSampleBuffer.
     * @param buffer Source buffer.
     * @param position Start frame index in source buffer.
     * @param numFrames Number of frames to copy (default: all available).
     */
    putBuffer(buffer: FifoSampleBuffer, position?: number, numFrames?: number): void;
    /**
     * Advances the read position and reduces frame count.
     * @param numFrames Number of frames to receive (default: all available).
     * @remarks
     * Consumed frames are no longer available for reading.
     */
    receive(numFrames?: number): void;
    /**
     * Copies and receives samples into an output array.
     * @param output Destination Float32Array.
     * @param numFrames Number of frames to copy and receive.
     * @remarks
     * Advances the read position after copying.
     */
    receiveSamples(output: Float32Array, numFrames?: number): void;
    /**
     * Extracts samples into an output array without advancing position.
     * @param output Destination Float32Array.
     * @param position Start frame index in buffer.
     * @param numFrames Number of frames to extract.
     */
    extract(output: Float32Array, position?: number, numFrames?: number): void;
    /**
     * Ensures the buffer has capacity for at least numFrames.
     * @param numFrames Minimum number of frames required.
     * @remarks
     * Grows the buffer if needed, preserving all readable frames in order.
     */
    ensureCapacity(numFrames?: number): void;
    /**
     * Ensures buffer has capacity for additional frames.
     * @param numFrames Number of additional frames required.
     */
    ensureAdditionalCapacity(numFrames?: number): void;
    /**
     * Moves all unread samples to the start of the buffer.
     * @remarks
     * Compacts the buffer so that all unread samples are at the beginning, freeing space for new data.
     */
    rewind(): void;
}
//# sourceMappingURL=FifoSampleBuffer.d.ts.map