/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Reconstruction Compute Shader
 * Architecture: Optimized for NVIDIA Pascal (GP104 / Wave32) & AMD Radeon Vega (GCN 5.0 / Wave64)
 * Target: DirectX 12 HLSL (CS 5.0 / CS 6.0)
 * Author & Co-Owner: Shreyas Pawar
 */

// 16x16 = 256 threads per threadgroup, an exact multiple of both wave sizes (no partial waves):
// - NVIDIA Pascal (GP104): 8 warps x 32 threads
// - AMD Radeon Vega (GCN 5.0 / Vega 7): 4 wavefronts x 64 threads
// (Achieved occupancy additionally depends on register and LDS usage; measure with a profiler.)
#define THREADGROUP_SIZE_X 16
#define THREADGROUP_SIZE_Y 16

// =============================================================================
// Resource Bindings
// =============================================================================

Texture2DMS<float4> g_QuarterColorMSAA : register(t0);
Texture2DMS<float>  g_QuarterDepthMSAA : register(t1);
Texture2D<float4>   g_HistoryColor     : register(t2);
Texture2D<float>    g_HistoryDepth     : register(t3);
Texture2D<float2>   g_Velocity         : register(t4);

SamplerState        g_LinearClampSampler : register(s0);
SamplerState        g_PointClampSampler  : register(s1);

RWTexture2D<float4> g_OutputImage      : register(u0);
// Full-resolution depth written for the next frame's disocclusion test (becomes g_HistoryDepth)
RWTexture2D<float>  g_OutputDepth      : register(u1);

// =============================================================================
// Constant Buffer (16-byte aligned)
// =============================================================================
cbuffer CBRConstants : register(b0)
{
    float2 g_TargetResolution;       // (3840.0f, 2160.0f)
    float2 g_InvTargetResolution;    // (1.0f / 3840.0f, 1.0f / 2160.0f)
    uint   g_FrameIndex;             // Monotonically increasing frame counter
    float  g_DepthTolerance;         // Disocclusion sensitivity threshold (0.010f)
    float  g_HistoryWeight;          // Temporal blend weight (0.90f)
    uint   g_DebugView;              // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw
    uint   g_EnableColorClamping;    // 1 = True, 0 = False
    float  g_MipLodBias;             // Texture LOD bias (-0.5f)
    uint   g_ColorSpace;             // 0 = YCoCg clamp, 1 = RGB clamp
    uint   g_EnableSpatialFallback;  // 1 = cross-bilateral fallback, 0 = raw current sample
    float2 g_JitterDelta;            // subpixel projection jitter delta (jc - jp)
        uint   g_EnableMotionDilation;  // 1 = closest-depth motion dilation over the active 3x3 neighbours
    float  g_JitterCompensation;    // multiplier on g_JitterDelta (default 0: whole-pixel jitter is absorbed by the mapping)
    int    g_ShiftDirection;        // odd-frame sampling-grid shift: +1 / -1 (include/cbr/checkerboard_mapping.h)
    uint   g_DepthMode;             // 0 = Standard (0 near .. 1 far), 1 = Reversed (1 near .. 0 far)
    float  g_DepthNear;             // camera near plane
    float  g_DepthFar;              // camera far plane (0 = infinite, Reversed only)
};

// =============================================================================
// Color Space Conversions (YCoCg)
// =============================================================================

float3 RGBtoYCoCg(float3 rgb)
{
    float Y  = dot(rgb, float3(0.25f, 0.50f, 0.25f));
    float Co = dot(rgb, float3(0.50f, 0.00f, -0.50f));
    float Cg = dot(rgb, float3(-0.25f, 0.50f, -0.25f));
    return float3(Y, Co, Cg);
}

float3 YCoCgtoRGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    float R  = Y + Co - Cg;
    float G  = Y + Cg;
    float B  = Y - Co - Cg;
    return max(float3(0.0f, 0.0f, 0.0f), float3(R, G, B));
}

float3 ToClampSpace(float3 rgb)   { return (g_ColorSpace == 0u) ? RGBtoYCoCg(rgb) : rgb; }
float3 FromClampSpace(float3 c)   { return (g_ColorSpace == 0u) ? YCoCgtoRGB(c) : max(float3(0.0f, 0.0f, 0.0f), c); }

// =============================================================================
// Checkerboard sample mapping. MIRRORS include/cbr/checkerboard_mapping.h: keep the two in sync
// (the C++ version is verified against a geometric simulation in tests/test_core.cpp).
//
// Standard 2x MSAA sample locations (y down): sample 0 = (0.75, 0.75) bottom-right,
// sample 1 = (0.25, 0.25) top-left. Even frames shade pixels with (x&1)==(y&1); odd frames (grid shifted
// by one full-resolution pixel in x) shade pixels with (x&1)!=(y&1).
// =============================================================================
struct CbrSample {
    bool isActive;  // a native sample exists for this pixel this frame
    int2 quarter;   // quarter-resolution texel (already clamped to the render target)
    int  sampleIdx; // MSAA sample index
};

CbrSample MapPixelToSample(int2 p, uint frameParity) {
    CbrSample s;
    int yBit = p.y & 1;
    s.isActive = (((uint(p.x) ^ uint(p.y)) & 1u) == frameParity);
    int qx;
    if (frameParity == 0u) {
        qx = p.x >> 1;
    } else if (g_ShiftDirection >= 0) {
        qx = max(p.x - yBit, 0) >> 1;
    } else {
        qx = (p.x + 1 - yBit) >> 1;
    }
    int2 qMax = int2(g_TargetResolution * 0.5f) - int2(1, 1);
    s.quarter = int2(clamp(qx, 0, qMax.x), min(p.y >> 1, qMax.y));
    s.sampleIdx = 1 - yBit;
    return s;
}

float3 FetchColor(CbrSample s) {
    return g_QuarterColorMSAA.Load(s.quarter, s.sampleIdx).rgb;
}

float FetchDepth(CbrSample s) {
    return g_QuarterDepthMSAA.Load(s.quarter, s.sampleIdx).r;
}

// MIRRORS include/cbr/depth_convention.h
// NaN/Inf device depth yields NaN so the disocclusion test must reject it.
float LinearizeDepth(float d) {
    if (!isnan(d) && !isinf(d)) {
        float n = g_DepthNear;
        float f = g_DepthFar;
        if (g_DepthMode == 1u) {
            return (f > 0.0f) ? (n * f) / (n + d * (f - n)) : n / max(d, 1e-7f);
        }
        float denom = (f - d * (f - n));
        if (denom != 0.0f && !isinf(denom) && !isnan(denom)) return (n * f) / denom;
    }
    return asfloat(0x7fc00000u); // quiet NaN
}

bool IsNearer(float a, float b) {
    return (g_DepthMode == 1u) ? (a > b) : (a < b);
}

bool InBounds(int2 p, int2 size) {
    return p.x >= 0 && p.y >= 0 && p.x < size.x && p.y < size.y;
}

// =============================================================================
// Compute Shader Entry Point
// =============================================================================
[numthreads(THREADGROUP_SIZE_X, THREADGROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID) {
    int2 pixelCoord = int2(dispatchThreadId.xy);
    int2 targetSize = int2(g_TargetResolution);

    // Bounds check
    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5f) * g_InvTargetResolution;
    uint frameParity = g_FrameIndex & 1u;
    static const int2 kCardinal[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

    // -------------------------------------------------------------------------
    // 1. Current-frame sample (native if this pixel was shaded this frame)
    // -------------------------------------------------------------------------
    CbrSample self = MapPixelToSample(pixelCoord, frameParity);
    bool isCurrentSampleActive = self.isActive;
    float3 currentColor = float3(0.0f, 0.0f, 0.0f); // valid only for active pixels
    float currentDepth;                              // raw device depth (written to the history depth target)
    float currentLinear;                             // linear depth used by the occlusion test

    // The 4 cardinal neighbours of a reconstructed pixel are always natively shaded (mirrored at borders).
    int2 cardinalCoord[4] = { int2(0, 0), int2(0, 0), int2(0, 0), int2(0, 0) };
    float cardinalDepth[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (isCurrentSampleActive) {
        currentColor = FetchColor(self);
        currentDepth = FetchDepth(self);
        currentLinear = LinearizeDepth(currentDepth);
    } else {
        // No native sample here: estimate depth from the cardinal neighbours (Intel "CSO": average LINEAR depth).
        float sumRaw = 0.0f;
        float sumLin = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int2 c = pixelCoord + kCardinal[i];
            if (!InBounds(c, targetSize)) {
                c = pixelCoord - kCardinal[i]; // mirror at the screen border
            }
            cardinalCoord[i] = c;
            cardinalDepth[i] = FetchDepth(MapPixelToSample(c, frameParity));
            sumRaw += cardinalDepth[i];
            sumLin += LinearizeDepth(cardinalDepth[i]);
        }
        currentDepth = sumRaw * 0.25f;
        currentLinear = sumLin * 0.25f;
    }

    // -------------------------------------------------------------------------
    // 2. Motion vector (optionally dilated to the nearest-depth neighbour) & history UV
    // -------------------------------------------------------------------------
    // Only natively shaded neighbours carry depth, so the dilation scans just those (5 of the 3x3 around an
    // active pixel, 4 around a reconstructed one). Skipped entirely when disabled.
    // For inactive pixels, the 4 cardinal neighbours are already fetched in step 1, avoiding redundant texture fetches.
    int2 motionCoord = pixelCoord;
    if (g_EnableMotionDilation != 0u) {
        if (isCurrentSampleActive) {
            float best = currentDepth;
            static const int2 kDiagonal[4] = { int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1) };
            for (int i = 0; i < 4; ++i) {
                int2 n = pixelCoord + kDiagonal[i];
                if (!InBounds(n, targetSize)) continue;
                float d = FetchDepth(MapPixelToSample(n, frameParity));
                if (IsNearer(d, best)) {
                    best = d;
                    motionCoord = n;
                }
            }
        } else {
            bool found = false;
            float best = 0.0f;
            for (int i = 0; i < 4; ++i) {
                int2 n = pixelCoord + kCardinal[i];
                if (!InBounds(n, targetSize)) continue;
                float d = cardinalDepth[i];
                if (!found || IsNearer(d, best)) {
                    found = true;
                    best = d;
                    motionCoord = n;
                }
            }
        }
    }

    float2 dilatedUV = (float2(motionCoord) + 0.5f) * g_InvTargetResolution;
    float2 velocity = g_Velocity.SampleLevel(g_LinearClampSampler, dilatedUV, 0.0f).xy;
    bool velocityBad = isinf(velocity.x) || isnan(velocity.x) || isinf(velocity.y) || isnan(velocity.y)
        || isinf(uv.x) || isnan(uv.x) || isinf(uv.y) || isnan(uv.y);
    float2 historyUV = uv - velocity - g_JitterDelta * g_JitterCompensation;
    bool historyUVBad = velocityBad || isinf(historyUV.x) || isnan(historyUV.x)
        || isinf(historyUV.y) || isnan(historyUV.y);

    // -------------------------------------------------------------------------
    // 3. Disocclusion & depth delta test (scale-invariant: relative difference of LINEAR depth)
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    float4 historyColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float previousDepth = currentDepth;

    if (historyUVBad || g_FrameIndex == 0u || historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f) {
        isDisoccluded = true; // First frame, bad velocity/UV, or sample moved outside screen space
    } else {
        previousDepth = g_HistoryDepth.SampleLevel(g_PointClampSampler, historyUV, 0.0f).r;
        float prevLin = LinearizeDepth(previousDepth);
        bool depthBad = isinf(currentLinear) || isnan(currentLinear) || isinf(prevLin) || isnan(prevLin);
        float depthDelta = depthBad ? 10.0f : abs(currentLinear - prevLin) / max(currentLinear, 1e-5f);
        if (depthDelta > g_DepthTolerance) {
            isDisoccluded = true;
        } else {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
            if (historyColor.a <= 0.0f) {
                isDisoccluded = true; // Cleared history buffer
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighbourhood statistics & variance clipping (anti-ghosting)
    // Uses ONLY natively shaded neighbours: samples that do not exist this frame must not enter the statistics.
    // -------------------------------------------------------------------------
    if (!isDisoccluded && g_EnableColorClamping != 0u) {
        float3 colorMin = float3(1e6f, 1e6f, 1e6f);
        float3 colorMax = float3(-1e6f, -1e6f, -1e6f);
        float3 m1 = float3(0.0f, 0.0f, 0.0f);
        float3 m2 = float3(0.0f, 0.0f, 0.0f);
        float n = 0.0f;

        int numNeighbors = isCurrentSampleActive ? 5 : 4;
        const int2 kDiagonal[4] = { int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1) };
        
        for (int i = 0; i < numNeighbors; ++i) {
            int2 offset;
            if (isCurrentSampleActive) {
                offset = (i == 0) ? int2(0, 0) : kDiagonal[i - 1];
            } else {
                offset = kCardinal[i];
            }
            
            int2 nc = pixelCoord + offset;
            if (!InBounds(nc, targetSize)) continue;
            
            float3 c;
            if (isCurrentSampleActive && i == 0) {
                c = ToClampSpace(currentColor);
            } else {
                c = ToClampSpace(FetchColor(MapPixelToSample(nc, frameParity)));
            }
            
            colorMin = min(colorMin, c);
            colorMax = max(colorMax, c);
            m1 += c;
            m2 += c * c;
            n += 1.0f;
        }

        if (n >= 2.0f) {
            // Variance clipping: clamp history inside [mean - gamma * stdDev, mean + gamma * stdDev]
            float3 mean = m1 / n;
            float3 stdDev = sqrt(max(float3(0.0f, 0.0f, 0.0f), (m2 / n) - (mean * mean)));
            const float gamma = 1.25f; // kVarianceClipGamma (include/cbr/limits.h)
            float3 varianceMin = max(colorMin, mean - gamma * stdDev);
            float3 varianceMax = min(colorMax, mean + gamma * stdDev);
            varianceMax = max(varianceMin, varianceMax); // Ensure varianceMin <= varianceMax to prevent clamp inversion

            float3 historyClampSpace = ToClampSpace(historyColor.rgb);
            historyClampSpace = clamp(historyClampSpace, varianceMin, varianceMax);
            historyColor.rgb = FromClampSpace(historyClampSpace);
        }
    }

    // -------------------------------------------------------------------------
    // 5. Final pixel reconstruction
    // -------------------------------------------------------------------------
    float3 finalColor;
    float spatialDepth = currentDepth; // depth estimate for reconstructed pixels (fallback path)

    if (isCurrentSampleActive) {
        if (!isDisoccluded && historyColor.a > 0.0f) {
            // Blend with history for temporal stability (g_HistoryWeight controls history influence)
            finalColor = lerp(currentColor, historyColor.rgb, g_HistoryWeight);
        } else {
            finalColor = currentColor;
        }
    } else {
        if (!isDisoccluded && historyColor.a > 0.0f) {
            finalColor = historyColor.rgb;
        } else {
            // Spatial cross-bilateral reconstruction from the 4 cardinal (natively shaded) neighbours.
            // With the fallback disabled the nearest neighbour is copied (no native sample exists here).
            float3 accumColor = float3(0.0f, 0.0f, 0.0f);
            float accumDepth = 0.0f;
            float accumWeight = 0.0f;

            for (int i = 0; i < 4 && g_EnableSpatialFallback != 0u; ++i) {
                float3 sCol = FetchColor(MapPixelToSample(cardinalCoord[i], frameParity));
                float sLin = LinearizeDepth(cardinalDepth[i]);

                // Relative linear-depth weighting: scale-invariant, independent of the depth convention
                float depthWeight = exp(-abs(currentLinear - sLin) / (max(currentLinear, 1e-5f) * max(g_DepthTolerance, 1e-4f)));

                accumColor += sCol * depthWeight;
                accumDepth += cardinalDepth[i] * depthWeight;
                accumWeight += depthWeight;
            }

            if (accumWeight > 1e-4f) {
                finalColor = accumColor / accumWeight;
                spatialDepth = accumDepth / accumWeight;
            } else {
                finalColor = FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
                spatialDepth = cardinalDepth[0];
            }
        }
    }

    // Guard against NaN/Inf pollution in the temporal feedback loop
    if (any(isnan(finalColor)) || any(isinf(finalColor))) {
        finalColor = isCurrentSampleActive ? currentColor : FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
    }

    // -------------------------------------------------------------------------
    // 6. Debug visualization modes
    // -------------------------------------------------------------------------
    if (g_DebugView == 1u) {
        // Checkerboard mask: White = natively shaded this frame, Black = reconstructed
        finalColor = isCurrentSampleActive ? float3(1.0f, 1.0f, 1.0f) : float3(0.05f, 0.05f, 0.05f);
    } else if (g_DebugView == 2u) {
        // Disocclusion heatmap: Green = temporal history, Red = spatial disocclusion
        finalColor = isDisoccluded ? float3(1.0f, 0.1f, 0.1f) : float3(0.1f, 0.9f, 0.1f);
    } else if (g_DebugView == 3u) {
        // Motion vector field
        finalColor = float3(abs(velocity) * 50.0f, 0.0f);
    } else if (g_DebugView == 4u) {
        // Quarter-resolution raw colour (nearest native sample for reconstructed pixels)
        finalColor = isCurrentSampleActive ? currentColor : FetchColor(MapPixelToSample(cardinalCoord[0], frameParity));
    } else if (g_DebugView == 5u) {
        // Spatial baseline resolve: average of 4 cardinal samples for reconstructed pixels
        // (direct visual A/B baseline comparison against temporal CBR, matching reference Space toggle)
        finalColor = isCurrentSampleActive ? currentColor : (
            FetchColor(MapPixelToSample(cardinalCoord[0], frameParity)) +
            FetchColor(MapPixelToSample(cardinalCoord[1], frameParity)) +
            FetchColor(MapPixelToSample(cardinalCoord[2], frameParity)) +
            FetchColor(MapPixelToSample(cardinalCoord[3], frameParity))
        ) * 0.25f;
    }

    // Write final reconstructed pixel to output storage image (alpha = 1.0 for valid history)
    g_OutputImage[pixelCoord] = float4(finalColor, 1.0f);

    // History depth for the next frame (see GLSL version for rationale)
    float outDepth = currentDepth;
    if (!isCurrentSampleActive) {
        outDepth = isDisoccluded ? spatialDepth : previousDepth;
    }
    g_OutputDepth[pixelCoord] = outDepth;
}
