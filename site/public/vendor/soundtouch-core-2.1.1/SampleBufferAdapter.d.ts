import type { SampleBuffer } from './SampleBuffer.js';
/**
 * Read/consume adapter used by processors that need consistent extraction
 * semantics regardless of concrete input buffer implementation.
 */
export interface SampleBufferAdapter {
    /** Number of frames currently available through the adapter view. */
    readonly frameCount: number;
    /** Clears all adapter-local state and temporary storage. */
    clear(): void;
    /**
     * Synchronizes adapter state from the latest input buffer contents.
     *
     * @param inputBuffer Source buffer for adapter reads.
     */
    syncFromInputBuffer(inputBuffer: SampleBuffer): void;
    /**
     * Copies frames from adapter storage into `target`.
     *
     * @param target Target interleaved stereo array.
     * @param sourceFrameOffset Source offset in frames.
     * @param frameCount Maximum number of frames to copy.
     * @returns Number of frames copied.
     */
    extract(target: Float32Array, sourceFrameOffset: number, frameCount: number): number;
    /**
     * Consumes frames previously exposed through the adapter.
     *
     * @param frameCount Number of frames to consume.
     */
    receive(frameCount: number): void;
}
/** Factory for creating adapter instances. */
export type SampleBufferAdapterFactory = () => SampleBufferAdapter;
/** Creates an adapter that reads directly from any `SampleBuffer` contract. */
export declare const createFifoSampleBufferAdapter: SampleBufferAdapterFactory;
/**
 * Creates an adapter that stages source frames in a circular buffer for
 * efficient repeated reads.
 */
export declare const createCircularSampleBufferAdapter: SampleBufferAdapterFactory;
//# sourceMappingURL=SampleBufferAdapter.d.ts.map