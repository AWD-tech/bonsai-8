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
import RateTransposer from './RateTransposer.js';
import { createCircularStretchInputBufferAdapter, createFifoStretchInputBufferAdapter, default as Stretch, } from './Stretch.js';
import CircularSampleBuffer from './CircularSampleBuffer.js';
import FifoSampleBuffer from './FifoSampleBuffer.js';
import { createCircularSampleBufferAdapter, createFifoSampleBufferAdapter, } from './SampleBufferAdapter.js';
import isFloatDifferent from './testFloatEqual.js';
/**
 * Main processing engine for pitch shifting and time-stretching.
 *
 * @remarks
 * Chains a `RateTransposer` and `Stretch` stage to deliver real-time pitch manipulation
 * without affecting playback tempo. Set `pitch`, `pitchOctaves`, or `pitchSemitones` to
 * control the output. The internal `_rate` and `_tempo` pipeline values are derived
 * automatically from `virtualPitch`.
 */
export default class SoundTouch {
    transposer;
    stretch;
    _sampleRate;
    _sampleBufferType;
    _sampleBufferFactory;
    _interpolationStrategy;
    _inputBuffer;
    _intermediateBuffer;
    _outputBuffer;
    _rate;
    _tempo;
    /** Current pitch multiplier. Updated by the `pitch`, `pitchOctaves`, and `pitchSemitones` setters. */
    virtualPitch;
    /**
     * Creates a new SoundTouch processor instance.
     * @param options Construction options for sample rate, buffer strategy, and factories.
     */
    constructor(options = {}) {
        this._sampleBufferType = options.sampleBufferType ?? 'circular';
        this._sampleBufferFactory =
            options.sampleBufferFactory ??
                (this._sampleBufferType === 'fifo'
                    ? () => new FifoSampleBuffer()
                    : () => new CircularSampleBuffer());
        this.transposer = new RateTransposer({
            createBuffers: false,
            sampleBufferAdapterFactory: this._sampleBufferType === 'circular'
                ? createCircularSampleBufferAdapter
                : createFifoSampleBufferAdapter,
            sampleBufferFactory: this._sampleBufferFactory,
            interpolationStrategy: options.interpolationStrategy,
        });
        this._interpolationStrategy = this.transposer.strategy;
        this._sampleRate = options.sampleRate ?? 44100;
        if (options.stretchFactory) {
            this.stretch = options.stretchFactory(this._sampleRate, {
                sampleBufferFactory: this._sampleBufferFactory,
                sampleBufferType: this._sampleBufferType,
            });
        }
        else {
            this.stretch = new Stretch({
                sampleRate: this._sampleRate,
                createBuffers: false,
                inputBufferAdapterFactory: this._sampleBufferType === 'circular'
                    ? createCircularStretchInputBufferAdapter
                    : createFifoStretchInputBufferAdapter,
                sampleBufferFactory: this._sampleBufferFactory,
            });
        }
        this.stretch.setParameters(this._sampleRate, 0, 0, 0);
        this._inputBuffer = this._sampleBufferFactory();
        this._intermediateBuffer = this._sampleBufferFactory();
        this._outputBuffer = this._sampleBufferFactory();
        this._rate = 0;
        this._tempo = 0;
        this.virtualPitch = 1.0;
        this.calculateEffectiveRateAndTempo();
    }
    /**
     * Clears both processing stages and their internal buffers.
     * @remarks
     * Resets the state of the transposer and stretch stages, including all internal buffers.
     */
    clear() {
        this.transposer.clear();
        this.stretch.clear();
    }
    /**
     * Creates an independent copy with equivalent runtime configuration.
     */
    clone() {
        const result = new SoundTouch({
            sampleRate: this._sampleRate,
            sampleBufferType: this._sampleBufferType,
            sampleBufferFactory: this._sampleBufferFactory,
            interpolationStrategy: {
                id: this._interpolationStrategy,
                params: this.transposer.strategyParams,
            },
        });
        result.pitch = this.virtualPitch;
        return result;
    }
    /**
     * Active interpolation strategy id used by the transposer stage.
     * @returns The current interpolation strategy identifier.
     */
    get interpolationStrategy() {
        return this._interpolationStrategy;
    }
    /**
     * Active interpolation strategy params used by the transposer stage.
     * @returns The current interpolation strategy parameters.
     */
    get interpolationStrategyParams() {
        return this.transposer.strategyParams;
    }
    /**
     * Switches interpolation strategy at runtime.
     * @param strategy The new interpolation strategy to use.
     */
    setInterpolationStrategy(strategy) {
        this.transposer.setInterpolationStrategy(strategy);
        this._interpolationStrategy = this.transposer.strategy;
    }
    /**
     * Applies a partial runtime params update to the current strategy.
     * @param params Partial set of parameters to update.
     */
    setInterpolationStrategyParams(params) {
        this.transposer.setInterpolationStrategyParams(params);
    }
    /**
     * Applies a partial set of WSOLA timing parameters to the stretch stage.
     *
     * @remarks
     * Delegates directly to {@link Stretch.setStretchParameters}. Only the provided
     * fields are updated; omitted fields remain unchanged. Pass `sequenceMs: 0` or
     * `seekWindowMs: 0` to switch that dimension back to auto-calculation.
     *
     * @param params Partial set of WSOLA timing parameters to apply.
     *
     * @example
     * st.setStretchParameters({ overlapMs: 12, quickSeek: false });
     */
    setStretchParameters(params) {
        this.stretch.setStretchParameters(params);
    }
    /**
     * Sets the pitch multiplier and recomputes the derived pipeline rate and tempo.
     *
     * @remarks
     * Internally sets `_rate = pitch` and `_tempo = 1 / pitch`, rewiring the
     * Transposer→Stretch stage order when pitch > 1.
     */
    set pitch(pitch) {
        this.virtualPitch = pitch;
        this.calculateEffectiveRateAndTempo();
    }
    /**
     * Sets pitch by octave offset.
     */
    set pitchOctaves(pitchOctaves) {
        this.pitch = Math.exp(0.69314718056 * pitchOctaves);
        this.calculateEffectiveRateAndTempo();
    }
    /**
     * Sets pitch by semitone offset.
     */
    set pitchSemitones(pitchSemitones) {
        this.pitchOctaves = pitchSemitones / 12.0;
    }
    /**
     * Input buffer for upstream interleaved stereo frames.
     * @returns The input buffer for writing audio frames.
     */
    get inputBuffer() {
        return this._inputBuffer;
    }
    /**
     * Output buffer that downstream consumers read from.
     * @returns The output buffer for reading processed audio frames.
     */
    get outputBuffer() {
        return this._outputBuffer;
    }
    /**
     * Recomputes the effective pipeline rate/tempo from `virtualPitch` and rewires stage order when needed.
     *
     * @remarks
     * `_rate` is set to `virtualPitch`; `_tempo` to `1 / virtualPitch`. When `_rate > 1` the
     * Stretch stage feeds the Transposer; otherwise the order is reversed.
     */
    calculateEffectiveRateAndTempo() {
        const previousTempo = this._tempo;
        const previousRate = this._rate;
        this._tempo = 1.0 / this.virtualPitch;
        this._rate = this.virtualPitch;
        if (isFloatDifferent(this._tempo, previousTempo)) {
            this.stretch.tempo = this._tempo;
        }
        if (isFloatDifferent(this._rate, previousRate)) {
            this.transposer.rate = this._rate;
        }
        if (this._rate > 1.0) {
            if (this._outputBuffer !== this.transposer.outputBuffer) {
                this.stretch.inputBuffer = this._inputBuffer;
                this.stretch.outputBuffer = this._intermediateBuffer;
                this.transposer.inputBuffer = this._intermediateBuffer;
                this.transposer.outputBuffer = this._outputBuffer;
            }
        }
        else {
            if (this._outputBuffer !== this.stretch.outputBuffer) {
                this.transposer.inputBuffer = this._inputBuffer;
                this.transposer.outputBuffer = this._intermediateBuffer;
                this.stretch.inputBuffer = this._intermediateBuffer;
                this.stretch.outputBuffer = this._outputBuffer;
            }
        }
    }
    /**
     * Runs one processing step through the currently selected stage order.
     * @remarks
     * Processes available frames through the pipeline, updating output buffers.
     */
    process() {
        if (this._rate > 1.0) {
            this.stretch.process();
            this.transposer.process();
        }
        else {
            this.transposer.process();
            this.stretch.process();
        }
    }
}
//# sourceMappingURL=SoundTouch.js.map