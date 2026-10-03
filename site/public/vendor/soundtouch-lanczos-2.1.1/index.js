const LANCZOS_DEFAULT_PARAMS = {
    zeroCrossings: 4,
    normalize: false,
};
function normalizeLanczosParams(params, defaults) {
    const merged = {
        ...defaults,
        ...(params ?? {}),
    };
    const zeroCrossings = Math.max(2, Math.min(8, Math.round(Number(merged['zeroCrossings'] ?? defaults['zeroCrossings'] ?? 4))));
    const normalize = Boolean(merged['normalize']);
    return { zeroCrossings, normalize };
}
function applyLanczosParams(state, params) {
    if (typeof state !== 'object' || state === null) {
        return;
    }
    const record = state;
    record.params = {
        zeroCrossings: Math.max(2, Math.round(Number(params['zeroCrossings'] ?? 4))),
        normalize: Boolean(params['normalize']),
    };
}
function readFrameSample(src, srcOffset, numFrames, frameIndex, channel, state) {
    if (frameIndex < 0) {
        return channel === 0 ? state.prevSampleL : state.prevSampleR;
    }
    if (frameIndex >= numFrames) {
        const edgeIndex = srcOffset + 2 * (numFrames - 1) + channel;
        return src[edgeIndex];
    }
    return src[srcOffset + 2 * frameIndex + channel];
}
function normalizedSinc(x) {
    if (x === 0) {
        return 1;
    }
    const value = Math.PI * x;
    return Math.sin(value) / value;
}
function lanczosWeight(distance, radius) {
    const absDistance = Math.abs(distance);
    if (absDistance >= radius) {
        return 0;
    }
    return normalizedSinc(distance) * normalizedSinc(distance / radius);
}
export const lanczosKernel = (src, srcOffset, numFrames, position, channel, state) => {
    const kernelState = state;
    const radius = kernelState.params.zeroCrossings;
    const normalize = Boolean(kernelState.params.normalize);
    const center = Math.floor(position);
    const start = center - (radius - 1);
    const end = center + radius;
    let numerator = 0;
    let denominator = 0;
    for (let sampleIndex = start; sampleIndex <= end; sampleIndex += 1) {
        const distance = position - sampleIndex;
        const weight = lanczosWeight(distance, radius);
        numerator +=
            readFrameSample(src, srcOffset, numFrames, sampleIndex, channel, kernelState) * weight;
        denominator += weight;
    }
    if (Math.abs(denominator) < 1e-12) {
        return readFrameSample(src, srcOffset, numFrames, Math.round(position), channel, kernelState);
    }
    return normalize ? numerator / denominator : numerator / (denominator || 1);
};
lanczosKernel.createState = () => ({
    prevSampleL: 0,
    prevSampleR: 0,
    params: { ...LANCZOS_DEFAULT_PARAMS },
});
/** Default Lanczos strategy registration payload. */
export const lanczosStrategy = {
    id: 'lanczos',
    baseStrategy: 'linear',
    kernel: lanczosKernel,
    defaultParams: LANCZOS_DEFAULT_PARAMS,
    normalizeParams: normalizeLanczosParams,
    applyParams: applyLanczosParams,
};
/** Registers the Lanczos strategy in a compatible registry. */
export function registerLanczosStrategy(registry) {
    registry.registerInterpolationStrategy(lanczosStrategy);
}
//# sourceMappingURL=index.js.map