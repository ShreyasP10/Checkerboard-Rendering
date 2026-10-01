#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <mutex>
#include "cbr/config.h"

namespace cbr {

class CBREngine {
public:
    static CBREngine& Get();

    bool Initialize();
    void Shutdown(bool isProcessExit = false);

    void SetModuleDirectory(const std::filesystem::path& dir) { m_moduleDirectory = dir; }
    const std::filesystem::path& GetModuleDirectory() const { return m_moduleDirectory; }

    // Frame lifecycle callbacks
    void OnBeginFrame();
    void OnPreRender();
    void OnPostRender();
    void OnPrePresent(void* queueOrContext, const void* presentInfo);
    void OnPostPresent();

    uint32_t    GetCurrentFrameIndex() const { return m_frameIndex.load(); }
    bool        IsEnabled() const { return m_enabled.load(); }
    void        SetEnabled(bool enabled) { m_enabled.store(enabled); }
    GraphicsApi GetActiveApi() const { return m_activeApi.load(); }
    void        SetActiveApi(GraphicsApi api) { m_activeApi.store(api); }

    // Performance metrics
    float GetLastReconstructionDurationMs() const { return m_lastReconDurationMs.load(); }

private:
    CBREngine() = default;
    ~CBREngine() = default;

    std::once_flag             m_initOnce;
    std::atomic<bool>          m_initialized{ false };
    std::atomic<bool>          m_enabled{ true };
    std::atomic<uint32_t>      m_frameIndex{ 0 };
    std::atomic<float>         m_lastReconDurationMs{ 0.0f };
    std::atomic<GraphicsApi>   m_activeApi{ GraphicsApi::Vulkan };
    std::filesystem::path      m_moduleDirectory;
};

} // namespace cbr
