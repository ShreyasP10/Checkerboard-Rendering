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

    JitterOffset GetCurrentJitter() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_currentJitter;
    }
    JitterOffset GetPreviousJitter() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_previousJitter;
    }
    JitterOffset GetJitterDelta() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return { m_currentJitter.x - m_previousJitter.x, m_currentJitter.y - m_previousJitter.y };
    }

    // Computes the NDC projection offset (delta_x, delta_y) applied to proj[8] and proj[9].
    // Incorporates ConfigManager::Get().GetConfig().projectionJitterSign and Vulkan Y-flip.
    std::pair<float, float> ComputeProjectionOffset(const JitterOffset& jitter, bool isVulkan) const;

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

    mutable std::mutex m_mutex;
    uint32_t     m_targetWidth{ 3840 };
    uint32_t     m_targetHeight{ 2160 };
    JitterOffset m_currentJitter;
    JitterOffset m_previousJitter;
};

} // namespace cbr
