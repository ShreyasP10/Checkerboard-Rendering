/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Reconstruction Compute Shader
 * Architecture: Optimized for NVIDIA Pascal (GP104 / Wave32) & AMD Radeon Vega (GCN 5.0 / Wave64)
 * Target: DirectX 12 HLSL (CS 5.0 / CS 6.0)
 * Author & Co-Owner: Shreyas Pawar
 */

// 16x16 = 256 threads per threadgroup:
// - NVIDIA Pascal (GP104): 8 warps x 32 threads = 100% warp occupancy
// - AMD Radeon Vega (GCN 5.0 / Vega 7): 4 wavefronts x 64 threads = 100% Wave64 occupancy
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
    uint   g_EnableMotionDilation;   // 1 = 3x3 closest-depth motion dilation
    float  g_JitterCompensation;     // multiplier on g_JitterDelta (1, -1 or 0)
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
// Compute Shader Entry Point
// =============================================================================

[numthreads(THREADGROUP_SIZE_X, THREADGROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID)
{
    int2 pixelCoord = int2(dispatchThreadId.xy);
    int2 targetSize = int2(g_TargetResolution);

    // Bounds check
    if (pixelCoord.x >= targetSize.x || pixelCoord.y >= targetSize.y)
    {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5f) * g_InvTargetResolution;
    int2 quarterCoord = pixelCoord / 2;

    // -------------------------------------------------------------------------
    // 1. Checkerboard Phase & MSAA Sample Selection
    // -------------------------------------------------------------------------
    uint pixelParity = (uint(pixelCoord.x) + uint(pixelCoord.y)) & 1u;
    uint frameParity = g_FrameIndex & 1u;
    bool isCurrentSampleActive = (pixelParity == frameParity);

    // In a 2x2 quarter cell, Sample 0 is top row (y even) and Sample 1 is bottom row (y odd)
    int msaaSampleIndex = int(uint(pixelCoord.y) & 1u);

    // In HLSL, Texture2DMS.Load takes (int2 Location, int SampleIndex)
    float4 currentSample = g_QuarterColorMSAA.Load(quarterCoord, msaaSampleIndex);
    float currentDepth   = g_QuarterDepthMSAA.Load(quarterCoord, msaaSampleIndex).r;

    // -------------------------------------------------------------------------
    // 2. Motion Vector Fetch & History Coordinate Calculation
    // -------------------------------------------------------------------------
    // 3x3 closest depth search for dilated motion vector (eliminates edge silhouette smearing)
    // RDR2 uses reversed-Z depth (near=1.0, far=0.0): greater depth value means closer to camera.
    // Skipped entirely when disabled (saves 9 MSAA depth fetches per pixel).
    float closestDepth = currentDepth;
    int2 closestCoord = pixelCoord;
    if (g_EnableMotionDilation != 0u)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 nCoord = clamp(pixelCoord + int2(dx, dy), int2(0, 0), targetSize - int2(1, 1));
                float d = g_QuarterDepthMSAA.Load(nCoord / 2, int(uint(nCoord.y) & 1u)).r;
                if (d > closestDepth) // Reversed-Z: greater = nearer to camera
                {
                    closestDepth = d;
                    closestCoord = nCoord;
                }
            }
        }
    }
    float2 dilatedUV = (float2(closestCoord) + 0.5f) * g_InvTargetResolution;
    float2 velocity = g_Velocity.SampleLevel(g_LinearClampSampler, dilatedUV, 0.0f).xy;
    float2 historyUV = uv - velocity - g_JitterDelta * g_JitterCompensation;

    // -------------------------------------------------------------------------
    // 3. Disocclusion & Depth Delta Test
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    float4 historyColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float previousDepth = currentDepth;

    if (historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f)
    {
        isDisoccluded = true;
    }
    else
    {
        // Point sampling for depth history avoids edge bleeding across discontinuities
        previousDepth = g_HistoryDepth.SampleLevel(g_PointClampSampler, historyUV, 0.0f).r;
        float depthDelta = abs(currentDepth - previousDepth);

        if (depthDelta > g_DepthTolerance)
        {
            isDisoccluded = true;
        }
        else
        {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
            if (historyColor.a <= 0.0f)
            {
                isDisoccluded = true;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighborhood Clamping (YCoCg Space & Variance Clipping)
    // Guarded to avoid unnecessary texture fetches when disoccluded or disabled
    // -------------------------------------------------------------------------
    if (!isDisoccluded && g_EnableColorClamping != 0u)
    {
        float3 colorMin = float3(1e6f, 1e6f, 1e6f);
        float3 colorMax = float3(-1e6f, -1e6f, -1e6f);
        float3 m1 = float3(0.0f, 0.0f, 0.0f);
        float3 m2 = float3(0.0f, 0.0f, 0.0f);

        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 neighborCoord = clamp(pixelCoord + int2(dx, dy), int2(0, 0), targetSize - int2(1, 1));
                int2 neighborQuarter = neighborCoord / 2;
                int neighborSample = int(uint(neighborCoord.y) & 1u);
                float3 neighborColor = g_QuarterColorMSAA.Load(neighborQuarter, neighborSample).rgb;

                float3 neighborClampSpace = ToClampSpace(neighborColor);
                colorMin = min(colorMin, neighborClampSpace);
                colorMax = max(colorMax, neighborClampSpace);
                m1 += neighborClampSpace;
                m2 += neighborClampSpace * neighborClampSpace;
            }
        }

        // Variance clipping: clamp history inside [mean - gamma * stdDev, mean + gamma * stdDev]
        float3 mean = m1 / 9.0f;
        float3 stdDev = sqrt(max(float3(0.0f, 0.0f, 0.0f), (m2 / 9.0f) - (mean * mean)));
        float gamma = 1.25f;
        float3 varianceMin = max(colorMin, mean - gamma * stdDev);
        float3 varianceMax = min(colorMax, mean + gamma * stdDev);

        float3 historyClampSpace = ToClampSpace(historyColor.rgb);
        historyClampSpace = clamp(historyClampSpace, varianceMin, varianceMax);
        historyColor.rgb = FromClampSpace(historyClampSpace);
    }

    // -------------------------------------------------------------------------
    // 5. Final Reconstruction
    // -------------------------------------------------------------------------
    float3 finalColor;
    float spatialDepth = currentDepth; // depth estimate for reconstructed pixels (fallback path)

    if (isCurrentSampleActive)
    {
        if (!isDisoccluded && historyColor.a > 0.0f)
        {
            finalColor = lerp(currentSample.rgb, historyColor.rgb, g_HistoryWeight);
        }
        else
        {
            finalColor = currentSample.rgb;
        }
    }
    else
    {
        if (!isDisoccluded && historyColor.a > 0.0f)
        {
            finalColor = historyColor.rgb;
        }
        else
        {
            // Spatial cross-bilateral filter fallback from 4 cardinal neighbors
            float3 accumColor = float3(0.0f, 0.0f, 0.0f);
            float  accumDepth  = 0.0f;
            float  accumWeight = 0.0f;
            const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

            // Loop is skipped entirely when the fallback is disabled (accumWeight stays 0)
            for (int i = 0; i < 4 && g_EnableSpatialFallback != 0u; ++i)
            {
                int2 sampleCoord = clamp(pixelCoord + offsets[i], int2(0, 0), targetSize - int2(1, 1));
                int2 sQuarter = sampleCoord / 2;
                int sIndex = int(uint(sampleCoord.y) & 1u);

                float3 sCol = g_QuarterColorMSAA.Load(sQuarter, sIndex).rgb;
                float  sDep = g_QuarterDepthMSAA.Load(sQuarter, sIndex).r;

                // Relative depth weighting: robust across depth ranges and reversed-Z
                float depthWeight = exp(-abs(currentDepth - sDep) / max(g_DepthTolerance, 1e-4f));
                accumColor += sCol * depthWeight;
                accumDepth += sDep * depthWeight;
                accumWeight += depthWeight;
            }

            finalColor = (accumWeight > 1e-4f) ? (accumColor / accumWeight) : currentSample.rgb;
            if (accumWeight > 1e-4f)
            {
                spatialDepth = accumDepth / accumWeight;
            }
        }
    }

    // Guard against NaN/Inf pollution in temporal feedback
    if (isnan(finalColor.r) || isinf(finalColor.r) ||
        isnan(finalColor.g) || isinf(finalColor.g) ||
        isnan(finalColor.b) || isinf(finalColor.b))
    {
        finalColor = currentSample.rgb;
    }

    // -------------------------------------------------------------------------
    // 6. Debug Modes
    // -------------------------------------------------------------------------
    if (g_DebugView == 1u)
    {
        finalColor = isCurrentSampleActive ? float3(1.0f, 1.0f, 1.0f) : float3(0.05f, 0.05f, 0.05f);
    }
    else if (g_DebugView == 2u)
    {
        finalColor = isDisoccluded ? float3(1.0f, 0.1f, 0.1f) : float3(0.1f, 0.9f, 0.1f);
    }
    else if (g_DebugView == 3u)
    {
        finalColor = float3(abs(velocity) * 50.0f, 0.0f);
    }
    else if (g_DebugView == 4u)
    {
        finalColor = currentSample.rgb;
    }

    g_OutputImage[pixelCoord] = float4(finalColor, 1.0f);

    // History depth for the next frame (see GLSL version for rationale)
    float outDepth = currentDepth;
    if (!isCurrentSampleActive)
    {
        outDepth = isDisoccluded ? spatialDepth : previousDepth;
    }
    g_OutputDepth[pixelCoord] = outDepth;
}
