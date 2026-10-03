#pragma once

#include <algorithm>
#include <cmath>
#include "cbr/config.h"

namespace cbr {

// Host-side mirror of LinearizeDepth() in shaders/cbr_reconstruct.{comp,hlsl}; keep in sync.
//
// Device depth d in [0,1] -> positive view-space distance.
// Standard (0 = near, 1 = far): z = n*f / (f - d*(f - n))
// Reversed (1 = near, 0 = far): z = n*f / (n + d*(f - n)), or n / d when f is infinite (f <= 0)
inline float LinearizeDepth(float d, DepthConvention convention, float zNear, float zFar) {
    if (convention == DepthConvention::Reversed) {
        if (zFar <= 0.0f) return zNear / std::max(d, 1e-7f);
        return (zNear * zFar) / (zNear + d * (zFar - zNear));
    }
    return (zNear * zFar) / (zFar - d * (zFar - zNear));
}

// Relative (scale-invariant) depth difference used by the disocclusion test.
inline float RelativeDepthDelta(float zA, float zB) {
    return std::fabs(zA - zB) / std::max(zA, 1e-5f);
}

// Depth range actually sent to the GPU. Standard depth needs a finite far plane; a missing or
// invalid one falls back to a large finite value so the shader never divides by zero.
struct DepthRange {
    float zNear;
    float zFar;
};

inline DepthRange SanitizeDepthRange(DepthConvention convention, float zNear, float zFar) {
    DepthRange r{ zNear > 0.0f ? zNear : 0.1f, zFar };
    if (convention == DepthConvention::Standard && !(r.zFar > r.zNear)) {
        r.zFar = 1000.0f;
    }
    if (r.zFar < 0.0f) r.zFar = 0.0f;
    return r;
}

} // namespace cbr
