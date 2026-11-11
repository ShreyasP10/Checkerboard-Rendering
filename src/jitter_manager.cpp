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

namespace {

std::pair<float, float> ComputeOffsetWithSign(const JitterOffset& jitter, bool isVulkan, int32_t projectionJitterSign) {
    const float sign = (projectionJitterSign < 0) ? -1.0f : 1.0f;
    const float jitterNdcX = sign * (2.0f * jitter.x);
    float jitterNdcY = sign * (2.0f * jitter.y);
    if (isVulkan) {
        jitterNdcY = -jitterNdcY;
    }
    return { jitterNdcX, jitterNdcY };
}

} // namespace

JitterManager& JitterManager::Get() {
    static JitterManager instance;
    return instance;
}

void JitterManager::Initialize(uint32_t targetWidth, uint32_t targetHeight) {
    const auto config = ConfigManager::Get().GetConfig();
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;
    const uint32_t validW = (targetWidth > 0) ? targetWidth : 3840;
    const uint32_t validH = (targetHeight > 0) ? targetHeight : 2160;
    const float pixelWidth = 1.0f / static_cast<float>(validW);
    const JitterOffset phase1Jitter{ dir * pixelWidth, 0.0f };
    const auto vkOffset = ComputeOffsetWithSign(phase1Jitter, true, config.projectionJitterSign);
    const auto dxOffset = ComputeOffsetWithSign(phase1Jitter, false, config.projectionJitterSign);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_targetWidth = validW;
        m_targetHeight = validH;
        m_currentJitter = { 0.0f, 0.0f };
        m_previousJitter = { 0.0f, 0.0f };
        m_lastUpdatedFrame = 0;
        m_hasUpdatedFrame = false;
        m_lastProj0 = 0.0f;
        m_trackedProjMatrix = nullptr;
        m_baseProjOffset = { 0.0f, 0.0f };
        m_expectedJitteredProj = { 0.0f, 0.0f };
        m_lastAppliedOffset = { 0.0f, 0.0f };
        m_lastAppliedFrame = 0;
        m_hasAppliedToTracked = false;
        m_hasAppliedFrame = false;

        m_cachedProjectionOffsetVk[0] = { 0.0f, 0.0f };
        m_cachedProjectionOffsetDx[0] = { 0.0f, 0.0f };
        m_cachedProjectionOffsetVk[1] = vkOffset;
        m_cachedProjectionOffsetDx[1] = dxOffset;
    }
    CBR_LOG_INFO("JitterManager initialized with target resolution: %ux%u", validW, validH);
}

void JitterManager::Update(uint32_t frameIndex) {
    // JitterScale is intentionally ignored: 2x MSAA checkerboard coverage
    // requires exactly one full-resolution pixel shift. See config warning.
    const auto config = ConfigManager::Get().GetConfig();
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasUpdatedFrame || m_lastUpdatedFrame != frameIndex) {
        m_previousJitter = m_currentJitter;
        m_lastUpdatedFrame = frameIndex;
        m_hasUpdatedFrame = true;
        m_hasAppliedFrame = false;
    }

    // 2-phase checkerboard jitter sequence:
    // Even frames are unjittered (samples land on pixel centres).
    // Odd frames are shifted horizontally by exactly one full-resolution pixel (shiftDirection * pixelWidth),
    // giving 100% 4-quadrant geometric coverage across two frames with standard 2x MSAA sample locations.
    const float pixelWidth = 1.0f / static_cast<float>(m_targetWidth);
    const JitterOffset phase1Jitter{ dir * pixelWidth, 0.0f };

    m_cachedProjectionOffsetVk[0] = { 0.0f, 0.0f };
    m_cachedProjectionOffsetDx[0] = { 0.0f, 0.0f };
    m_cachedProjectionOffsetVk[1] = ComputeOffsetWithSign(phase1Jitter, true, config.projectionJitterSign);
    m_cachedProjectionOffsetDx[1] = ComputeOffsetWithSign(phase1Jitter, false, config.projectionJitterSign);

    if (frameIndex & 1u) {
        m_currentJitter.x = dir * pixelWidth;
        m_currentJitter.y = 0.0f;
    } else {
        m_currentJitter.x = 0.0f;
        m_currentJitter.y = 0.0f;
    }
}

float JitterManager::GetQuarterViewportShiftX(uint32_t frameIndex) const {
    const auto config = ConfigManager::Get().GetConfig();
    const float dir = (config.jitterDirection < 0) ? -1.0f : 1.0f;
    if (!(frameIndex & 1u)) return 0.0f;
    const float shift = 0.5f * dir;
    return std::round(shift * 16.0f) / 16.0f;
}

std::pair<float, float> JitterManager::ComputeProjectionOffset(const JitterOffset& jitter, bool isVulkan) const {
    const auto config = ConfigManager::Get().GetConfig();
    return ComputeOffsetWithSign(jitter, isVulkan, config.projectionJitterSign);
}

void JitterManager::ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const {
    if (!projMatrix4x4 || !std::isfinite(projMatrix4x4[8]) || !std::isfinite(projMatrix4x4[9])) return;

    const float cur0 = projMatrix4x4[0];
    const float cur8 = projMatrix4x4[8];
    const float cur9 = projMatrix4x4[9];
    bool resetHistory = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Detect rapid FOV changes (e.g. scoping / sniper zoom > 5 degrees per frame)
        if (m_lastProj0 > 0.0f && cur0 > 0.0f) {
            const float fovDeltaRatio = std::abs(cur0 - m_lastProj0) / m_lastProj0;
            if (fovDeltaRatio > 0.08f) {
                resetHistory = true;
            }
        }
        m_lastProj0 = cur0;

        const uint32_t phase = (m_currentJitter.x != 0.0f) ? 1u : 0u;
        const auto offset = isVulkan ? m_cachedProjectionOffsetVk[phase] : m_cachedProjectionOffsetDx[phase];

        const bool sameTrackedPtr = m_hasAppliedToTracked && (m_trackedProjMatrix == projMatrix4x4);
        const bool stillHasExpectedJitter = sameTrackedPtr &&
            (std::fabs(cur8 - m_expectedJitteredProj.first) <= 1e-6f) &&
            (std::fabs(cur9 - m_expectedJitteredProj.second) <= 1e-6f);

        float baseX = cur8;
        float baseY = cur9;
        bool alreadyApplied = false;
        if (stillHasExpectedJitter) {
            // Same persistent matrix still holds our previous jitter: reuse its cached unjittered base.
            baseX = m_baseProjOffset.first;
            baseY = m_baseProjOffset.second;

            const bool sameFrame = !m_hasUpdatedFrame || (m_hasAppliedFrame && m_lastAppliedFrame == m_lastUpdatedFrame);
            const bool sameOffset =
                (std::fabs(m_lastAppliedOffset.first - offset.first) <= 1e-7f) &&
                (std::fabs(m_lastAppliedOffset.second - offset.second) <= 1e-7f);
            alreadyApplied = (sameFrame && sameOffset);
        } else {
            // Either a new matrix pointer or the engine rebuilt fresh unjittered base values.
            m_trackedProjMatrix = projMatrix4x4;
            m_baseProjOffset = { baseX, baseY };
            m_hasAppliedToTracked = true;
        }

        if (!alreadyApplied) {
            const float out8 = baseX + offset.first;
            const float out9 = baseY + offset.second;
            projMatrix4x4[8] = out8;
            projMatrix4x4[9] = out9;

            m_expectedJitteredProj = { out8, out9 };
            m_lastAppliedOffset = offset;
            m_lastAppliedFrame = m_lastUpdatedFrame;
            m_hasAppliedFrame = true;
        }
    }

    if (resetHistory) {
        RenderTargetManager::Get().ResetHistory();
    }
}

void JitterManager::RemoveJitterFromProjection(float* projMatrix4x4, bool /*isVulkan*/) const {
    if (!projMatrix4x4) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hasAppliedToTracked && m_trackedProjMatrix == projMatrix4x4) {
        if ((std::fabs(projMatrix4x4[8] - m_expectedJitteredProj.first) <= 1e-6f) &&
            (std::fabs(projMatrix4x4[9] - m_expectedJitteredProj.second) <= 1e-6f)) {
            projMatrix4x4[8] = m_baseProjOffset.first;
            projMatrix4x4[9] = m_baseProjOffset.second;
        } else {
            projMatrix4x4[8] -= m_lastAppliedOffset.first;
            projMatrix4x4[9] -= m_lastAppliedOffset.second;
        }
        m_trackedProjMatrix = nullptr;
        m_hasAppliedToTracked = false;
        m_hasAppliedFrame = false;
        m_baseProjOffset = { 0.0f, 0.0f };
        m_expectedJitteredProj = { 0.0f, 0.0f };
        m_lastAppliedOffset = { 0.0f, 0.0f };
        return;
    }

    projMatrix4x4[8] -= m_lastAppliedOffset.first;
    projMatrix4x4[9] -= m_lastAppliedOffset.second;
    m_lastAppliedOffset = { 0.0f, 0.0f };
}

bool JitterManager::IsProjectionJitterApplied(const float* projMatrix4x4, uint32_t frameIndex, bool isVulkan) const {
    if (!projMatrix4x4) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasAppliedToTracked || !m_hasAppliedFrame || m_trackedProjMatrix != projMatrix4x4) {
        return false;
    }
    if (m_hasUpdatedFrame && (m_lastUpdatedFrame != frameIndex || m_lastAppliedFrame != frameIndex)) {
        return false;
    }
    const uint32_t phase = (m_currentJitter.x != 0.0f) ? 1u : 0u;
    const auto expectedOffset = isVulkan ? m_cachedProjectionOffsetVk[phase] : m_cachedProjectionOffsetDx[phase];
    if ((std::fabs(m_lastAppliedOffset.first - expectedOffset.first) > 1e-7f) ||
        (std::fabs(m_lastAppliedOffset.second - expectedOffset.second) > 1e-7f)) {
        return false;
    }
    return (std::fabs(projMatrix4x4[8] - m_expectedJitteredProj.first) <= 1e-6f) &&
           (std::fabs(projMatrix4x4[9] - m_expectedJitteredProj.second) <= 1e-6f);
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
