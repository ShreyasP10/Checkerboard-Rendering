#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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
    float       depthTolerance{ 0.010f };
    bool        enableColorClamping{ true };
    ColorSpace  colorSpace{ ColorSpace::YCoCg };
    float       historyWeight{ 0.90f };
    bool        enableSpatialFallback{ true };
    // 3x3 closest-depth motion-vector dilation. Costs 9 extra MSAA depth fetches per output pixel;
    // disable on bandwidth-limited GPUs if silhouette smearing is acceptable.
    bool        enableMotionDilation{ true };

    // Jitter
    JitterPattern jitterPattern{ JitterPattern::Checkerboard };
    float         jitterScale{ 1.0f };
    // Multiplier applied to the jitter delta when reprojecting history. 1 = subtract (jc - jp),
    // -1 = opposite sign convention, 0 = off. The correct sign depends on the engine's projection
    // convention and must be confirmed with DebugView on a static camera.
    float         jitterCompensation{ 1.0f };

    // Debug
    bool        showOverlay{ false };
    uint32_t    debugView{ 0 }; // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw
    bool        logToFile{ true };
    std::string logLevel{ "Info" };
};

class ConfigManager {
public:
    static ConfigManager& Get();

    bool Load(const std::filesystem::path& configPath);
    bool Save(const std::filesystem::path& configPath);

    const CBRConfig& GetConfig() const { return m_config; }
    CBRConfig& GetMutableConfig() { return m_config; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    CBRConfig m_config;
};

} // namespace cbr
