#include "cbr/jitter_manager.h"
#include "cbr/config.h"
#include "cbr/logger.h"

namespace cbr {

JitterManager& JitterManager::Get() {
    static JitterManager instance;
    return instance;
}

void JitterManager::Initialize(uint32_t targetWidth, uint32_t targetHeight) {
    uint32_t w, h;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_targetWidth = (targetWidth > 0) ? targetWidth : 3840;
        m_targetHeight = (targetHeight > 0) ? targetHeight : 2160;
        m_currentJitter = { 0.0f, 0.0f };
        m_previousJitter = { 0.0f, 0.0f };
        w = m_targetWidth; h = m_targetHeight;
    }
    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", w, h);
}

void JitterManager::Update(uint32_t frameIndex) {
    // JitterScale is intentionally ignored: 2x MSAA checkerboard coverage
    // requires exactly one full-resolution pixel shift. See config warning.
    std::lock_guard<std::mutex> lock(m_mutex);
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

std::pair<float, float> JitterManager::ComputeProjectionOffset(const JitterOffset& jitter, bool isVulkan) const {
    const auto& config = ConfigManager::Get().GetConfig();
    const float sign = (config.projectionJitterSign < 0) ? -1.0f : 1.0f;

    // Projection matrix offset in NDC space
    float jitterNdcX = sign * (2.0f * jitter.x);
    float jitterNdcY = sign * (2.0f * jitter.y);

    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }

    return { jitterNdcX, jitterNdcY };
}

void JitterManager::ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4) return;

    JitterOffset cur;
    { std::lock_guard<std::mutex> lock(m_mutex); cur = m_currentJitter; }
    auto [jitterNdcX, jitterNdcY] = ComputeProjectionOffset(cur, isVulkan);

    projMatrix4x4[8] += jitterNdcX;
    projMatrix4x4[9] += jitterNdcY;
}

void JitterManager::RemoveJitterFromProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4) return;

    JitterOffset cur;
    { std::lock_guard<std::mutex> lock(m_mutex); cur = m_currentJitter; }
    auto [jitterNdcX, jitterNdcY] = ComputeProjectionOffset(cur, isVulkan);

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

    JitterOffset cur;
    { std::lock_guard<std::mutex> lock(m_mutex); cur = m_currentJitter; }
    auto [jitterNdcX, jitterNdcY] = ComputeProjectionOffset(cur, isVulkan);

    outMatrix4x4[8] = inUnjitteredMatrix4x4[8] + jitterNdcX;
    outMatrix4x4[9] = inUnjitteredMatrix4x4[9] + jitterNdcY;
}

} // namespace cbr
