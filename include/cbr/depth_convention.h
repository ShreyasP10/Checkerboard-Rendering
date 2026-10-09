#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include "cbr/config.h"

namespace cbr {

constexpr float kDefaultDepthNear = 0.1f;
constexpr float kFallbackDepthFar = 1000.0f;
constexpr float kMinDeviceDepth = 1e-7f;
constexpr float kMinLinearDepth = 1e-5f;

// Host-side mirror of LinearizeDepth() in shaders/cbr_reconstruct.{comp,hlsl}; keep in sync.
//
// Device depth d in [0,1] -> positive view-space distance.
// Standard (0 = near, 1 = far): z = n*f / (f - d*(f - n))
// Reversed (1 = near, 0 = far): z = n*f / (n + d*(f - n)), or n / d when f is infinite (f <= 0)
// Returns NaN for NaN/Inf device depth so callers must treat it as disoccluded.
inline float LinearizeDepth(float d, DepthConvention convention, float zNear, float zFar) {
    if (!std::isfinite(d)) return std::numeric_limits<float>::quiet_NaN();
    if (convention == DepthConvention::Reversed) {
        if (zFar <= 0.0f) return zNear / std::max(d, kMinDeviceDepth);
        const float denom = (zNear + d * (zFar - zNear));
        if (!std::isfinite(denom) || denom == 0.0f) return std::numeric_limits<float>::quiet_NaN();
        return (zNear * zFar) / denom;
    }
    const float denom = (zFar - d * (zFar - zNear));
    if (!std::isfinite(denom) || denom == 0.0f) return std::numeric_limits<float>::quiet_NaN();
    return (zNear * zFar) / denom;
}

// Relative (scale-invariant) depth difference used by the disocclusion test.
// Returns +Inf for non-finite inputs so NaN/Inf depths always fail the tolerance test.
inline float RelativeDepthDelta(float zA, float zB) {
    if (!std::isfinite(zA) || !std::isfinite(zB)) return std::numeric_limits<float>::infinity();
    return std::fabs(zA - zB) / std::max(zA, kMinLinearDepth);
}

// Depth range actually sent to the GPU. Standard depth needs a finite far plane; a missing or
// invalid one falls back to a large finite value so the shader never divides by zero.
struct DepthRange {
    float zNear;
    float zFar;
};

inline DepthRange SanitizeDepthRange(DepthConvention convention, float zNear, float zFar) {
    DepthRange r{ zNear > 0.0f ? zNear : kDefaultDepthNear, zFar };
    if (convention == DepthConvention::Standard && !(r.zFar > r.zNear)) {
        r.zFar = kFallbackDepthFar;
    }
    if (r.zFar < 0.0f) r.zFar = 0.0f;
    return r;
}

} // namespace cbr
