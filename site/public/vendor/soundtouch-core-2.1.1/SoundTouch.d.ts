import RateTransposer from './RateTransposer.js';
import type { RateTransposerInterpolationStrategy } from './RateTransposer.js';
import type { InterpolationStrategyParams, RateTransposerInterpolationStrategyId } from './interpolationStrategyRegistry.js';
import type { StretchParameters } from './Stretch.js';
import type { StretchFactory, StretchPipe } from './StretchPipe.js';
import type { SampleBuffer, SampleBufferFactory, SampleBufferType } from './SampleBuffer.js';
/**
 * Configuration options for constructing a `SoundTouch` processor.
 *
 * @remarks
 * Allows customization of sample rate, buffer strategy, and interpolation strategy for the SoundTouch engine.
 */
export interface SoundTouchOptions {
    /** Processing sample rate in Hz.
     *
     * @defaultValue 44100
     */
    sampleRate?: number;
    /**
     * Internal sample buffer strategy.
     *
     * @defaultValue 'circular'
     */
    sampleBufferType?: SampleBufferType;
    /**
     * Custom factory for creating all chain buffers.
     *
     * @remarks
     * When provided, this takes precedence over `sampleBufferType` for buffer
     * instantiation.
     */
    sampleBufferFactory?: SampleBufferFactory;
    /**
     * Interpolation strategy used by the rate transposer stage.
     *
     * @defaultValue 'linear'
     */
    interpolationStrategy?: RateTransposerInterpolationStrategy;
    /**
     * Optional factory for creating a custom time-stretch stage.
     *
     * @remarks
     * When provided, `SoundTouch` calls this function instead of constructing
     * the default WSOLA `Stretch` instance. Use this to substitute a phase
     * vocoder or any other `StretchPipe`-compatible implementation.
     *
     * @example
     * import { createPhaseVocoderFactory } from '@soundtouchjs/stretch-phase-vocoder';
     * const st = new SoundTouch({ stretchFactory: createPhaseVocoderFactory() });
     */
    stretchFactory?: StretchFactory;
}
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
    transposer: RateTransposer;
    stretch: StretchPipe;
    private _sampleRate;
    private _sampleBufferType;
    private _sampleBufferFactory;
    private _interpolationStrategy;
    private _inputBuffer;
    private _intermediateBuffer;
    private _outputBuffer;
    private _rate;
    private _tempo;
    /** Current pitch multiplier. Updated by the `pitch`, `pitchOctaves`, and `pitchSemitones` setters. */
    virtualPitch: number;
    /**
     * Creates a new SoundTouch processor instance.
     * @param options Construction options for sample rate, buffer strategy, and factories.
     */
    constructor(options?: SoundTouchOptions);
    /**
     * Clears both processing stages and their internal buffers.
     * @remarks
     * Resets the state of the transposer and stretch stages, including all internal buffers.
     */
    clear(): void;
    /**
     * Creates an independent copy with equivalent runtime configuration.
     */
    clone(): SoundTouch;
    /**
     * Active interpolation strategy id used by the transposer stage.
     * @returns The current interpolation strategy identifier.
     */
    get interpolationStrategy(): RateTransposerInterpolationStrategyId;
    /**
     * Active interpolation strategy params used by the transposer stage.
     * @returns The current interpolation strategy parameters.
     */
    get interpolationStrategyParams(): Readonly<InterpolationStrategyParams>;
    /**
     * Switches interpolation strategy at runtime.
     * @param strategy The new interpolation strategy to use.
     */
    setInterpolationStrategy(strategy: RateTransposerInterpolationStrategy): void;
    /**
     * Applies a partial runtime params update to the current strategy.
     * @param params Partial set of parameters to update.
     */
    setInterpolationStrategyParams(params: Partial<InterpolationStrategyParams>): void;
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
    setStretchParameters(params: StretchParameters): void;
    /**
     * Sets the pitch multiplier and recomputes the derived pipeline rate and tempo.
     *
     * @remarks
     * Internally sets `_rate = pitch` and `_tempo = 1 / pitch`, rewiring the
     * Transposer→Stretch stage order when pitch > 1.
     */
    set pitch(pitch: number);
    /**
     * Sets pitch by octave offset.
     */
    set pitchOctaves(pitchOctaves: number);
    /**
     * Sets pitch by semitone offset.
     */
    set pitchSemitones(pitchSemitones: number);
    /**
     * Input buffer for upstream interleaved stereo frames.
     * @returns The input buffer for writing audio frames.
     */
    get inputBuffer(): SampleBuffer;
    /**
     * Output buffer that downstream consumers read from.
     * @returns The output buffer for reading processed audio frames.
     */
    get outputBuffer(): SampleBuffer;
    /**
     * Recomputes the effective pipeline rate/tempo from `virtualPitch` and rewires stage order when needed.
     *
     * @remarks
     * `_rate` is set to `virtualPitch`; `_tempo` to `1 / virtualPitch`. When `_rate > 1` the
     * Stretch stage feeds the Transposer; otherwise the order is reversed.
     */
    calculateEffectiveRateAndTempo(): void;
    /**
     * Runs one processing step through the currently selected stage order.
     * @remarks
     * Processes available frames through the pipeline, updating output buffers.
     */
    process(): void;
}
//# sourceMappingURL=SoundTouch.d.ts.map