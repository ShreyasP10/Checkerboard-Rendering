#include "cbr/jitter_manager.h"
#include "cbr/logger.h"

namespace cbr {

JitterManager& JitterManager::Get() {
    static JitterManager instance;
    return instance;
}

void JitterManager::Initialize(uint32_t targetWidth, uint32_t targetHeight) {
    m_targetWidth = targetWidth;
    m_targetHeight = targetHeight;
    m_currentJitter = { 0.0f, 0.0f };
    m_previousJitter = { 0.0f, 0.0f };

    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", targetWidth, targetHeight);
}

void JitterManager::Update(uint32_t frameIndex) {
    m_previousJitter = m_currentJitter;

    // 2-phase subpixel checkerboard jitter sequence
    // Alternates between (+0.5px, +0.5px) and (-0.5px, -0.5px)
    float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
    float pixelHeight = 1.0f / static_cast<float>(m_targetHeight);

    if (frameIndex & 1u) {
        m_currentJitter.x = 0.5f * pixelWidth;
        m_currentJitter.y = 0.5f * pixelHeight;
    } else {
        m_currentJitter.x = -0.5f * pixelWidth;
        m_currentJitter.y = -0.5f * pixelHeight;
    }
}

void JitterManager::ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4) return;

    // In a standard column-major 4x4 projection matrix:
    // element [2][0] (index 8) is the X shear/offset
    // element [2][1] (index 9) is the Y shear/offset
    //
    // In row-major:
    // element [0][2] (index 2) is X offset
    // element [1][2] (index 6) is Y offset
    //
    // For standard graphics engines (column-major projection):
    // NDC.x = (X * P00) + (Z * P20)
    // We add 2 * Jitter.x to P20 so that in NDC space, the sample is shifted by (2 * Jitter.x) / 2 = Jitter.x

    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        // Vulkan Y is inverted in NDC compared to DirectX
        jitterNdcY = -jitterNdcY;
    }

    projMatrix4x4[8] += jitterNdcX;
    projMatrix4x4[9] += jitterNdcY;
}

} // namespace cbr
