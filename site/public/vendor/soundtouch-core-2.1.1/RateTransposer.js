/*
 * SoundTouch JS audio processing library
 * Copyright (c) Olli Parviainen
 * Copyright (c) Ryan Berdeen
 * Copyright (c) Jakub Fiala
 * Copyright (c) Steve 'Cutter' Blades
 *
 * Licensed under the Mozilla Public License, v. 2.0.
 * You can obtain one at https://mozilla.org/MPL/2.0/.
 */
import AbstractSamplePipe from './AbstractSamplePipe.js';
import CircularSampleBuffer from './CircularSampleBuffer.js';
import { createCircularSampleBufferAdapter, } from './SampleBufferAdapter.js';
import { resolveInterpolationStrategyRuntime, } from './interpolationStrategyRegistry.js';
/**
 * Sample rate transposer for pitch and tempo manipulation.
 *
 * @remarks
 * Used internally by SoundTouch for rate-based processing. Applies interpolation strategies to resample audio at different rates, supporting real-time pitch and tempo changes.
 */
export default class RateTransposer extends AbstractSamplePipe {
    /**
     * Current rate factor for transposition.
     */
    _rate;
    /**
     * Source position (in frames) for the next output sample, relative to the
     * current processing block where 0 is the first frame and -1 is prevSample.
     */
    fractionalPosition;
    /**
     * Previous left channel sample for interpolation.
     */
    previousLeftSample;
    /**
     * Previous right channel sample for interpolation.
     */
    previousRightSample;
    /** Scratch space used for extracted input samples. */
    inputScratch;
    /** Scratch space used for generated output samples. */
    outputScratch;
    /** Factory used when cloning or initializing adapter strategy. */
    sampleBufferAdapterFactory;
    /** Factory used to construct input/output chain buffers. */
    sampleBufferFactory;
    /** Adapter that normalizes reads from the bound input buffer. */
    inputAdapter;
    /** Selected interpolation strategy for transposition. */
    interpolationStrategy;
    /** Resolved kernel used by this transposer instance. */
    resolvedInterpolationKernel;
    /** Optional per-instance state for plugin kernels. */
    kernelState;
    /** Normalized params for the selected interpolation strategy. */
    interpolationStrategyParams;
    /** Optional params application hook from the strategy registration. */
    applyKernelParams;
    /**
     * Creates a RateTransposer instance.
     * @param options Constructor options.
     * @remarks
     * Accepts factories for buffer and adapter creation, and allows specifying the interpolation strategy.
     */
    constructor({ createBuffers = false, sampleBufferAdapterFactory = createCircularSampleBufferAdapter, sampleBufferFactory = () => new CircularSampleBuffer(), interpolationStrategy, } = {}) {
        super({
            createBuffers,
            inputBufferFactory: sampleBufferFactory,
            outputBufferFactory: sampleBufferFactory,
        });
        this.fractionalPosition = -1;
        this.previousLeftSample = 0;
        this.previousRightSample = 0;
        this._rate = 1;
        this.inputScratch = new Float32Array(0);
        this.outputScratch = new Float32Array(0);
        this.sampleBufferAdapterFactory = sampleBufferAdapterFactory;
        this.sampleBufferFactory = sampleBufferFactory;
        this.inputAdapter = sampleBufferAdapterFactory();
        this.interpolationStrategy = 'lanczos';
        this.resolvedInterpolationKernel = () => 0;
        this.kernelState = undefined;
        this.interpolationStrategyParams = {};
        this.applyKernelParams = undefined;
        this.setInterpolationStrategy(interpolationStrategy ?? 'lanczos');
    }
    /**
     * Sets the rate factor for transposition.
     * @param rate Rate factor.
     */
    set rate(rate) {
        this._rate = rate;
    }
    /**
     * Active interpolation strategy.
     * @returns The current interpolation strategy identifier.
     */
    get strategy() {
        return this.interpolationStrategy;
    }
    /**
     * Active interpolation strategy params.
     * @returns The current interpolation strategy parameters.
     */
    get strategyParams() {
        return { ...this.interpolationStrategyParams };
    }
    /**
     * Switches interpolation strategy at runtime.
     * @param strategy The new interpolation strategy to use.
     */
    setInterpolationStrategy(strategy) {
        const resolved = resolveInterpolationStrategyRuntime(strategy);
        this.interpolationStrategy = resolved.id;
        this.resolvedInterpolationKernel = resolved.kernel;
        this.interpolationStrategyParams = { ...resolved.params };
        this.applyKernelParams = resolved.applyParams;
        if ('createState' in this.resolvedInterpolationKernel &&
            typeof this.resolvedInterpolationKernel.createState === 'function') {
            this.kernelState = this.resolvedInterpolationKernel.createState();
        }
        else {
            this.kernelState = undefined;
        }
        if (this.applyKernelParams !== undefined) {
            this.applyKernelParams(this.kernelState, this.interpolationStrategyParams);
        }
        this.reset();
    }
    /**
     * Applies a partial params update to the current interpolation strategy.
     * @param params Partial set of parameters to update.
     */
    setInterpolationStrategyParams(params) {
        const nextParams = {
            ...this.interpolationStrategyParams,
        };
        for (const [key, value] of Object.entries(params)) {
            if (value !== undefined) {
                nextParams[key] = value;
            }
        }
        this.interpolationStrategyParams = nextParams;
        if (this.applyKernelParams !== undefined) {
            this.applyKernelParams(this.kernelState, this.interpolationStrategyParams);
        }
    }
    /**
     * Resets internal state for interpolation.
     * @remarks
     * Clears previous sample values and resets the fractional position for output generation.
     */
    reset() {
        this.fractionalPosition = -1;
        this.previousLeftSample = 0;
        this.previousRightSample = 0;
    }
    /**
     * Clears buffers and resets internal state.
     * @remarks
     * Calls clear on all internal buffers and resets interpolation state.
     */
    clear() {
        super.clear();
        this.inputAdapter.clear();
        this.reset();
    }
    /**
     * Creates a clone of this RateTransposer with the same rate.
     * @returns Cloned RateTransposer instance.
     */
    clone() {
        const result = new RateTransposer({
            createBuffers: false,
            sampleBufferAdapterFactory: this.sampleBufferAdapterFactory,
            sampleBufferFactory: this.sampleBufferFactory,
            interpolationStrategy: {
                id: this.interpolationStrategy,
                params: this.interpolationStrategyParams,
            },
        });
        result.rate = this._rate;
        return result;
    }
    /**
     * Processes input buffer and writes transposed samples to output buffer.
     * @remarks
     * Reads frames from the input buffer, applies rate transposition, and writes to the output buffer.
     */
    process() {
        if (this._inputBuffer === null || this._outputBuffer === null) {
            return;
        }
        this.inputAdapter.syncFromInputBuffer(this._inputBuffer);
        const numFrames = this.inputAdapter.frameCount;
        if (numFrames === 0) {
            return;
        }
        const numFramesOutput = this.transpose(numFrames);
        this.inputAdapter.receive(numFrames);
        if (numFramesOutput > 0) {
            this._outputBuffer.putSamples(this.outputScratch, 0, numFramesOutput);
        }
    }
    /**
     * Ensures temporary scratch arrays are large enough for the current frame request and estimated output size.
     *
     * @param numInputFrames Number of input frames that will be processed.
     * @remarks
     * Allocates or resizes scratch arrays as needed for efficient processing.
     */
    ensureScratchCapacity(numInputFrames) {
        const inputSamples = numInputFrames * 2;
        if (this.inputScratch.length < inputSamples) {
            this.inputScratch = new Float32Array(inputSamples);
        }
        const estimatedOutputFrames = Math.ceil(numInputFrames / this._rate) + 2;
        const outputSamples = Math.max(0, estimatedOutputFrames) * 2;
        if (this.outputScratch.length < outputSamples) {
            this.outputScratch = new Float32Array(outputSamples);
        }
    }
    /**
     * Transposes input samples by the current rate.
     * @param numFrames Number of input frames to transpose.
     * @returns Number of output frames written.
     * @remarks
     * Applies the selected interpolation kernel to generate output samples at the new rate.
     */
    transpose(numFrames = 0) {
        if (this._inputBuffer !== null) {
            this.inputAdapter.syncFromInputBuffer(this._inputBuffer);
            if (numFrames === 0) {
                numFrames = this.inputAdapter.frameCount;
            }
        }
        if (numFrames === 0) {
            return 0;
        }
        this.ensureScratchCapacity(numFrames);
        const src = this.inputScratch;
        const extractedFrames = this.inputAdapter.extract(src, 0, numFrames);
        if (extractedFrames === 0) {
            return 0;
        }
        numFrames = extractedFrames;
        return this.transposePluginKernel(numFrames);
    }
    /**
     * Handles transposition using a plugin kernel.
     * @remarks
     * Invokes the selected interpolation kernel for each output sample.
     */
    transposePluginKernel(numFrames) {
        const src = this.inputScratch;
        const dest = this.outputScratch;
        const srcOffset = 0;
        const destOffset = 0;
        const kernel = this.resolvedInterpolationKernel;
        const state = this.kernelState;
        const stateRecord = this.getKernelStateRecord(state);
        if (stateRecord !== undefined) {
            stateRecord.prevSampleL = this.previousLeftSample;
            stateRecord.prevSampleR = this.previousRightSample;
        }
        let i = 0;
        let position = this.fractionalPosition;
        const maxPosition = numFrames - 1;
        while (position <= maxPosition) {
            dest[destOffset + 2 * i] = kernel(src, srcOffset, numFrames, position, 0, state);
            dest[destOffset + 2 * i + 1] = kernel(src, srcOffset, numFrames, position, 1, state);
            i = i + 1;
            position += this._rate;
        }
        this.fractionalPosition = position - numFrames;
        this.previousLeftSample = src[srcOffset + 2 * numFrames - 2];
        this.previousRightSample = src[srcOffset + 2 * numFrames - 1];
        if (stateRecord !== undefined) {
            stateRecord.prevSampleL = this.previousLeftSample;
            stateRecord.prevSampleR = this.previousRightSample;
        }
        return i;
    }
    /**
     * Returns the kernel state record if available.
     * @param state The kernel state object.
     * @returns The state record with previous sample values, or undefined if not present.
     */
    getKernelStateRecord(state) {
        if (typeof state !== 'object' || state === null) {
            return undefined;
        }
        const record = state;
        const prevSampleL = record['prevSampleL'];
        const prevSampleR = record['prevSampleR'];
        if (typeof prevSampleL === 'number' && typeof prevSampleR === 'number') {
            return record;
        }
        return undefined;
    }
}
//# sourceMappingURL=RateTransposer.js.map