import AbstractSamplePipe from './AbstractSamplePipe.js';
import { type SampleBufferAdapterFactory } from './SampleBufferAdapter.js';
import { type InterpolationStrategyParams, type RateTransposerInterpolationStrategyId, type RateTransposerInterpolationStrategyOption } from './interpolationStrategyRegistry.js';
import type { SampleBuffer } from './SampleBuffer.js';
export type RateTransposerInterpolationStrategy = RateTransposerInterpolationStrategyOption;
export interface RateTransposerConstructorOptions {
    /** Whether to allocate internal buffers. */
    createBuffers?: boolean;
    /** Factory for creating adapters that normalize input reads. */
    sampleBufferAdapterFactory?: SampleBufferAdapterFactory;
    /** Factory for creating chain input/output buffers. */
    sampleBufferFactory?: () => SampleBuffer;
    /** Interpolation strategy used for resampling. */
    interpolationStrategy?: RateTransposerInterpolationStrategy;
}
/**
 * Sample rate transposer for pitch and tempo manipulation.
 *
 * @remarks
 * Used internally by SoundTouch for rate-based processing. Applies interpolation strategies to resample audio at different rates, supporting real-time pitch and tempo changes.
 */
export default class RateTransposer extends AbstractSamplePipe<SampleBuffer, SampleBuffer> {
    /**
     * Current rate factor for transposition.
     */
    private _rate;
    /**
     * Source position (in frames) for the next output sample, relative to the
     * current processing block where 0 is the first frame and -1 is prevSample.
     */
    private fractionalPosition;
    /**
     * Previous left channel sample for interpolation.
     */
    private previousLeftSample;
    /**
     * Previous right channel sample for interpolation.
     */
    private previousRightSample;
    /** Scratch space used for extracted input samples. */
    private inputScratch;
    /** Scratch space used for generated output samples. */
    private outputScratch;
    /** Factory used when cloning or initializing adapter strategy. */
    private readonly sampleBufferAdapterFactory;
    /** Factory used to construct input/output chain buffers. */
    private readonly sampleBufferFactory;
    /** Adapter that normalizes reads from the bound input buffer. */
    private readonly inputAdapter;
    /** Selected interpolation strategy for transposition. */
    private interpolationStrategy;
    /** Resolved kernel used by this transposer instance. */
    private resolvedInterpolationKernel;
    /** Optional per-instance state for plugin kernels. */
    private kernelState;
    /** Normalized params for the selected interpolation strategy. */
    private interpolationStrategyParams;
    /** Optional params application hook from the strategy registration. */
    private applyKernelParams;
    /**
     * Creates a RateTransposer instance.
     * @param options Constructor options.
     * @remarks
     * Accepts factories for buffer and adapter creation, and allows specifying the interpolation strategy.
     */
    constructor({ createBuffers, sampleBufferAdapterFactory, sampleBufferFactory, interpolationStrategy, }?: RateTransposerConstructorOptions);
    /**
     * Sets the rate factor for transposition.
     * @param rate Rate factor.
     */
    set rate(rate: number);
    /**
     * Active interpolation strategy.
     * @returns The current interpolation strategy identifier.
     */
    get strategy(): RateTransposerInterpolationStrategyId;
    /**
     * Active interpolation strategy params.
     * @returns The current interpolation strategy parameters.
     */
    get strategyParams(): Readonly<InterpolationStrategyParams>;
    /**
     * Switches interpolation strategy at runtime.
     * @param strategy The new interpolation strategy to use.
     */
    setInterpolationStrategy(strategy: RateTransposerInterpolationStrategy): void;
    /**
     * Applies a partial params update to the current interpolation strategy.
     * @param params Partial set of parameters to update.
     */
    setInterpolationStrategyParams(params: Partial<InterpolationStrategyParams>): void;
    /**
     * Resets internal state for interpolation.
     * @remarks
     * Clears previous sample values and resets the fractional position for output generation.
     */
    private reset;
    /**
     * Clears buffers and resets internal state.
     * @remarks
     * Calls clear on all internal buffers and resets interpolation state.
     */
    clear(): void;
    /**
     * Creates a clone of this RateTransposer with the same rate.
     * @returns Cloned RateTransposer instance.
     */
    clone(): RateTransposer;
    /**
     * Processes input buffer and writes transposed samples to output buffer.
     * @remarks
     * Reads frames from the input buffer, applies rate transposition, and writes to the output buffer.
     */
    process(): void;
    /**
     * Ensures temporary scratch arrays are large enough for the current frame request and estimated output size.
     *
     * @param numInputFrames Number of input frames that will be processed.
     * @remarks
     * Allocates or resizes scratch arrays as needed for efficient processing.
     */
    private ensureScratchCapacity;
    /**
     * Transposes input samples by the current rate.
     * @param numFrames Number of input frames to transpose.
     * @returns Number of output frames written.
     * @remarks
     * Applies the selected interpolation kernel to generate output samples at the new rate.
     */
    transpose(numFrames?: number): number;
    /**
     * Handles transposition using a plugin kernel.
     * @remarks
     * Invokes the selected interpolation kernel for each output sample.
     */
    private transposePluginKernel;
    /**
     * Returns the kernel state record if available.
     * @param state The kernel state object.
     * @returns The state record with previous sample values, or undefined if not present.
     */
    private getKernelStateRecord;
}
//# sourceMappingURL=RateTransposer.d.ts.map