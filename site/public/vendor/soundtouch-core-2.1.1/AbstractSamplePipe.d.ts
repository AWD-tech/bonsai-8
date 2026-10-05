import type { SampleBuffer } from './SampleBuffer.js';
export interface AbstractSamplePipeOptions<TInputBuffer extends SampleBuffer, TOutputBuffer extends SampleBuffer> {
    /** If true, initializes input and output buffers. */
    createBuffers?: boolean;
    /** Factory used to create input buffer instances. */
    inputBufferFactory?: () => TInputBuffer;
    /** Factory used to create output buffer instances. */
    outputBufferFactory?: () => TOutputBuffer;
}
/**
 * Abstract base class for sample processing pipes.
 *
 * @remarks
 * Manages input and output buffers for audio sample processing chains. Subclasses should implement
 * specific processing logic for audio transformation or analysis. This class is not intended to be used
 * directly, but as a base for concrete audio processing stages.
 *
 * @typeParam TInputBuffer - Concrete input buffer type (defaults to the generic `SampleBuffer` contract).
 * @typeParam TOutputBuffer - Concrete output buffer type (defaults to `TInputBuffer` so input/output share the same buffer type unless a subclass opts into different types).
 */
export default class AbstractSamplePipe<TInputBuffer extends SampleBuffer = SampleBuffer, TOutputBuffer extends SampleBuffer = TInputBuffer> {
    /**
     * Input buffer for audio samples.
     */
    protected _inputBuffer: TInputBuffer | null;
    /**
     * Output buffer for processed audio samples.
     */
    protected _outputBuffer: TOutputBuffer | null;
    /**
     * Constructs an AbstractSamplePipe.
     * @param options Constructor options.
     *
     * @remarks
     * When `createBuffers` is true, both factories are required so subclasses can
     * control exact buffer implementations without unsafe casting.
     */
    constructor({ createBuffers, inputBufferFactory, outputBufferFactory, }?: AbstractSamplePipeOptions<TInputBuffer, TOutputBuffer>);
    /**
     * Gets the input buffer.
     * @returns The current input buffer instance, or null if not set.
     */
    get inputBuffer(): TInputBuffer | null;
    /**
     * Sets the input buffer.
     * @param inputBuffer The new input buffer instance, or null to unset.
     */
    set inputBuffer(inputBuffer: TInputBuffer | null);
    /**
     * Gets the output buffer.
     * @returns The current output buffer instance, or null if not set.
     */
    get outputBuffer(): TOutputBuffer | null;
    /**
     * Sets the output buffer.
     * @param outputBuffer The new output buffer instance, or null to unset.
     */
    set outputBuffer(outputBuffer: TOutputBuffer | null);
    /**
     * Clears both input and output buffers.
     *
     * @remarks
     * Resets the state of both input and output buffers, if present, by calling their `clear()` methods.
     */
    clear(): void;
}
//# sourceMappingURL=AbstractSamplePipe.d.ts.map