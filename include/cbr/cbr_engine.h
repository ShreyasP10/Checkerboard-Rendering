#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include "cbr/config.h"

namespace cbr {

class CBREngine {
public:
    static CBREngine& Get();

    bool Initialize();
    void Shutdown();

    // Frame lifecycle callbacks
    void OnBeginFrame();
    void OnPreRender();
    void OnPostRender();
    void OnPrePresent(void* queueOrContext, const void* presentInfo);
    void OnPostPresent();

    uint32_t GetCurrentFrameIndex() const { return m_frameIndex.load(); }
    bool     IsEnabled() const { return m_enabled.load(); }
    void     SetEnabled(bool enabled) { m_enabled.store(enabled); }

    // Performance metrics
    float GetLastReconstructionDurationMs() const { return m_lastReconDurationMs; }

private:
    CBREngine() = default;
    ~CBREngine() = default;

    std::atomic<bool>     m_initialized{ false };
    std::atomic<bool>     m_enabled{ true };
    std::atomic<uint32_t> m_frameIndex{ 0 };
    float                 m_lastReconDurationMs{ 0.0f };
};

} // namespace cbr
