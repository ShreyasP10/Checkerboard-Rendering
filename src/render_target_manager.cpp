#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

namespace cbr {

RenderTargetManager& RenderTargetManager::Get() {
    static RenderTargetManager instance;
    return instance;
}

void RenderTargetManager::Initialize(uint32_t width, uint32_t height) {
    m_dims.fullWidth = width;
    m_dims.fullHeight = height;
    m_dims.quarterWidth = width / 2;
    m_dims.quarterHeight = height / 2;
    m_dims.msaaSamples = 2;
    m_historyPingPong.store(0);

    // Calculate VRAM footprint:
    // 1. Quarter-Res 2x MSAA Color (RGBA16F = 8 bytes/sample * 2 samples):
    size_t qColor = static_cast<size_t>(m_dims.quarterWidth) * m_dims.quarterHeight * 8 * 2;
    // 2. Quarter-Res 2x MSAA Depth (D32F = 4 bytes/sample * 2 samples):
    size_t qDepth = static_cast<size_t>(m_dims.quarterWidth) * m_dims.quarterHeight * 4 * 2;
    // 3. Full-Res History A & B (RGBA16F = 8 bytes):
    size_t histColor = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 8 * 2;
    // 4. Full-Res Depth History A & B (R32F = 4 bytes * 2 buffers):
    size_t histDepth = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 4 * 2;
    // 5. Full-Res Output Image (RGBA16F = 8 bytes):
    size_t outColor = static_cast<size_t>(m_dims.fullWidth) * m_dims.fullHeight * 8;

    m_totalAllocatedVramBytes = qColor + qDepth + histColor + histDepth + outColor;
    m_initialized = true;

    CBR_LOG_INFO("RenderTargetManager initialized for target: %ux%u", width, height);
    CBR_LOG_INFO("Quarter-Resolution 2x MSAA Buffer size: %ux%u", m_dims.quarterWidth, m_dims.quarterHeight);
    CBR_LOG_INFO("Total CBR VRAM Footprint: %.2f MB", static_cast<double>(m_totalAllocatedVramBytes) / (1024.0 * 1024.0));
}

void RenderTargetManager::Shutdown() {
    m_initialized = false;
    m_totalAllocatedVramBytes = 0;
    CBR_LOG_INFO("RenderTargetManager shut down.");
}

bool RenderTargetManager::IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t /*format*/) const {
    if (!m_initialized) return false;

    // Matches if the target resolution is identical or close to full output resolution
    bool matchesWidth = (width == m_dims.fullWidth);
    bool matchesHeight = (height == m_dims.fullHeight);

    return matchesWidth && matchesHeight;
}

bool RenderTargetManager::IsQuarterPassCandidate(uint32_t width, uint32_t height) const {
    if (!m_initialized) return false;
    return (width == m_dims.quarterWidth && height == m_dims.quarterHeight);
}

} // namespace cbr
