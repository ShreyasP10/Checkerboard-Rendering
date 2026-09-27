#pragma once

#include <string>
#include <cstdint>

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

    // Jitter
    JitterPattern jitterPattern{ JitterPattern::Checkerboard };
    float         jitterScale{ 1.0f };

    // Debug
    bool        showOverlay{ false };
    uint32_t    debugView{ 0 }; // 0=Normal, 1=Mask, 2=Disocclusion, 3=Motion, 4=Raw
    bool        logToFile{ true };
    std::string logLevel{ "Info" };
};

class ConfigManager {
public:
    static ConfigManager& Get();

    bool Load(const std::string& configPath);
    bool Save(const std::string& configPath);

    const CBRConfig& GetConfig() const { return m_config; }
    CBRConfig& GetMutableConfig() { return m_config; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    CBRConfig m_config;
};

} // namespace cbr
