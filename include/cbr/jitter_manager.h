#pragma once

#include <cstdint>
#include <array>

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

    JitterOffset GetCurrentJitter() const { return m_currentJitter; }
    JitterOffset GetPreviousJitter() const { return m_previousJitter; }
    JitterOffset GetJitterDelta() const {
        return { m_currentJitter.x - m_previousJitter.x, m_currentJitter.y - m_previousJitter.y };
    }

    // Computes subpixel jitter offset for a 4x4 projection matrix
    void ApplyJitterToProjection(float* projMatrix4x4, bool isVulkan) const;
    void RemoveJitterFromProjection(float* projMatrix4x4, bool isVulkan) const;

    // Idempotent: sets jitter on outMatrix4x4 relative to inUnjitteredMatrix4x4 without accumulating
    void SetProjectionJitter(float* outMatrix4x4, const float* inUnjitteredMatrix4x4, bool isVulkan) const;

private:
    JitterManager() = default;
    ~JitterManager() = default;

    uint32_t     m_targetWidth{ 3840 };
    uint32_t     m_targetHeight{ 2160 };
    JitterOffset m_currentJitter;
    JitterOffset m_previousJitter;
};

} // namespace cbr
