#include "cbr/jitter_manager.h"
#include <cmath>
#include "cbr/config.h"
#include "cbr/logger.h"
#include "cbr/render_target_manager.h"

// LOCK HIERARCHY:
//   Level 1: ConfigManager::m_mutex
//   Level 2: JitterManager::m_mutex
//   Level 3: Logger::m_mutex
// ConfigManager mutex must NEVER be acquired while holding JitterManager::m_mutex.
// All configuration lookups occur strictly outside of JitterManager::m_mutex lock scopes.

namespace cbr {

JitterManager& JitterManager::Get() {
    static JitterManager instance;
    return instance;
}

void JitterManager::Initialize(uint32_t targetWidth, uint32_t targetHeight) {
    const auto config = ConfigManager::Get().GetConfig();
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;
    uint32_t w, h;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_targetWidth = (targetWidth > 0) ? targetWidth : 3840;
        m_targetHeight = (targetHeight > 0) ? targetHeight : 2160;
        m_currentJitter = { 0.0f, 0.0f };
        m_previousJitter = { 0.0f, 0.0f };
        m_lastProj0 = 0.0f;
        m_lastAppliedOffset = { 0.0f, 0.0f };
        w = m_targetWidth; h = m_targetHeight;

        const float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
        const JitterOffset phase1Jitter{ dir * pixelWidth, 0.0f };
        m_cachedProjectionOffsetVk[0] = { 0.0f, 0.0f };
        m_cachedProjectionOffsetDx[0] = { 0.0f, 0.0f };
        m_cachedProjectionOffsetVk[1] = ComputeProjectionOffset(phase1Jitter, true);
        m_cachedProjectionOffsetDx[1] = ComputeProjectionOffset(phase1Jitter, false);
    }
    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", w, h);
}

void JitterManager::Update(uint32_t frameIndex) {
    // JitterScale is intentionally ignored: 2x MSAA checkerboard coverage
    // requires exactly one full-resolution pixel shift. See config warning.
    const auto config = ConfigManager::Get().GetConfig();
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_previousJitter = m_currentJitter;

    // 2-phase checkerboard jitter sequence:
    // Even frames are unjittered (samples land on pixel centres).
    // Odd frames are shifted horizontally by exactly one full-resolution pixel (shiftDirection * pixelWidth),
    // giving 100% 4-quadrant geometric coverage across two frames with standard 2x MSAA sample locations.
    const float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
    const JitterOffset phase1Jitter{ dir * pixelWidth, 0.0f };

    m_cachedProjectionOffsetVk[0] = { 0.0f, 0.0f };
    m_cachedProjectionOffsetDx[0] = { 0.0f, 0.0f };
    m_cachedProjectionOffsetVk[1] = ComputeProjectionOffset(phase1Jitter, true);
    m_cachedProjectionOffsetDx[1] = ComputeProjectionOffset(phase1Jitter, false);

    if (frameIndex & 1u) {
        m_currentJitter.x = dir * pixelWidth;
        m_currentJitter.y = 0.0f;
    } else {
        m_currentJitter.x = 0.0f;
        m_currentJitter.y = 0.0f;
    }
}

float JitterManager::GetQuarterViewportShiftX(uint32_t frameIndex) const {
    const auto& config = ConfigManager::Get().GetConfig();
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;
    if (!(frameIndex & 1u)) return 0.0f;
    const float shift = 0.5f * dir;
    return std::round(shift * 16.0f) / 16.0f;
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

    // Detect rapid FOV changes (e.g. scoping / sniper zoom > 5 degrees per frame)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastProj0 > 0.0f && projMatrix4x4[0] > 0.0f) {
            const float fovDeltaRatio = std::abs(projMatrix4x4[0] - m_lastProj0) / m_lastProj0;
            if (fovDeltaRatio > 0.08f) {
                RenderTargetManager::Get().ResetHistory();
            }
        }
        m_lastProj0 = projMatrix4x4[0];
    }

    std::pair<float, float> offset;
    std::pair<float, float> lastOffset;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint32_t phase = (m_currentJitter.x != 0.0f) ? 1u : 0u;
        offset = isVulkan ? m_cachedProjectionOffsetVk[phase] : m_cachedProjectionOffsetDx[phase];
        lastOffset = m_lastAppliedOffset;
        m_lastAppliedOffset = offset;
    }

    // Subtract previously applied offset to prevent cumulative drift on persistent engine matrices (Item H-01)
    projMatrix4x4[8] -= lastOffset.first;
    projMatrix4x4[9] -= lastOffset.second;

    projMatrix4x4[8] += offset.first;
    projMatrix4x4[9] += offset.second;
}

void JitterManager::RemoveJitterFromProjection(float* projMatrix4x4, bool /*isVulkan*/) const {
    if (!projMatrix4x4) return;

    std::pair<float, float> lastOffset;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        lastOffset = m_lastAppliedOffset;
        m_lastAppliedOffset = { 0.0f, 0.0f };
    }

    projMatrix4x4[8] -= lastOffset.first;
    projMatrix4x4[9] -= lastOffset.second;
}

void JitterManager::SetProjectionJitter(float* outMatrix4x4, const float* inUnjitteredMatrix4x4, bool isVulkan) const {
    if (!outMatrix4x4 || !inUnjitteredMatrix4x4) return;

    if (outMatrix4x4 != inUnjitteredMatrix4x4) {
        for (int i = 0; i < 16; ++i) {
            outMatrix4x4[i] = inUnjitteredMatrix4x4[i];
        }
    }

    std::pair<float, float> offset;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint32_t phase = (m_currentJitter.x != 0.0f) ? 1u : 0u;
        offset = isVulkan ? m_cachedProjectionOffsetVk[phase] : m_cachedProjectionOffsetDx[phase];
    }

    outMatrix4x4[8] = inUnjitteredMatrix4x4[8] + offset.first;
    outMatrix4x4[9] = inUnjitteredMatrix4x4[9] + offset.second;
}

} // namespace cbr
