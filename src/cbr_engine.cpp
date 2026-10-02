#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/logger.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/ui_overlay.h"
#include "cbr/hooks.h"
#include <cctype>
#include <string>

namespace cbr {

namespace {

LogLevel ParseLogLevel(const std::string& name) {
    std::string s;
    s.reserve(name.size());
    for (unsigned char c : name) s.push_back(static_cast<char>(std::tolower(c)));
    if (s == "debug")                   return LogLevel::Debug;
    if (s == "warn" || s == "warning")  return LogLevel::Warning;
    if (s == "error")                   return LogLevel::Error;
    return LogLevel::Info;
}

} // namespace

CBREngine& CBREngine::Get() {
    static CBREngine instance;
    return instance;
}

bool CBREngine::Initialize() {
    std::call_once(m_initOnce, [this]() {
        // 1. Load configuration first. Messages logged while loading are buffered by the
        //    Logger and flushed (or discarded) once the log destination is known.
        const std::filesystem::path baseDir = m_moduleDirectory; // empty => current directory
        ConfigManager::Get().Load(baseDir / "cbr.ini");
        const auto& config = ConfigManager::Get().GetConfig();

        // 2. Configure logging from the loaded settings
        Logger::Get().SetMinLevel(ParseLogLevel(config.logLevel));
        if (config.logToFile) {
            Logger::Get().Initialize(baseDir / "cbr.log");
        } else {
            Logger::Get().Disable();
        }

        CBR_LOG_INFO("Initializing CBREngine for Red Dead Redemption 2...");
        m_enabled.store(config.enabled);
        GraphicsApi api = config.preferredApi;
        if (api == GraphicsApi::Auto) {
            api = HookManager::Get().DetectLoadedApi();
            CBR_LOG_INFO("PreferredApi=Auto resolved to %s.", api == GraphicsApi::Vulkan ? "Vulkan" : "D3D12");
        }
        m_activeApi.store(api);

        // 3. Initialize Render Target & Jitter Managers
        RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
        JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

        // 4. Initialize Overlay
        UIOverlay::Get().Initialize();

        // 5. Install API Hooks
        HookManager::Get().Initialize();
        if (api == GraphicsApi::Vulkan) {
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

void CBREngine::OnPrePresent(void* queueOrSwapchain, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    // Only the main output may run reconstruction / flip history
    void* mainTarget = m_mainPresentTarget.load();
    if (mainTarget != nullptr && queueOrSwapchain != mainTarget) return;

    uint32_t currentFrame = m_frameIndex.load();

    // NOTE: the argument received here is a VkQueue (Vulkan) or IDXGISwapChain (DX12), NOT a
    // command buffer / command list. The reconstruction pass must record into its own command
    // buffer / list, so nullptr is passed until the real recording path exists.
    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        ReconstructionPass::Get().DispatchVulkan(nullptr, currentFrame);
    } else {
        ReconstructionPass::Get().DispatchDX12(nullptr, currentFrame);
    }

    // Swap history buffers (ping-pong double buffer)
    RenderTargetManager::Get().SwapHistoryBuffers();

    // Render ImGui overlay if toggled on
    UIOverlay::Get().Render();
}

void CBREngine::OnPostPresent(void* presentTarget) {
    // The first present seen after start-up / swapchain recreation defines the main target.
    void* expected = nullptr;
    m_mainPresentTarget.compare_exchange_strong(expected, presentTarget);

    if (m_mainPresentTarget.load() == presentTarget) {
        m_frameIndex.fetch_add(1);
    }
}

void CBREngine::OnSwapchainRecreated() {
    m_mainPresentTarget.store(nullptr);
    m_frameIndex.store(0);
    RenderTargetManager::Get().ResetHistory();
    CBR_LOG_INFO("Swapchain recreated: frame parity and history reset.");
}

} // namespace cbr
