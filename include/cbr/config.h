#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <mutex>

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
    RGB
};

struct CBRConfig {
    // General
    bool        enabled{ true };
    uint32_t    targetWidth{ 3840 };
    uint32_t    targetHeight{ 2160 };
    GraphicsApi preferredApi{ GraphicsApi::Vulkan };
    float       mipLodBias{ -0.5f };

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

    CBRConfig GetConfig() const {
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
        std::lock_guard<std::mutex> lock(m_mutex);
        fn(m_config);
    }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    mutable std::mutex m_mutex;
    CBRConfig m_config;
};

} // namespace cbr
