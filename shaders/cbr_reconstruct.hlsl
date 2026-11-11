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
    float  g_MipLodBias;             // Retained strictly for 80-byte cbuffer ABI parity; sampler LOD bias is enforced CPU-side in Hooked_vkCreateSampler (Item M-01)
    uint   g_ColorSpace;             // 0 = YCoCg clamp, 1 = RGB clamp, 2 = ICtCp clamp (HDR10/BT.2100)
    uint   g_EnableSpatialFallback;  // 1 = cross-bilateral fallback, 0 = raw current sample
    float2 g_JitterDelta;            // subpixel projection jitter delta (jc - jp)
        uint   g_EnableMotionDilation;  // 1 = closest-depth motion dilation over the active 3x3 neighbours
    float  g_JitterCompensation;    // multiplier on g_JitterDelta (default 0: whole-pixel jitter is absorbed by the mapping)
    int    g_ShiftDirection;        // odd-frame sampling-grid shift: +1 / -1 (include/cbr/checkerboard_mapping.h)
    uint   g_DepthMode;             // 0 = Standard (0 near .. 1 far), 1 = Reversed (1 near .. 0 far)
    float  g_DepthNear;             // camera near plane
    float  g_DepthFar;              // camera far plane (0 = infinite, Reversed only)
};

// Camera matrices & post-resolve controls (16-byte aligned; preserves 80-byte b0 root constants)
cbuffer CBRCameraData : register(b1)
{
    column_major float4x4 g_CurrViewProjInv;
    column_major float4x4 g_PrevViewProj;
    float  g_SharpeningStrength;
    uint   g_EnableCameraFallback;
    uint   g_HasCameraMatrices;
    uint   g_CameraReserved;
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

// ITU-R BT.2100 ICtCp color space transform for HDR10 and Wide Color Gamut (Rec.2020)
float3 RGBtoICtCp(float3 rgb)
{
    float3 lms;
    lms.x = dot(rgb, float3(0.3593094f, 0.6976081f, -0.0358878f));
    lms.y = dot(rgb, float3(0.0828188f, 0.7938356f,  0.1233592f));
    lms.z = dot(rgb, float3(0.0125411f, 0.0767544f,  0.9107026f));

    float3 lmsPrime = sign(lms) * pow(abs(lms), float3(0.25f, 0.25f, 0.25f));

    float3 ictcp;
    ictcp.x = dot(lmsPrime, float3(0.5f, 0.5f, 0.0f));
    ictcp.y = dot(lmsPrime, float3(1.613769f, -3.323439f, 1.709670f));
    ictcp.z = dot(lmsPrime, float3(4.378183f, -4.245536f, -0.132647f));
    return ictcp;
}

float3 ICtCptoRGB(float3 ictcp)
{
    float3 lmsPrime;
    lmsPrime.x = ictcp.x + 0.00861421f * ictcp.y + 0.11102749f * ictcp.z;
    lmsPrime.y = ictcp.x - 0.00861421f * ictcp.y - 0.11102749f * ictcp.z;
    lmsPrime.z = ictcp.x + 0.56003196f * ictcp.y - 0.32062666f * ictcp.z;

    float3 lms = sign(lmsPrime) * (lmsPrime * lmsPrime * lmsPrime * lmsPrime);

    float3 rgb;
    rgb.x = dot(lms, float3( 3.4811960f, -3.1132484f,  0.5588873f));
    rgb.y = dot(lms, float3(-0.3604555f,  1.5987806f, -0.2307671f));
    rgb.z = dot(lms, float3(-0.0175594f, -0.0918741f,  1.1098062f));
    return max(float3(0.0f, 0.0f, 0.0f), rgb);
}

float3 ToClampSpace(float3 rgb)
{
    if (g_ColorSpace == 0u) return RGBtoYCoCg(rgb);
    if (g_ColorSpace == 2u) return RGBtoICtCp(rgb);
    return rgb;
}

float3 FromClampSpace(float3 c)
{
    if (g_ColorSpace == 0u) return YCoCgtoRGB(c);
    if (g_ColorSpace == 2u) return ICtCptoRGB(c);
    return c;
}

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
    if (!isnan(d) && !isinf(d) && g_DepthNear > 0.0f) {
        float n = g_DepthNear;
        float f = g_DepthFar;
        if (g_DepthMode == 1u) {
            float denom = (n + d * (f - n));
            if (abs(denom) < 1e-7f || isnan(denom) || isinf(denom)) return asfloat(0x7fc00000u);
            return (f > 0.0f) ? (n * f) / denom : n / max(d, 1e-7f);
        }
        float denom = (f - d * (f - n));
        if (abs(denom) < 1e-7f || isinf(denom) || isnan(denom)) return asfloat(0x7fc00000u);
        return (n * f) / denom;
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
    float currentDepth = 0.0f;                              // raw device depth (written to the history depth target)
    float currentLinear = 0.0f;                             // linear depth used by the occlusion test

    // The 4 cardinal neighbours of a reconstructed pixel are always natively shaded (mirrored at borders).
    int2 cardinalCoord[4] = { int2(0, 0), int2(0, 0), int2(0, 0), int2(0, 0) };
    float cardinalDepth[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float3 cardinalColor[4] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };

    if (isCurrentSampleActive) {
        currentColor = FetchColor(self);
        if (any(isnan(currentColor)) || any(isinf(currentColor))) {
            currentColor = float3(0.0f, 0.0f, 0.0f);
        }
        currentDepth = FetchDepth(self);
        currentLinear = LinearizeDepth(currentDepth);
    } else {
        // No native sample here: estimate depth from the cardinal neighbours (Intel "CSO": average LINEAR depth).
        float sumRaw = 0.0f;
        float sumLin = 0.0f;
        float validCount = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int2 c = pixelCoord + kCardinal[i];
            if (!InBounds(c, targetSize)) {
                c = pixelCoord - kCardinal[i]; // mirror at the screen border
            }
            if (!InBounds(c, targetSize)) {
                c = pixelCoord;
            }
            cardinalCoord[i] = c;
            CbrSample cs = MapPixelToSample(c, frameParity);
            cardinalDepth[i] = FetchDepth(cs);
            cardinalColor[i] = FetchColor(cs);
            float raw = cardinalDepth[i];
            float lin = LinearizeDepth(raw);
            if (!isnan(raw) && !isinf(raw) && !isnan(lin) && !isinf(lin)) {
                sumRaw += raw;
                sumLin += lin;
                validCount += 1.0f;
            }
        }
        if (validCount > 0.0f) {
            currentDepth = sumRaw / validCount;
            currentLinear = sumLin / validCount;
        } else {
            currentDepth = (g_DepthMode == 1u) ? 0.0f : 1.0f;
            currentLinear = 1000.0f;
        }
    }

    // -------------------------------------------------------------------------
    // 2. Motion vector (optionally dilated to the nearest-depth neighbour) & history UV
    // -------------------------------------------------------------------------
    // Only natively shaded neighbours carry depth, so the dilation scans just those (5 of the 3x3 around an
    // active pixel, 4 around a reconstructed one). Skipped entirely when disabled.
    // For inactive pixels, the 4 cardinal neighbours are already fetched in step 1, avoiding redundant texture fetches.
    int2 motionCoord = pixelCoord;
    if (g_EnableMotionDilation != 0u) {
        float best = currentDepth;
        if (isCurrentSampleActive) {
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
            for (int i = 0; i < 4; ++i) {
                int2 n = pixelCoord + kCardinal[i];
                if (!InBounds(n, targetSize)) continue;
                float d = cardinalDepth[i];
                if (IsNearer(d, best)) {
                    best = d;
                    motionCoord = n;
                }
            }
        }
    }

    // Sample quarter-resolution velocity buffer (targetSize >> 1) with clamped coordinates (Item C-05)
    int2 velCoord = clamp(motionCoord >> 1, int2(0, 0), max((targetSize >> 1) - int2(1, 1), int2(0, 0)));
    float2 velocity = g_Velocity.Load(int3(velCoord, 0)).xy;
    velocity = clamp(velocity, float2(-0.5f, -0.5f), float2(0.5f, 0.5f));
    bool velocityBad = isinf(velocity.x) || isnan(velocity.x) || isinf(velocity.y) || isnan(velocity.y)
        || isinf(uv.x) || isnan(uv.x) || isinf(uv.y) || isnan(uv.y);
    // Camera-only depth unprojection fallback (Section 5.4) when velocity is zeroed/unbound or fallback is active
    bool velZero = dot(velocity, velocity) < 1e-12f;
    if (g_HasCameraMatrices != 0u &&
        (g_EnableCameraFallback != 0u || velZero || velocityBad)) {
        float4 clipPos = float4(uv * 2.0f - 1.0f, currentDepth, 1.0f);
        float4 worldH = mul(g_CurrViewProjInv, clipPos);
        if (!isinf(worldH.w) && !isnan(worldH.w) && abs(worldH.w) > 1e-6f) {
            float4 worldPos = worldH / worldH.w;
            float4 prevClip = mul(g_PrevViewProj, worldPos);
            if (!isinf(prevClip.w) && !isnan(prevClip.w) && abs(prevClip.w) > 1e-6f) {
                float2 prevUV = (prevClip.xy / prevClip.w) * 0.5f + 0.5f;
                float2 camVel = clamp(uv - prevUV, float2(-0.5f, -0.5f), float2(0.5f, 0.5f));
                if (!isinf(camVel.x) && !isnan(camVel.x) && !isinf(camVel.y) && !isnan(camVel.y)) {
                    velocity = camVel;
                    velocityBad = isinf(uv.x) || isnan(uv.x) || isinf(uv.y) || isnan(uv.y);
                }
            }
        }
    }
    // JitterCompensation (Item M-08): 0.0 when velocity buffer is unjittered or Strategy A viewport shift is active;
    // set to 1.0 only if velocity vectors encode the projection jitter delta (verify via RenderDoc).
    // Jitter dampener: dampens jitter compensation on zero-velocity passes (transparent particles, fog, smoke)
    // to prevent half-pixel oscillation when alpha-blended particles lack motion vectors.
    float jitterDampener = (dot(velocity, velocity) < 1e-9f) ? 0.0f : 1.0f;
    float2 historyUV = uv - velocity - (g_JitterDelta * (g_JitterCompensation * jitterDampener));
    float2 minUV = 0.5f * g_InvTargetResolution;
    float2 maxUV = float2(1.0f, 1.0f) - minUV;
    historyUV = clamp(historyUV, minUV, maxUV);
    bool historyUVBad = velocityBad || isinf(historyUV.x) || isnan(historyUV.x)
        || isinf(historyUV.y) || isnan(historyUV.y);

    // -------------------------------------------------------------------------
    // 3. Disocclusion & depth delta test (scale-invariant: relative difference of LINEAR depth)
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    float4 historyColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float previousDepth = currentDepth;
    float depthDelta = 0.0f;

    if (historyUVBad || g_FrameIndex == 0u || historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f) {
        isDisoccluded = true; // First frame, bad velocity/UV, or sample moved outside screen space
    } else {
        // Exact integer texel load: matches GLSL texelFetch(u_HistoryDepth, historyCoord, 0).r without UV rounding (Item M-02)
        int2 historyCoord = clamp(int2(historyUV * g_TargetResolution), int2(0, 0), targetSize - int2(1, 1));
        previousDepth = g_HistoryDepth.Load(int3(historyCoord, 0)).r;
        float prevLin = LinearizeDepth(previousDepth);
        bool depthBad = isinf(currentLinear) || isnan(currentLinear) || isinf(prevLin) || isnan(prevLin);
        depthDelta = depthBad ? 10.0f : abs(currentLinear - prevLin) / max(currentLinear, 1e-5f);
        float dynamicTolerance = g_DepthTolerance * max(1.0f, 1.0f + 0.0005f * currentLinear);
        if (depthDelta > dynamicTolerance) {
            isDisoccluded = true;
        } else {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
            if (historyColor.a < 0.5f) {
                isDisoccluded = true; // Cleared history buffer
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighbourhood statistics & variance clipping (anti-ghosting)
    // Uses ONLY natively shaded neighbours: samples that do not exist this frame must not enter the statistics.
    // Clamping is active on both static and moving geometry: static geometry subject to dynamic lighting,
    // shadows, or moving occluders still requires history bounding to prevent temporal ghosting.
    // Variance clipping is bypassed on strictly static geometry with sub-tolerance depth variance.
    // -------------------------------------------------------------------------
    bool isStatic = dot(velocity, velocity) < 1e-9f && depthDelta < (0.05f * g_DepthTolerance);
    if (!isDisoccluded && g_EnableColorClamping != 0u && !isStatic) {
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
            } else if (!isCurrentSampleActive) {
                c = ToClampSpace(cardinalColor[i]);
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
            float3 rawStdDev = sqrt(max(float3(0.0f, 0.0f, 0.0f), (m2 / n) - (mean * mean)));
            float3 stdDev = max(rawStdDev, float3(0.005f, 0.005f, 0.005f));
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
            // Blend with history for temporal stability
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
                float3 sCol = cardinalColor[i];
                float sLin = LinearizeDepth(cardinalDepth[i]);

                // Relative linear-depth weighting: scale-invariant, independent of the depth convention
                // Fast base-2 exponentiation: exp(-x) == exp2(-x * log2(e))
                float depthWeight = exp2((-abs(currentLinear - sLin) / (max(currentLinear, 1e-5f) * max(g_DepthTolerance, 1e-4f))) * 1.44269504f);

                accumColor += sCol * depthWeight;
                accumDepth += cardinalDepth[i] * depthWeight;
                accumWeight += depthWeight;
            }

            if (accumWeight > 1e-4f) {
                finalColor = accumColor / accumWeight;
                spatialDepth = accumDepth / accumWeight;
            } else {
                finalColor = isCurrentSampleActive ? currentColor : (
                    cardinalColor[0] + cardinalColor[1] + cardinalColor[2] + cardinalColor[3]
                ) * 0.25f;
                spatialDepth = isCurrentSampleActive ? currentDepth : (cardinalDepth[0] + cardinalDepth[1] + cardinalDepth[2] + cardinalDepth[3]) * 0.25f;
            }
        }
    }

    // Guard against NaN/Inf pollution in the temporal feedback loop (Item H-05)
    if (any(isnan(finalColor)) || any(isinf(finalColor))) {
        float3 fallback = isCurrentSampleActive ? currentColor : cardinalColor[0];
        if (any(isnan(fallback)) || any(isinf(fallback))) {
            fallback = float3(0.0f, 0.0f, 0.0f);
        }
        finalColor = fallback;
    }

    // 5-tap RCAS contrast-adaptive sharpening (Section 5.6; bypassed when 0.0 or debug overlay active)
    if (g_SharpeningStrength > 0.0f && g_DebugView == 0u) {
        float3 rc0 = cardinalColor[0];
        float3 rc1 = cardinalColor[1];
        float3 rc2 = cardinalColor[2];
        float3 rc3 = cardinalColor[3];
        if (isCurrentSampleActive) {
            for (int i = 0; i < 4; ++i) {
                float3 sc = finalColor;
                if (!isDisoccluded && g_FrameIndex > 0u) {
                    float2 nUV = clamp(historyUV + float2(kCardinal[i]) * g_InvTargetResolution, minUV, maxUV);
                    float4 hc = g_HistoryColor.SampleLevel(g_LinearClampSampler, nUV, 0.0f);
                    if (hc.a >= 0.5f && !any(isnan(hc.rgb)) && !any(isinf(hc.rgb))) {
                        sc = hc.rgb;
                    }
                } else {
                    int2 stepAxis = (i < 2) ? int2(kCardinal[i].x, 1) : int2(1, kCardinal[i].y);
                    int2 diag = pixelCoord + stepAxis;
                    if (!InBounds(diag, targetSize)) diag = pixelCoord - stepAxis;
                    if (InBounds(diag, targetSize)) {
                        float3 dc = FetchColor(MapPixelToSample(diag, frameParity));
                        if (!any(isnan(dc)) && !any(isinf(dc))) sc = dc;
                    }
                }
                if (i == 0) rc0 = sc;
                else if (i == 1) rc1 = sc;
                else if (i == 2) rc2 = sc;
                else rc3 = sc;
            }
        }
        float3 mn4 = min(min(rc0, rc1), min(rc2, rc3));
        float3 mx4 = max(max(rc0, rc1), max(rc2, rc3));
        float3 mn5 = min(mn4, finalColor);
        float3 mx5 = max(mx4, finalColor);
        float3 amp = sqrt(clamp(min(max(mn5, float3(0.0f, 0.0f, 0.0f)), max(mx5 - mn5, float3(0.0f, 0.0f, 0.0f))) / max(mx5, float3(1e-5f, 1e-5f, 1e-5f)),
                                float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f)));
        float w = max(-0.1875f * clamp(g_SharpeningStrength, 0.0f, 1.0f) * min(amp.r, min(amp.g, amp.b)), -0.1875f);
        float3 sharpened = max((finalColor + w * (rc0 + rc1 + rc2 + rc3)) / max(1.0f + 4.0f * w, 1e-4f), float3(0.0f, 0.0f, 0.0f));
        if (!any(isnan(sharpened)) && !any(isinf(sharpened))) {
            finalColor = sharpened;
        }
    }

    // -------------------------------------------------------------------------
    // 6. Debug visualization modes
    // -------------------------------------------------------------------------
    if (g_DebugView == 1u) {
        // Checkerboard mask: White = natively shaded this frame, Black = reconstructed
        finalColor = isCurrentSampleActive ? float3(1.0f, 1.0f, 1.0f) : float3(0.05f, 0.05f, 0.05f);
    } else if (g_DebugView == 2u) {
        // Disocclusion heatmap: Green = temporal history, Red = spatial disocclusion
        float t = clamp(depthDelta / max(g_DepthTolerance, 1e-4f), 0.0f, 1.0f);
        finalColor = isDisoccluded ? float3(1.0f, 0.1f, 0.1f) : lerp(float3(0.1f, 0.9f, 0.1f), float3(1.0f, 0.9f, 0.1f), t);
    } else if (g_DebugView == 3u) {
        // Motion vector field: directional color wheel
        float2 normVel = velocity * 20.0f;
        float speed = clamp(length(normVel), 0.0f, 1.0f);
        finalColor = float3(clamp(normVel * 0.5f + 0.5f, 0.0f, 1.0f), speed);
    } else if (g_DebugView == 4u) {
        // Quarter-resolution raw colour without directional bias
        finalColor = isCurrentSampleActive ? currentColor : (
            cardinalColor[0] + cardinalColor[1] + cardinalColor[2] + cardinalColor[3]
        ) * 0.25f;
    } else if (g_DebugView == 5u) {
        // Spatial baseline resolve: average of 4 cardinal samples for reconstructed pixels
        // (direct visual A/B baseline comparison against temporal CBR, matching reference Space toggle)
        finalColor = isCurrentSampleActive ? currentColor : (
            cardinalColor[0] + cardinalColor[1] + cardinalColor[2] + cardinalColor[3]
        ) * 0.25f;
    }

    // Write final reconstructed pixel to output storage image (alpha = 1.0 for valid history)
    g_OutputImage[pixelCoord] = float4(finalColor, 1.0f);

    // History depth for the next frame (see GLSL version for rationale)
    float outDepth = currentDepth;
    if (!isCurrentSampleActive) {
        outDepth = isDisoccluded ? spatialDepth : previousDepth;
    }
    if (isnan(outDepth) || isinf(outDepth)) {
        outDepth = (g_DepthMode == 1u) ? 0.0f : 1.0f;
    }
    g_OutputDepth[pixelCoord] = outDepth;
}
