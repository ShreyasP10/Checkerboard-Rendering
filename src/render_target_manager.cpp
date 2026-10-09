#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

namespace cbr {

RenderTargetManager& RenderTargetManager::Get() {
    static RenderTargetManager instance;
    return instance;
}

void RenderTargetManager::Initialize(uint32_t width, uint32_t height) {
    if (width < 2 || height < 2 || width > 16384 || height > 16384) {
        CBR_LOG_ERROR("RenderTargetManager::Initialize rejected invalid %ux%u (must be 2..16384).", width, height);
        return;
    }
    TargetDimensions dims{};
    dims.fullWidth = width;
    dims.fullHeight = height;
    dims.quarterWidth = width / 2;
    dims.quarterHeight = height / 2;
    dims.msaaSamples = 2;
    m_historyPingPong.store(0);

    // Calculate VRAM footprint:
    // 1. Quarter-Res 2x MSAA Color (RGBA16F = 8 bytes/sample * 2 samples):
    size_t qColor = static_cast<size_t>(dims.quarterWidth) * dims.quarterHeight * 8 * 2;
    // 2. Quarter-Res 2x MSAA Depth (D32F = 4 bytes/sample * 2 samples):
    size_t qDepth = static_cast<size_t>(dims.quarterWidth) * dims.quarterHeight * 4 * 2;
    // 3. Full-Res History A & B (RGBA16F = 8 bytes):
    size_t histColor = static_cast<size_t>(dims.fullWidth) * dims.fullHeight * 8 * 2;
    // 4. Full-Res Depth History A & B (R32F = 4 bytes * 2 buffers):
    size_t histDepth = static_cast<size_t>(dims.fullWidth) * dims.fullHeight * 4 * 2;
    // 5. Full-Res Output Image (RGBA16F = 8 bytes):
    size_t outColor = static_cast<size_t>(dims.fullWidth) * dims.fullHeight * 8;

    const size_t total = qColor + qDepth + histColor + histDepth + outColor;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_dims = dims;
        m_totalAllocatedVramBytes.store(total);
    }
    m_initialized = true;

    CBR_LOG_INFO("RenderTargetManager initialized for target: %ux%u", width, height);
    CBR_LOG_INFO("Quarter-Resolution 2x MSAA Buffer size: %ux%u", dims.quarterWidth, dims.quarterHeight);
    CBR_LOG_INFO("Total CBR VRAM Footprint: %.2f MiB (%.2f MB)",
        static_cast<double>(total) / (1024.0 * 1024.0),
        static_cast<double>(total) / 1000000.0);
}

void RenderTargetManager::Shutdown() {
    m_initialized = false;
    m_totalAllocatedVramBytes.store(0);
    CBR_LOG_INFO("RenderTargetManager shut down.");
}

bool RenderTargetManager::IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t format) const {
    if (!m_initialized.load()) return false;

    // NOTE: format is currently ignored (all full-res targets match). Revisit when
    // depth vs color formats must be distinguished to avoid false positives.
    (void)format;
    const TargetDimensions dims = GetDimensions();
    // Matches if the target resolution is identical or close to full output resolution
    bool matchesWidth = (width == dims.fullWidth);
    bool matchesHeight = (height == dims.fullHeight);

    return matchesWidth && matchesHeight;
}

bool RenderTargetManager::IsQuarterPassCandidate(uint32_t width, uint32_t height) const {
    if (!m_initialized.load()) return false;
    const TargetDimensions dims = GetDimensions();
    return (width == dims.quarterWidth && height == dims.quarterHeight);
}

} // namespace cbr
