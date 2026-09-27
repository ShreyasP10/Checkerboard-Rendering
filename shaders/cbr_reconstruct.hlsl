/**
 * RDR2 Checkerboard Rendering Mod (CBR) - Reconstruction Compute Shader
 * Architecture: Optimized for NVIDIA Pascal (GP104 / GTX 1070 Ti) & Modern GPUs
 * Target: DirectX 12 HLSL (CS 5.0 / CS 6.0)
 * Author & Co-Owner: Shreyas Pawar
 */

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

RWTexture2D<float4> g_OutputImage      : register(u0);

// =============================================================================
// Constant Buffer
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
    return max(0.0f.xxx, float3(R, G, B));
}

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

    // Subpixel MSAA sample index calculation
    int msaaSampleIndex = int((uint(pixelCoord.x) & 1u) ^ (uint(pixelCoord.y) & 1u));

    float4 currentSample = g_QuarterColorMSAA.Load(quarterCoord, msaaSampleIndex);
    float currentDepth   = g_QuarterDepthMSAA.Load(quarterCoord, msaaSampleIndex).r;

    // -------------------------------------------------------------------------
    // 2. Motion Vector Fetch & History Coordinate Calculation
    // -------------------------------------------------------------------------
    float2 velocity = g_Velocity.SampleLevel(g_LinearClampSampler, uv, 0.0f).xy;
    float2 historyUV = uv - velocity;

    // -------------------------------------------------------------------------
    // 3. Disocclusion & Depth Delta Test
    // -------------------------------------------------------------------------
    bool isDisoccluded = false;
    float4 historyColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (historyUV.x < 0.0f || historyUV.x > 1.0f || historyUV.y < 0.0f || historyUV.y > 1.0f)
    {
        isDisoccluded = true;
    }
    else
    {
        float previousDepth = g_HistoryDepth.SampleLevel(g_LinearClampSampler, historyUV, 0.0f).r;
        float depthDelta = abs(currentDepth - previousDepth) / max(currentDepth, 1e-5f);

        if (depthDelta > g_DepthTolerance)
        {
            isDisoccluded = true;
        }
        else
        {
            historyColor = g_HistoryColor.SampleLevel(g_LinearClampSampler, historyUV, 0.0f);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Neighborhood Clamping (YCoCg Space)
    // -------------------------------------------------------------------------
    float3 colorMin = float3(1e6f, 1e6f, 1e6f);
    float3 colorMax = float3(-1e6f, -1e6f, -1e6f);

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighborCoord = clamp(pixelCoord + int2(dx, dy), int2(0, 0), targetSize - int2(1, 1));
            int2 neighborQuarter = neighborCoord / 2;
            int neighborSample = int((uint(neighborCoord.x) & 1u) ^ (uint(neighborCoord.y) & 1u));
            float3 neighborColor = g_QuarterColorMSAA.Load(neighborQuarter, neighborSample).rgb;

            float3 neighborYCoCg = RGBtoYCoCg(neighborColor);
            colorMin = min(colorMin, neighborYCoCg);
            colorMax = max(colorMax, neighborYCoCg);
        }
    }

    if (!isDisoccluded && g_EnableColorClamping != 0u)
    {
        float3 historyYCoCg = RGBtoYCoCg(historyColor.rgb);
        historyYCoCg = clamp(historyYCoCg, colorMin, colorMax);
        historyColor.rgb = YCoCgtoRGB(historyYCoCg);
    }

    // -------------------------------------------------------------------------
    // 5. Final Reconstruction
    // -------------------------------------------------------------------------
    float3 finalColor;

    if (isCurrentSampleActive)
    {
        if (!isDisoccluded && historyColor.a > 0.0f)
        {
            finalColor = lerp(currentSample.rgb, historyColor.rgb, 1.0f - g_HistoryWeight);
        }
        else
        {
            finalColor = currentSample.rgb;
        }
    }
    else
    {
        if (!isDisoccluded)
        {
            finalColor = historyColor.rgb;
        }
        else
        {
            // Spatial cross-bilateral filter fallback
            float3 accumColor = float3(0.0f, 0.0f, 0.0f);
            float  accumWeight = 0.0f;
            const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

            for (int i = 0; i < 4; ++i)
            {
                int2 sampleCoord = clamp(pixelCoord + offsets[i], int2(0, 0), targetSize - int2(1, 1));
                int2 sQuarter = sampleCoord / 2;
                int sIndex = int((uint(sampleCoord.x) & 1u) ^ (uint(sampleCoord.y) & 1u));

                float3 sCol = g_QuarterColorMSAA.Load(sQuarter, sIndex).rgb;
                float  sDep = g_QuarterDepthMSAA.Load(sQuarter, sIndex).r;

                float depthWeight = exp(-abs(currentDepth - sDep) * 100.0f);
                accumColor += sCol * depthWeight;
                accumWeight += depthWeight;
            }

            finalColor = (accumWeight > 1e-4f) ? (accumColor / accumWeight) : currentSample.rgb;
        }
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
}
