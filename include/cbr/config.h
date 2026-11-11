#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <mutex>
#include "cbr/limits.h"

namespace cbr {

enum class GraphicsApi {
    Vulkan,
    D3D12,
    Auto
};

enum class JitterPattern {
    Checkerboard,
    Halton
};

enum class DepthConvention {
    Standard,
    Reversed
};

enum class ColorSpace {
    YCoCg,
    RGB,
    ICtCp // HDR10 / BT.2100 Wide Color Gamut (Rec.2020) with perceptual quantization
};

enum class SamplingStrategy {
    StrategyA, // ViewportShift (standard 2x MSAA sample locations + horizontal viewport half-pixel jitter on odd frames)
    StrategyB  // ProgrammableSampleLocations (VK_EXT_sample_locations 2x MSAA alternation per frame, stationary viewport)
};

enum class DebugViewMode : uint32_t {
    Normal = 0,
    CheckerboardMask = 1,
    DisocclusionHeatmap = 2,
    MotionVectors = 3,
    QuarterResRaw = 4,
    SpatialUpscaleBaseline = 5
};

struct CBRConfig {
    // General
    bool             enabled{ true };
    uint32_t         targetWidth{ 3840 };
    uint32_t         targetHeight{ 2160 };
    GraphicsApi      preferredApi{ GraphicsApi::Vulkan };
    SamplingStrategy samplingStrategy{ SamplingStrategy::StrategyA };
    // MIP LOD Bias: -1.0f mathematically compensates for the 2x larger texture coordinate
    // derivatives in quarter-resolution (half-width, half-height) 2x MSAA rendering
    // (CHECKERBOARD.md lines 365-425 / El Mansouri 2016: LOD' = -1 + LOD).
    float            mipLodBias{ -1.0f };

    // Reconstruction
    float           depthTolerance{ 0.010f };
    bool            enableColorClamping{ true };
    ColorSpace      colorSpace{ ColorSpace::YCoCg };
    float           historyWeight{ 0.90f };
    bool            enableSpatialFallback{ true };
    // 3x3 closest-depth motion-vector dilation over active samples.
    bool            enableMotionDilation{ true };
    DepthConvention depthConvention{ DepthConvention::Reversed };
    float           depthNear{ 0.1f };
    float           depthFar{ 0.0f }; // 0.0 = infinite far plane (Reversed only)
    // 5-tap RCAS contrast-adaptive sharpening strength (0.0 = disabled, 1.0 = max).
    float           sharpeningStrength{ 0.0f };
    // Camera-only depth unprojection fallback when sampled velocity is zeroed/unbound.
    bool            enableCameraMotionFallback{ false };

    // Jitter
    JitterPattern jitterPattern{ JitterPattern::Checkerboard };
    float         jitterScale{ 1.0f };
    int32_t       jitterDirection{ 1 }; // +1 or -1
    // Independent sign applied when writing jitter offsets into the projection matrix
    // (proj[8] / proj[9]). Accounts for engine projection-matrix handedness / column-major vs
    // row-major conventions independently of reconstruction sample parity (+1 or -1).
    int32_t       projectionJitterSign{ 1 };
    // Multiplier applied to the jitter delta when reprojecting history: default 0.0
    // (whole-pixel coverage jitter is absorbed by the sample mapping, so history needs no compensation).
    float         jitterCompensation{ 0.0f };

    // Debug
    bool        showOverlay{ false };
    uint32_t    debugView{ 0 }; // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw, 5=SpatialUpscaleBaseline
    bool        logToFile{ true };
    std::string logLevel{ "Info" };
};

class ConfigManager {
public:
    static ConfigManager& Get();

    bool Load(const std::filesystem::path& configPath);
    bool Save(const std::filesystem::path& configPath);

    [[nodiscard]] CBRConfig GetConfig() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_config;
    }
    // NOTE: GetMutableConfig() was removed: it returned CBRConfig& after the
    // lock_guard was destroyed, exposing an unprotected reference (data race).
    // Mutate via Modify() or UpdateConfig() which hold the lock for the write.
    void UpdateConfig(const CBRConfig& config) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = config;
    }
    template<typename Fn>
    void Modify(Fn&& fn) {
        CBRConfig cfg;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cfg = m_config;
        }
        fn(cfg);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_config = cfg;
        }
    }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    mutable std::mutex m_mutex;
    CBRConfig m_config;
};

} // namespace cbr
