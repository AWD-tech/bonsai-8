/** Built-in interpolation strategy ids understood by the core pipeline. */
export type BuiltInInterpolationStrategy = 'linear' | 'lanczos';
/**
 * Kernel contract for interpolation plugins.
 *
 * @remarks
 * Kernels receive interleaved stereo input and may read from `state` for
 * continuity across processing blocks.
 */
export interface InterpolationKernel {
    (src: Float32Array, srcOffset: number, numFrames: number, position: number, channel: 0 | 1, state: unknown): number;
    /** Optional factory for per-instance mutable kernel state. */
    createState?: () => unknown;
}
export type InterpolationStrategyParams = Record<string, number | boolean>;
export type RateTransposerInterpolationStrategyId = string;
export interface RateTransposerInterpolationStrategyDescriptor {
    readonly id: RateTransposerInterpolationStrategyId;
    readonly params?: Partial<InterpolationStrategyParams>;
}
export type RateTransposerInterpolationStrategyOption = RateTransposerInterpolationStrategyId | RateTransposerInterpolationStrategyDescriptor;
export interface InterpolationStrategyRegistration {
    /** Unique strategy id used by `SoundTouch` and `RateTransposer` options. */
    readonly id: RateTransposerInterpolationStrategyId;
    /** Base strategy id used when no custom kernel is provided. */
    readonly baseStrategy?: BuiltInInterpolationStrategy;
    /** Optional plugin kernel implementation. */
    readonly kernel?: InterpolationKernel;
    /** Default params merged with runtime overrides. */
    readonly defaultParams?: InterpolationStrategyParams;
    /** Optional params normalizer/validator. */
    readonly normalizeParams?: (params: Partial<InterpolationStrategyParams> | undefined, defaults: InterpolationStrategyParams) => InterpolationStrategyParams;
    /** Optional hook used to apply params to kernel state. */
    readonly applyParams?: (state: unknown, params: InterpolationStrategyParams) => void;
}
export interface ResolvedInterpolationStrategyRuntime {
    readonly id: RateTransposerInterpolationStrategyId;
    readonly kernel: InterpolationKernel;
    readonly params: InterpolationStrategyParams;
    readonly applyParams?: (state: unknown, params: InterpolationStrategyParams) => void;
}
export declare function registerInterpolationStrategy(registration: InterpolationStrategyRegistration): void;
export declare function unregisterInterpolationStrategy(strategyId: RateTransposerInterpolationStrategyId): boolean;
/** Returns true when a strategy id is currently registered. */
export declare function hasInterpolationStrategy(strategyId: RateTransposerInterpolationStrategyId): boolean;
/** Returns all registered strategy ids sorted lexicographically. */
export declare function listInterpolationStrategies(): readonly RateTransposerInterpolationStrategyId[];
/** Returns the process-wide active strategy id used as implicit default. */
export declare function getActiveInterpolationStrategyId(): RateTransposerInterpolationStrategyId;
/** Sets the process-wide active strategy id. */
export declare function setActiveInterpolationStrategy(strategy: RateTransposerInterpolationStrategyOption): void;
/**
 * Resolves a user-provided option to a validated strategy id.
 *
 * @throws Error when the strategy id is unknown.
 */
export declare function normalizeInterpolationStrategyId(strategy?: RateTransposerInterpolationStrategyOption): RateTransposerInterpolationStrategyId;
/**
 * Resolves a strategy to either a built-in base id or a plugin kernel.
 *
 * @throws Error when the strategy id is unknown.
 */
export declare function resolveInterpolationStrategy(strategy?: RateTransposerInterpolationStrategyOption): BuiltInInterpolationStrategy | InterpolationKernel;
/**
 * Resolves runtime strategy state (kernel + normalized params + applier hook).
 */
export declare function resolveInterpolationStrategyRuntime(strategy?: RateTransposerInterpolationStrategyOption): ResolvedInterpolationStrategyRuntime;
//# sourceMappingURL=interpolationStrategyRegistry.d.ts.map