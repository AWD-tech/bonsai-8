export type BuiltInInterpolationStrategy = 'linear' | 'lanczos';
/** Kernel contract compatible with the core interpolation registry. */
export interface InterpolationKernel {
    (src: Float32Array, srcOffset: number, numFrames: number, position: number, channel: 0 | 1, state: unknown): number;
    createState?: () => unknown;
}
export interface InterpolationStrategyRegistration {
    /** Unique strategy id used by consumers. */
    readonly id: string;
    /** Base strategy used when kernel chaining is desired. */
    readonly baseStrategy?: BuiltInInterpolationStrategy;
    /** Kernel implementation for interpolation. */
    readonly kernel?: InterpolationKernel;
    /** Default params merged with runtime overrides. */
    readonly defaultParams?: Record<string, number | boolean>;
    /** Optional params normalizer/validator. */
    readonly normalizeParams?: (params: Partial<Record<string, number | boolean>> | undefined, defaults: Record<string, number | boolean>) => Record<string, number | boolean>;
    /** Optional hook used to apply params to kernel state. */
    readonly applyParams?: (state: unknown, params: Record<string, number | boolean>) => void;
}
export interface InterpolationStrategyRegistrar {
    registerInterpolationStrategy: (registration: InterpolationStrategyRegistration) => void;
}
/**
 * Parameters for the Lanczos interpolation strategy.
 *
 * @property zeroCrossings Kernel half-width in zero-crossings (2–8, default: 4)
 * @property normalize If true, output is normalized so weights sum to 1 (default: false)
 */
export interface LanczosStrategyParams extends Record<string, number | boolean> {
    zeroCrossings: number;
    normalize: boolean;
}
export declare const lanczosKernel: InterpolationKernel;
/** Default Lanczos strategy registration payload. */
export declare const lanczosStrategy: InterpolationStrategyRegistration;
/** Registers the Lanczos strategy in a compatible registry. */
export declare function registerLanczosStrategy(registry: InterpolationStrategyRegistrar): void;
//# sourceMappingURL=index.d.ts.map