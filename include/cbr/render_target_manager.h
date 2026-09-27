#pragma once

#include <cstdint>
#include <vector>

namespace cbr {

struct TargetDimensions {
    uint32_t fullWidth{ 3840 };
    uint32_t fullHeight{ 2160 };
    uint32_t quarterWidth{ 1920 };
    uint32_t quarterHeight{ 1080 };
    uint32_t msaaSamples{ 2 };
};

class RenderTargetManager {
public:
    static RenderTargetManager& Get();

    void Initialize(uint32_t width, uint32_t height);
    void Shutdown();

    const TargetDimensions& GetDimensions() const { return m_dims; }

    bool IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t format) const;

    // Ping-pong history buffer index management
    uint32_t GetCurrentHistoryIndex() const { return m_historyPingPong; }
    uint32_t GetPreviousHistoryIndex() const { return 1 - m_historyPingPong; }
    void     SwapHistoryBuffers() { m_historyPingPong = 1 - m_historyPingPong; }

    // Memory footprint tracking
    size_t GetTotalAllocatedVramBytes() const { return m_totalAllocatedVramBytes; }

private:
    RenderTargetManager() = default;
    ~RenderTargetManager() = default;

    TargetDimensions m_dims;
    uint32_t         m_historyPingPong{ 0 };
    size_t           m_totalAllocatedVramBytes{ 0 };
    bool             m_initialized{ false };
};

} // namespace cbr
