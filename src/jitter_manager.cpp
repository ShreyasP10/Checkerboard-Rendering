#include "cbr/jitter_manager.h"
#include "cbr/config.h"
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

    // 2-phase checkerboard jitter sequence:
    // Even frames are unjittered (samples land on pixel centres).
    // Odd frames are shifted horizontally by exactly one full-resolution pixel (shiftDirection * pixelWidth),
    // giving 100% 4-quadrant geometric coverage across two frames with standard 2x MSAA sample locations.
    const auto& config = ConfigManager::Get().GetConfig();
    const float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;

    if (frameIndex & 1u) {
        m_currentJitter.x = dir * pixelWidth;
        m_currentJitter.y = 0.0f;
    } else {
        m_currentJitter.x = 0.0f;
        m_currentJitter.y = 0.0f;
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

void JitterManager::SetProjectionJitter(float* outMatrix4x4, const float* inUnjitteredMatrix4x4, bool isVulkan) const {
    if (!outMatrix4x4 || !inUnjitteredMatrix4x4) return;

    if (outMatrix4x4 != inUnjitteredMatrix4x4) {
        for (int i = 0; i < 16; ++i) {
            outMatrix4x4[i] = inUnjitteredMatrix4x4[i];
        }
    }

    float jitterNdcX = 2.0f * m_currentJitter.x;
    float jitterNdcY = 2.0f * m_currentJitter.y;

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    outMatrix4x4[8] = inUnjitteredMatrix4x4[8] + jitterNdcX;
    outMatrix4x4[9] = inUnjitteredMatrix4x4[9] + jitterNdcY;
}

} // namespace cbr
