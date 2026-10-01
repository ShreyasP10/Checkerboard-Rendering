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
    std::call_once(m_initOnce, [this]() {
        // 1. Resolve configuration path relative to module directory
        std::string configPath = m_moduleDirectory.empty() ? "cbr.ini" : (m_moduleDirectory + "\\cbr.ini");
        ConfigManager::Get().Load(configPath);
        const auto& config = ConfigManager::Get().GetConfig();

        // 2. Initialize Logger if enabled in configuration
        if (config.logToFile) {
            std::string logPath = m_moduleDirectory.empty() ? "cbr.log" : (m_moduleDirectory + "\\cbr.log");
            Logger::Get().Initialize(logPath);
        }

        CBR_LOG_INFO("Initializing CBREngine for Red Dead Redemption 2...");
        m_enabled.store(config.enabled);
        m_activeApi.store(config.preferredApi);

        // 3. Initialize Render Target & Jitter Managers
        RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
        JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

        // 4. Initialize Overlay
        UIOverlay::Get().Initialize();

        // 5. Install API Hooks
        HookManager::Get().Initialize();
        if (config.preferredApi == GraphicsApi::Vulkan) {
            HookManager::Get().InstallVulkanHooks();
            ReconstructionPass::Get().InitializeVulkan(nullptr, nullptr);
        } else {
            HookManager::Get().InstallDX12Hooks();
            ReconstructionPass::Get().InitializeDX12(nullptr);
        }

        m_initialized.store(true);
        CBR_LOG_INFO("CBREngine initialized successfully. Ready for frame interception.");
    });

    return m_initialized.load();
}

void CBREngine::Shutdown(bool isProcessExit) {
    if (!m_initialized.load()) return;

    if (!isProcessExit) {
        CBR_LOG_INFO("Shutting down CBREngine cleanly...");
        HookManager::Get().Shutdown();
        ReconstructionPass::Get().Shutdown();
        UIOverlay::Get().Shutdown();
        RenderTargetManager::Get().Shutdown();
        Logger::Get().Shutdown();
    }

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

void CBREngine::OnPrePresent(void* queueOrContext, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    uint32_t currentFrame = m_frameIndex.load();

    // Execute Reconstruction Compute Pass based on active API
    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        ReconstructionPass::Get().DispatchVulkan(queueOrContext, currentFrame);
    } else {
        ReconstructionPass::Get().DispatchDX12(queueOrContext, currentFrame);
    }

    // Swap history buffers (ping-pong double buffer)
    RenderTargetManager::Get().SwapHistoryBuffers();

    // Render ImGui overlay if toggled on
    UIOverlay::Get().Render();
}

void CBREngine::OnPostPresent() {
    m_frameIndex.fetch_add(1);
}

} // namespace cbr
