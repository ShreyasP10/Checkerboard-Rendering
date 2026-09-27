#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/logger.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/ui_overlay.h"
#include "cbr/hooks.h"

namespace cbr {

CBREngine& CBREngine::Get() {
    static CBREngine instance;
    return instance;
}

bool CBREngine::Initialize() {
    if (m_initialized.load()) return true;

    // 1. Initialize Logger
    Logger::Get().Initialize("cbr.log");
    CBR_LOG_INFO("Initializing CBREngine for Red Dead Redemption 2...");

    // 2. Load Configuration
    ConfigManager::Get().Load("cbr.ini");
    const auto& config = ConfigManager::Get().GetConfig();
    m_enabled.store(config.enabled);

    // 3. Initialize Render Target & Jitter Managers
    RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
    JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

    // 4. Initialize Overlay
    UIOverlay::Get().Initialize();

    // 5. Install API Hooks
    HookManager::Get().Initialize();
    if (config.preferredApi == GraphicsApi::Vulkan) {
        HookManager::Get().InstallVulkanHooks();
    } else {
        HookManager::Get().InstallDX12Hooks();
    }

    m_initialized.store(true);
    CBR_LOG_INFO("CBREngine initialized successfully. Ready for frame interception.");
    return true;
}

void CBREngine::Shutdown() {
    if (!m_initialized.load()) return;

    CBR_LOG_INFO("Shutting down CBREngine...");
    HookManager::Get().Shutdown();
    ReconstructionPass::Get().Shutdown();
    UIOverlay::Get().Shutdown();
    RenderTargetManager::Get().Shutdown();
    Logger::Get().Shutdown();

    m_initialized.store(false);
}

void CBREngine::OnBeginFrame() {
    if (!m_enabled.load()) return;

    uint32_t currentFrame = m_frameIndex.load();
    JitterManager::Get().Update(currentFrame);
}

void CBREngine::OnPreRender() {
    if (!m_enabled.load()) return;
    // Jitter is active for projection matrix during scene geometry pass
}

void CBREngine::OnPostRender() {
    if (!m_enabled.load()) return;
    // Geometry pass complete, intermediate quarter-res 2x MSAA buffer ready for resolve
}

void CBREngine::OnPrePresent(void* /*queueOrContext*/, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    uint32_t currentFrame = m_frameIndex.load();

    // Execute Reconstruction Compute Pass
    // Reconstructs full 3840x2160 frame from quarter 2x MSAA + history + motion vectors
    ReconstructionPass::Get().DispatchVulkan(nullptr, currentFrame);

    // Swap history buffers (ping-pong double buffer)
    RenderTargetManager::Get().SwapHistoryBuffers();

    // Render ImGui overlay if toggled on
    UIOverlay::Get().Render();
}

void CBREngine::OnPostPresent() {
    m_frameIndex.fetch_add(1);
}

bool HookManager::Initialize() {
    CBR_LOG_INFO("HookManager initialized.");
    return true;
}

void HookManager::Shutdown() {
    UninstallVulkanHooks();
    UninstallDX12Hooks();
    CBR_LOG_INFO("HookManager shut down.");
}

} // namespace cbr
