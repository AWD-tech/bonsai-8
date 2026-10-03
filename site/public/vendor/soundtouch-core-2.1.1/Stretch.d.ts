import AbstractSamplePipe from './AbstractSamplePipe.js';
import type { SampleBuffer } from './SampleBuffer.js';
import type { StretchPipe } from './StretchPipe.js';
/**
 * Read adapter used by `Stretch` so input access is decoupled from concrete
 * buffer implementations.
 */
interface StretchReadBufferAdapter {
    /** Number of readable frames currently exposed by the adapter. */
    readonly frameCount: number;
    /** Sample-index origin used by callers that operate on absolute offsets. */
    readonly startIndex: number;
    /**
     * Binds the adapter to a new source buffer.
     * @param buffer Source buffer to read from.
     */
    setBuffer(buffer: SampleBuffer): void;
    /**
     * Reads a single sample at an absolute sample index.
     * @param sampleIndex Absolute sample index.
     */
    readSample(sampleIndex: number): number;
    /**
     * Returns a contiguous sample range.
     * @param start Inclusive start sample index.
     * @param end Exclusive end sample index.
     */
    readSubarray(start: number, end: number): Float32Array;
    /**
     * Consumes frames from the adapter source.
     * @param numFrames Number of frames to consume.
     */
    receive(numFrames: number): void;
    /**
     * Extracts and consumes frames into `output`.
     * @param output Destination sample array.
     * @param numFrames Number of frames to extract.
     */
    receiveSamples(output: Float32Array, numFrames: number): void;
}
/** Factory for stretch input adapters. */
type StretchInputBufferAdapterFactory = () => StretchReadBufferAdapter;
/**
 * WSOLA timing parameters that control the time-stretching algorithm.
 *
 * @remarks
 * All time-based fields are in milliseconds. Pass `0` for `sequenceMs` or
 * `seekWindowMs` to let the algorithm auto-compute values based on tempo.
 * Omit a field to leave it unchanged.
 *
 * @example
 * stretch.setStretchParameters({ overlapMs: 12, quickSeek: false });
 */
export interface StretchParameters {
    /**
     * Length of the processing sequence window in milliseconds.
     * `0` switches to automatic calculation (50–125 ms range based on tempo).
     */
    sequenceMs?: number;
    /**
     * Length of the seek window in milliseconds.
     * `0` switches to automatic calculation (15–25 ms range based on tempo).
     */
    seekWindowMs?: number;
    /**
     * Overlap crossfade length in milliseconds.
     * Must be greater than `0`. Values less than `1 ms` (after sample-rate conversion) are clamped to 16 samples.
     */
    overlapMs?: number;
    /**
     * Whether to use the fast multi-pass seek algorithm.
     * `true` (default) uses a quick scan; `false` performs an exhaustive search for better quality at lower tempos.
     */
    quickSeek?: boolean;
}
export interface StretchConstructorOptions {
    /** Initial processing sample rate in Hz.
     *
     * @defaultValue 44100
     */
    sampleRate?: number;
    /** Whether to allocate internal input/output buffers. */
    createBuffers?: boolean;
    /** Factory for creating stretch input adapters. */
    inputBufferAdapterFactory?: StretchInputBufferAdapterFactory;
    /** Factory for creating chain input/output buffers. */
    sampleBufferFactory?: () => SampleBuffer;
}
/**
 * Creates a stretch input adapter that reads from FIFO-compatible buffers.
 */
export declare const createFifoStretchInputBufferAdapter: StretchInputBufferAdapterFactory;
/**
 * Creates a stretch input adapter backed by `CircularSampleBuffer`.
 */
export declare const createCircularStretchInputBufferAdapter: StretchInputBufferAdapterFactory;
/**
 * Time-stretch processor for tempo adjustment without affecting pitch.
 * Used internally by SoundTouch for time-stretching audio.
 */
export default class Stretch extends AbstractSamplePipe<SampleBuffer, SampleBuffer> implements StretchPipe {
    private readonly inputBufferAdapterFactory;
    private readonly sampleBufferFactory;
    private readonly inputBufferAdapter;
    private readonly outputBufferAdapter;
    private overlapScratch;
    private _quickSeek;
    private midBufferDirty;
    midBuffer: Float32Array | null;
    private refMidBuffer;
    private refMidBufferEnergy;
    overlapLength: number;
    private autoSeqSetting;
    private autoSeekSetting;
    _tempo: number;
    private sampleRate;
    private _overlapMs;
    sequenceMs: number;
    seekWindowMs: number;
    private seekWindowLength;
    private seekLength;
    private nominalSkip;
    private skipFract;
    sampleReq: number;
    /**
     * Creates a Stretch instance.
     * @param options Constructor options.
     */
    constructor({ sampleRate, createBuffers, inputBufferAdapterFactory, sampleBufferFactory, }?: StretchConstructorOptions);
    clear(): void;
    clearMidBuffer(): void;
    setParameters(sampleRate: number, sequenceMs: number, seekWindowMs: number, overlapMs: number): void;
    set tempo(newTempo: number);
    get tempo(): number;
    get inputChunkSize(): number;
    get outputChunkSize(): number;
    calculateOverlapLength(overlapInMsec?: number): void;
    private checkLimits;
    private calculateSequenceParameters;
    private normalizeWindowInvariants;
    private updateTempoDerivedState;
    /**
     * Whether the fast multi-pass seek algorithm is active.
     * @returns `true` if quick seek is enabled (default); `false` for exhaustive search.
     */
    get quickSeek(): boolean;
    set quickSeek(enable: boolean);
    /**
     * Current overlap crossfade length in milliseconds.
     * @returns The overlap period used at the current sample rate.
     */
    get overlapMs(): number;
    /**
     * Sets the overlap crossfade length and recalculates derived parameters.
     * @param ms Overlap period in milliseconds (must be > 0).
     */
    set overlapMs(ms: number);
    /**
     * Applies a partial set of WSOLA timing parameters.
     *
     * @remarks
     * Only the provided fields are updated; omitted fields remain unchanged.
     * Pass `sequenceMs: 0` or `seekWindowMs: 0` to switch that dimension back to auto-calculation.
     *
     * @param params Partial set of WSOLA timing parameters to apply.
     *
     * @example
     * stretch.setStretchParameters({ overlapMs: 12, quickSeek: false });
     */
    setStretchParameters(params: StretchParameters): void;
    clone(): Stretch;
    seekBestOverlapPosition(inputBuffer?: StretchReadBufferAdapter): number;
    private seekBestOverlapPositionStereo;
    private seekBestOverlapPositionStereoQuick;
    private getQuickScanOffsets;
    private generateFractionalScanOffsets;
    private generateSymmetricScanOffsets;
    private preCalculateCorrelationReferenceStereo;
    private calculateCrossCorrelationStereo;
    private overlapStereo;
    process(): void;
    private bootstrapMidBuffer;
    private processOneWindow;
    private captureOverlapHistory;
    private advanceInputByNominalSkip;
    private getInputBufferAdapter;
    private getOutputBufferAdapter;
}
export {};
//# sourceMappingURL=Stretch.d.ts.map