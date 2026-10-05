export { default as AbstractSamplePipe } from './AbstractSamplePipe.js';
export { default as CircularSampleBuffer } from './CircularSampleBuffer.js';
export { default as FifoSampleBuffer } from './FifoSampleBuffer.js';
export { default as RateTransposer } from './RateTransposer.js';
export { default as Stretch } from './Stretch.js';
export { default as SoundTouch } from './SoundTouch.js';
export type { StretchConstructorOptions, StretchParameters, } from './Stretch.js';
import { getActiveInterpolationStrategyId, hasInterpolationStrategy, resolveInterpolationStrategyRuntime, listInterpolationStrategies, normalizeInterpolationStrategyId, registerInterpolationStrategy, resolveInterpolationStrategy, setActiveInterpolationStrategy, unregisterInterpolationStrategy } from './interpolationStrategyRegistry.js';
export { getActiveInterpolationStrategyId, hasInterpolationStrategy, resolveInterpolationStrategyRuntime, listInterpolationStrategies, normalizeInterpolationStrategyId, registerInterpolationStrategy, resolveInterpolationStrategy, setActiveInterpolationStrategy, unregisterInterpolationStrategy, };
export type { SampleBuffer, SampleBufferFactory, SampleBufferType, } from './SampleBuffer.js';
export type { RateTransposerInterpolationStrategy } from './RateTransposer.js';
export type { BuiltInInterpolationStrategy, InterpolationStrategyParams, InterpolationStrategyRegistration, ResolvedInterpolationStrategyRuntime, RateTransposerInterpolationStrategyDescriptor, RateTransposerInterpolationStrategyId, RateTransposerInterpolationStrategyOption, } from './interpolationStrategyRegistry.js';
export type { SoundTouchOptions } from './SoundTouch.js';
export type { StretchFactory, StretchFactoryOptions, StretchPipe, } from './StretchPipe.js';
//# sourceMappingURL=index.d.ts.map