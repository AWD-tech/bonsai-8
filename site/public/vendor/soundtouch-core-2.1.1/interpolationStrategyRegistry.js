import { lanczosStrategy } from '../soundtouch-lanczos-2.1.1/index.js';
const strategyRegistry = new Map();
let activeStrategyId = 'lanczos';
function readStrategySelection(strategy) {
    if (typeof strategy === 'string') {
        return { id: strategy };
    }
    if (strategy !== undefined) {
        return strategy;
    }
    return { id: activeStrategyId };
}
function readStrategyId(strategy) {
    return readStrategySelection(strategy).id;
}
function requireRegisteredStrategy(strategyId) {
    const registered = strategyRegistry.get(strategyId);
    if (registered !== undefined) {
        return registered;
    }
    throw new Error(`Unknown interpolation strategy id "${strategyId}". Register it before use.`);
}
export function registerInterpolationStrategy(registration) {
    const baseStrategy = registration.baseStrategy ?? 'lanczos';
    strategyRegistry.set(registration.id, {
        id: registration.id,
        baseStrategy,
        builtIn: false,
        kernel: registration.kernel,
        defaultParams: { ...(registration.defaultParams ?? {}) },
        normalizeParams: registration.normalizeParams,
        applyParams: registration.applyParams,
    });
}
function registerBuiltInInterpolationStrategy(registration) {
    const baseStrategy = registration.baseStrategy ?? 'lanczos';
    strategyRegistry.set(registration.id, {
        id: registration.id,
        baseStrategy,
        builtIn: true,
        kernel: registration.kernel,
        defaultParams: { ...(registration.defaultParams ?? {}) },
        normalizeParams: registration.normalizeParams,
        applyParams: registration.applyParams,
    });
}
function resolveKernelRegistration(registration, visited = new Set()) {
    if (registration.kernel !== undefined) {
        return registration;
    }
    if (visited.has(registration.id)) {
        throw new Error(`Interpolation strategy resolution cycle detected at "${registration.id}".`);
    }
    visited.add(registration.id);
    const next = requireRegisteredStrategy(registration.baseStrategy);
    return resolveKernelRegistration(next, visited);
}
function normalizeParams(registration, params) {
    const defaults = registration.defaultParams;
    if (registration.normalizeParams !== undefined) {
        return registration.normalizeParams(params, defaults);
    }
    const normalized = { ...defaults };
    if (params !== undefined) {
        for (const [key, value] of Object.entries(params)) {
            if (value !== undefined) {
                normalized[key] = value;
            }
        }
    }
    return normalized;
}
export function unregisterInterpolationStrategy(strategyId) {
    const existing = strategyRegistry.get(strategyId);
    if (existing === undefined || existing.builtIn) {
        return false;
    }
    const deleted = strategyRegistry.delete(strategyId);
    if (deleted && activeStrategyId === strategyId) {
        activeStrategyId = 'lanczos';
    }
    return deleted;
}
/** Returns true when a strategy id is currently registered. */
export function hasInterpolationStrategy(strategyId) {
    return strategyRegistry.has(strategyId);
}
/** Returns all registered strategy ids sorted lexicographically. */
export function listInterpolationStrategies() {
    return Array.from(strategyRegistry.keys()).sort();
}
/** Returns the process-wide active strategy id used as implicit default. */
export function getActiveInterpolationStrategyId() {
    return activeStrategyId;
}
/** Sets the process-wide active strategy id. */
export function setActiveInterpolationStrategy(strategy) {
    const strategyId = readStrategyId(strategy);
    requireRegisteredStrategy(strategyId);
    activeStrategyId = strategyId;
}
/**
 * Resolves a user-provided option to a validated strategy id.
 *
 * @throws Error when the strategy id is unknown.
 */
export function normalizeInterpolationStrategyId(strategy) {
    const strategyId = readStrategyId(strategy);
    requireRegisteredStrategy(strategyId);
    return strategyId;
}
/**
 * Resolves a strategy to either a built-in base id or a plugin kernel.
 *
 * @throws Error when the strategy id is unknown.
 */
export function resolveInterpolationStrategy(strategy) {
    const strategyId = readStrategyId(strategy);
    const registered = requireRegisteredStrategy(strategyId);
    if ('kernel' in registered && registered.kernel) {
        return registered.kernel;
    }
    return registered.baseStrategy;
}
/**
 * Resolves runtime strategy state (kernel + normalized params + applier hook).
 */
export function resolveInterpolationStrategyRuntime(strategy) {
    const selection = readStrategySelection(strategy);
    const registered = requireRegisteredStrategy(selection.id);
    const kernelRegistration = resolveKernelRegistration(registered);
    const kernel = kernelRegistration.kernel;
    if (kernel === undefined) {
        throw new Error(`Interpolation strategy "${selection.id}" did not resolve to a kernel.`);
    }
    const params = normalizeParams(registered, selection.params);
    return {
        id: registered.id,
        kernel,
        params,
        applyParams: registered.applyParams ?? kernelRegistration.applyParams,
    };
}
registerBuiltInInterpolationStrategy({
    ...lanczosStrategy,
    baseStrategy: 'lanczos',
});
//# sourceMappingURL=interpolationStrategyRegistry.js.map