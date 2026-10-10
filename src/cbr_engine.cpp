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
            const GraphicsApi detected = HookManager::Get().DetectLoadedApi();
            if (detected == GraphicsApi::Auto) {
                // No runtime loaded yet: default to Vulkan for now and re-detect on every hook attempt
                api = GraphicsApi::Vulkan;
                m_apiPending.store(true);
                CBR_LOG_INFO("PreferredApi=Auto: no graphics runtime loaded yet; will re-detect.");
            } else {
                api = detected;
                CBR_LOG_INFO("PreferredApi=Auto resolved to %s.", api == GraphicsApi::Vulkan ? "Vulkan" : "D3D12");
            }
        }
        m_activeApi.store(api);

        // 3. Initialize Render Target & Jitter Managers
        RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
        JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

        // 4. Initialize Overlay
        UIOverlay::Get().Initialize();

        // 5. Prepare the reconstruction pass for the active API
        HookManager::Get().Initialize();
        if (api == GraphicsApi::Vulkan) {
            ReconstructionPass::Get().InitializeVulkan(nullptr, nullptr);
        } else {
            ReconstructionPass::Get().InitializeDX12(nullptr);
        }

        m_initialized.store(true);
        CBR_LOG_INFO("CBREngine initialized successfully. Waiting for graphics hooks.");
    });

    return m_initialized.load();
}

bool CBREngine::TryInstallHooks() {
    if (!m_initialized.load()) return false;

    std::lock_guard<std::mutex> lock(m_hookMutex);

    // Auto mode with no runtime at init time: pick whichever runtime has appeared since
    if (m_apiPending.load()) {
        const GraphicsApi detected = HookManager::Get().DetectLoadedApi();
        if (detected == GraphicsApi::Auto) return false; // still nothing to hook
        if (detected != m_activeApi.load()) {
            m_activeApi.store(detected);
            if (detected == GraphicsApi::Vulkan) ReconstructionPass::Get().InitializeVulkan(nullptr, nullptr);
            else                                 ReconstructionPass::Get().InitializeDX12(nullptr);
        }
        m_apiPending.store(false);
        CBR_LOG_INFO("PreferredApi=Auto resolved to %s.", detected == GraphicsApi::Vulkan ? "Vulkan" : "D3D12");
    }

    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        return HookManager::Get().IsVulkanHooked() || HookManager::Get().InstallVulkanHooks();
    }
    return HookManager::Get().IsDX12Hooked() || HookManager::Get().InstallDX12Hooks();
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

void CBREngine::OnScenePassEnd(void* cmdBufferOrContext) {
    if (!m_enabled.load()) return;

    const uint32_t currentFrame = m_frameIndex.load();

    // At most one reconstruction (and one history swap) per frame. exchange() makes this race-free
    // if the hook ever fires from more than one thread.
    if (m_lastDispatchedFrame.exchange(currentFrame) == currentFrame) return;

    // Mid-frame dispatch: reconstruct immediately when the quarter-resolution 2x MSAA
    // geometry pass finishes, before post-processing and UI are composited.
    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        ReconstructionPass::Get().DispatchVulkan(cmdBufferOrContext, currentFrame);
    } else {
        ReconstructionPass::Get().DispatchDX12(cmdBufferOrContext, currentFrame);
    }

    // Swap history buffers (ping-pong double buffer)
    RenderTargetManager::Get().SwapHistoryBuffers();
}

void CBREngine::OnPrePresent(void* queueOrSwapchain, const void* /*presentInfo*/) {
    if (!m_enabled.load()) return;

    // Only the main output may handle presentation callbacks
    void* mainTarget = m_mainPresentTarget.load();
    if (mainTarget != nullptr && queueOrSwapchain != mainTarget) return;

    // Render ImGui overlay if toggled on (Present is the correct timing for overlay drawing)
    UIOverlay::Get().Render();
}

void CBREngine::OnPostPresent(void* presentTarget) {
    // Intentional: frame parity advances even while disabled so that re-enabling
    // resumes on a fresh frame (OnScenePassEnd is gated on m_enabled and will
    // dispatch exactly once for the new frame). Do not gate this on m_enabled.
    // The first present seen after start-up / swapchain recreation defines the main target.
    void* expected = nullptr;
    m_mainPresentTarget.compare_exchange_strong(expected, presentTarget);

    if (m_mainPresentTarget.load() == presentTarget) {
        m_frameIndex.fetch_add(1);
    }
}

void CBREngine::OnSwapchainRecreated(uint32_t width, uint32_t height) {
    m_mainPresentTarget.store(nullptr);
    m_frameIndex.store(0);
    m_lastDispatchedFrame.store(kNoFrame);

    // Swapchain extents are untrusted: odd sizes round up to even, implausible ones are ignored.
    if (width >= kMinSwapchainExtent && width <= kMaxSwapchainExtent &&
        height >= kMinSwapchainHeight && height <= kMaxSwapchainExtent) {
        width = MakeEvenUp(width);
        height = MakeEvenUp(height);
        RenderTargetManager::Get().Initialize(width, height);
        JitterManager::Get().Initialize(width, height);
        CBR_LOG_INFO("Swapchain recreated with new resolution %ux%u: frame parity and history reset.", width, height);
    } else {
        RenderTargetManager::Get().ResetHistory();
        CBR_LOG_INFO("Swapchain recreated: frame parity and history reset.");
    }
}

} // namespace cbr
