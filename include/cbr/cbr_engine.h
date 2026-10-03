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

    // One-time setup (config, logging, buffers, overlay). Does NOT depend on the graphics runtime being loaded.
    bool Initialize();
    // Attempts to install the graphics hooks for the active API. Safe to call repeatedly (e.g. from a
    // retry loop while the game loads its graphics runtime); returns true once hooks are installed.
    bool TryInstallHooks();
    void Shutdown(bool isProcessExit = false);

    void SetModuleDirectory(const std::filesystem::path& dir) { m_moduleDirectory = dir; }
    const std::filesystem::path& GetModuleDirectory() const { return m_moduleDirectory; }

    // Frame lifecycle callbacks
    void OnBeginFrame();
    void OnPreRender();
    void OnPostRender();
    // Mid-frame pass interception: called when the main geometry pass completes, before post-processing / UI.
    // Runs the reconstruction AT MOST ONCE per frame: extra matching passes in the same frame
    // (reflections, mirrors, cubemaps) are ignored, so history ping-pong cannot desynchronize.
    void OnScenePassEnd(void* cmdBufferOrContext);
    void OnPrePresent(void* queueOrContext, const void* presentInfo);
    void OnPostPresent(void* presentTarget);
    // Call when the game (re)creates its swapchain: resets frame parity, history, and updates dimensions if provided
    void OnSwapchainRecreated(uint32_t width = 0, uint32_t height = 0);

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
    std::mutex                 m_hookMutex; // serializes TryInstallHooks (init thread vs CBR_PluginInit)
    std::atomic<bool>          m_initialized{ false };
    std::atomic<bool>          m_enabled{ true };
    std::atomic<uint32_t>      m_frameIndex{ 0 };
    // The present target (VkQueue / IDXGISwapChain) of the game's main output. Presents from any
    // other target (overlays, loading screens, secondary windows) must not advance checkerboard parity.
    std::atomic<void*>         m_mainPresentTarget{ nullptr };
    // Frame index for which the reconstruction last ran (kNoFrame = none yet)
    static constexpr uint32_t  kNoFrame = 0xFFFFFFFFu;
    std::atomic<uint32_t>      m_lastDispatchedFrame{ kNoFrame };
    // PreferredApi = Auto and no runtime was loaded yet: re-detect on each hook attempt
    std::atomic<bool>          m_apiPending{ false };
    std::atomic<float>         m_lastReconDurationMs{ 0.0f };
    std::atomic<GraphicsApi>   m_activeApi{ GraphicsApi::Vulkan };
    std::filesystem::path      m_moduleDirectory;
};

} // namespace cbr
