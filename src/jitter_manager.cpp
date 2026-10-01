#include "cbr/jitter_manager.h"
#include "cbr/logger.h"

namespace cbr {

JitterManager& JitterManager::Get() {
    static JitterManager instance;
    return instance;
}

void JitterManager::Initialize(uint32_t targetWidth, uint32_t targetHeight) {
    m_targetWidth = (targetWidth > 0) ? targetWidth : 3840;
    m_targetHeight = (targetHeight > 0) ? targetHeight : 2160;
    m_currentJitter = { 0.0f, 0.0f };
    m_previousJitter = { 0.0f, 0.0f };

    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", m_targetWidth, m_targetHeight);
}

void JitterManager::Update(uint32_t frameIndex) {
    m_previousJitter = m_currentJitter;

    // 2-phase subpixel checkerboard jitter sequence
    // Shifts alternating frames by (+0.5px, +0.5px) and (-0.5px, -0.5px)
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

    // Projection matrix offset in NDC space
    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    projMatrix4x4[8] += jitterNdcX;
    projMatrix4x4[9] += jitterNdcY;
}

void JitterManager::RemoveJitterFromProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4) return;

    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    projMatrix4x4[8] -= jitterNdcX;
    projMatrix4x4[9] -= jitterNdcY;
}

} // namespace cbr
