#pragma once

#include <cstdint>
#include <array>
#include <mutex>
#include <utility>

namespace cbr {

struct JitterOffset {
    float x{ 0.0f };
    float y{ 0.0f };
};

class JitterManager {
public:
    static JitterManager& Get();

    void Initialize(uint32_t targetWidth, uint32_t targetHeight);
    void Update(uint32_t frameIndex);

    [[nodiscard]] JitterOffset GetCurrentJitter() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_currentJitter;
    }
    [[nodiscard]] JitterOffset GetPreviousJitter() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_previousJitter;
    }
    [[nodiscard]] JitterOffset GetJitterDelta() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return { m_currentJitter.x - m_previousJitter.x, m_currentJitter.y - m_previousJitter.y };
    }

    // Returns viewport horizontal offset in quarter-resolution pixels (0.5 px shift on odd frames)
    // per CHECKERBOARD.md (Viewport Half-Pixel Offset).
    [[nodiscard]] float GetQuarterViewportShiftX(uint32_t frameIndex) const;

    // Computes the NDC projection offset (delta_x, delta_y) applied to proj[8] and proj[9].
    // Incorporates ConfigManager::Get().GetConfig().projectionJitterSign and Vulkan Y-flip.
    [[nodiscard]] std::pair<float, float> ComputeProjectionOffset(const JitterOffset& jitter, bool isVulkan) const;

    // Computes subpixel jitter offset for a 4x4 projection matrix
    void ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const;
    void RemoveJitterFromProjection(float* projMatrix4x4, bool isVulkan) const;

    // Idempotent: sets jitter on outMatrix4x4 relative to inUnjitteredMatrix4x4 without accumulating.
    // IMPORTANT: inUnjitteredMatrix4x4 must really be the unjittered source. Passing the same pointer
    // for both and calling twice re-applies the offset (use a separate output buffer).
    void SetProjectionJitter(float* outMatrix4x4, const float* inUnjitteredMatrix4x4, bool isVulkan) const;

private:
    JitterManager() = default;
    ~JitterManager() = default;

    // LOCK HIERARCHY: ConfigManager mutex must NEVER be acquired while holding JitterManager::m_mutex.
    // Methods that query ConfigManager (such as Update or ComputeProjectionOffset) query configuration
    // outside of m_mutex locking scopes to prevent inversion deadlocks.
    mutable std::mutex m_mutex;
    uint32_t     m_targetWidth{ 3840 };
    uint32_t     m_targetHeight{ 2160 };
    JitterOffset m_currentJitter;
    JitterOffset m_previousJitter;
    std::pair<float, float> m_cachedProjectionOffsetVk[2]{};
    std::pair<float, float> m_cachedProjectionOffsetDx[2]{};
    mutable float           m_lastProj0{ 0.0f };
    mutable std::pair<float, float> m_lastAppliedOffset{ 0.0f, 0.0f };
};

} // namespace cbr
