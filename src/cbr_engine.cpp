#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/logger.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/ui_overlay.h"
#include "cbr/hooks.h"
#include "cbr/crash_handler.h"
#include "cbr/pattern_scanner.h"
#include <cctype>
#include <chrono>
#include <cwchar>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
        const auto config = ConfigManager::Get().GetConfig();

        // 2. Configure logging from the loaded settings
        Logger::Get().SetMinLevel(ParseLogLevel(config.logLevel));
        if (config.logToFile) {
            Logger::Get().Initialize(baseDir / "cbr.log");
        } else {
            Logger::Get().Disable();
        }

        CBR_LOG_INFO("Initializing CBREngine for Red Dead Redemption 2...");

        // 2a. Anti-Cheat & Online Safety Verification: refuse execution if anti-cheat or online DLLs are detected
        if (CheckAntiCheatModules()) {
            CBR_LOG_ERROR("CRITICAL: CBREngine initialization refused due to detected anti-cheat / online security modules.");
            m_initialized.store(false);
            return;
        }

        CrashHandler::Get().Install(baseDir);
        CrashHandler::Get().LogSystemTelemetry();
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

        {
            std::lock_guard<std::mutex> lock(m_frameConfigMutex);
            m_cachedFrameConfig = config;
        }

        // 3. Initialize Render Target & Jitter Managers
        RenderTargetManager::Get().Initialize(config.targetWidth, config.targetHeight);
        JitterManager::Get().Initialize(config.targetWidth, config.targetHeight);

        // 4. Initialize Overlay
        UIOverlay::Get().Initialize();

        // 5. Prepare the reconstruction pass for the active API
        if (!baseDir.empty()) {
            ReconstructionPass::Get().SetShaderDirectory(baseDir / "shaders");
        }
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

    static uint32_t s_pollAttempt = 0;
    ++s_pollAttempt;
    if (s_pollAttempt == 1 || s_pollAttempt % 40 == 0) {
        CBR_LOG_INFO("Polling graphics hooks (attempt %u)... Vulkan loaded: %s, D3D12 loaded: %s",
            s_pollAttempt,
            HookManager::Get().DetectLoadedApi() == GraphicsApi::Vulkan ? "Yes" : "No",
            HookManager::Get().DetectLoadedApi() == GraphicsApi::D3D12 ? "Yes" : "No");
    }

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
        CrashHandler::Get().Uninstall();
        HookManager::Get().Shutdown();
        ReconstructionPass::Get().Shutdown();
        UIOverlay::Get().Shutdown();
        RenderTargetManager::Get().Shutdown();
        Logger::Get().Shutdown();
    }

    m_initialized.store(false);
}

bool CBREngine::ConsumeCandidatePassActive(void* cmdBuffer) {
    if (!cmdBuffer) return true; // allow manual/null calls (e.g. tests)
    void* active = m_activeCandidateCmdBuffer.load();
    if (active == cmdBuffer) {
        return m_activeCandidateCmdBuffer.compare_exchange_strong(active, nullptr);
    }
    return false;
}

void CBREngine::OnBeginFrame() {
    if (!m_enabled.load()) return;

    {
        std::lock_guard<std::mutex> lock(m_frameConfigMutex);
        m_cachedFrameConfig = ConfigManager::Get().GetConfig();
    }

    uint32_t currentFrame = m_frameIndex.load();
    JitterManager::Get().Update(currentFrame);

#if defined(_WIN32)
    const auto config = GetCachedFrameConfig();
    if (config.samplingStrategy == SamplingStrategy::StrategyA && config.jitterDirection != 0) {
        PatternScanner::UpdateCameraJitter(currentFrame, m_activeApi.load() == GraphicsApi::Vulkan);
    }
    float* camProj = PatternScanner::GetCameraProjectionMatrix();
    if (camProj) {
        ReconstructionPass::Get().SetCameraMatrices(camProj, nullptr);
    }
#endif
}

void CBREngine::OnPreRender() {
    if (!m_enabled.load()) return;
    // Jitter is active for projection matrix during scene geometry pass
    // (Camera projection jitter is applied in OnBeginFrame; avoid duplicate application here)
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

    static std::atomic<bool> s_firstPassLogged{ false };
    if (!s_firstPassLogged.exchange(true)) {
        CBR_LOG_INFO("First scene render pass intercepted mid-frame (%p). Ready for reconstruction dispatch.", cmdBufferOrContext);
    }

    // Mid-frame dispatch: reconstruct immediately when the quarter-resolution 2x MSAA
    // geometry pass finishes, before post-processing and UI are composited.
    const auto dispatchStart = std::chrono::steady_clock::now();
    if (m_activeApi.load() == GraphicsApi::Vulkan) {
        ReconstructionPass::Get().DispatchVulkan(cmdBufferOrContext, currentFrame);
    } else {
        ReconstructionPass::Get().DispatchDX12(cmdBufferOrContext, currentFrame);
    }
    const auto dispatchEnd = std::chrono::steady_clock::now();

    const float passDurationMs = ReconstructionPass::Get().GetLastDispatchDurationMs();
    if (passDurationMs > 0.0f) {
        m_lastReconDurationMs.store(passDurationMs, std::memory_order_release);
    } else {
        const float cpuElapsedMs = std::chrono::duration<float, std::milli>(dispatchEnd - dispatchStart).count();
        m_lastReconDurationMs.store(cpuElapsedMs > 0.001f ? cpuElapsedMs : 0.001f, std::memory_order_release);
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
    if (m_mainPresentTarget.compare_exchange_strong(expected, presentTarget)) {
        CBR_LOG_INFO("Primary swapchain present target acquired: %p. Active output established.", presentTarget);
    }

    if (m_mainPresentTarget.load() == presentTarget) {
        m_frameIndex.fetch_add(1);
        OnBeginFrame();
    }
}

void CBREngine::OnSwapchainRecreated(uint32_t width, uint32_t height) {
    m_mainPresentTarget.store(nullptr);
    m_activeCandidateCmdBuffer.store(nullptr);
    m_frameIndex.store(0);
    m_lastDispatchedFrame.store(kNoFrame);

    // Swapchain extents are untrusted: odd sizes round up to even for 2x MSAA symmetry (Item 91)
    if (width >= kMinSwapchainExtent && width <= kMaxSwapchainExtent &&
        height >= kMinSwapchainExtent && height <= kMaxSwapchainExtent) {
        const uint32_t evenW = MakeEvenUp(width);
        const uint32_t evenH = MakeEvenUp(height);
        if (evenW != width || evenH != height) {
            CBR_LOG_INFO("Swapchain odd extent (%ux%u) rounded up to even (%ux%u) for 2x MSAA symmetry (Item 91).",
                         width, height, evenW, evenH);
        }
        width = evenW;
        height = evenH;
        RenderTargetManager::Get().Initialize(width, height);
        JitterManager::Get().Initialize(width, height);
        ReconstructionPass::Get().ClearCameraMatrices();
        CBR_LOG_INFO("Swapchain recreated with new resolution %ux%u: frame parity and history reset.", width, height);
        OnBeginFrame();
    } else {
        RenderTargetManager::Get().ResetHistory();
        ReconstructionPass::Get().ClearCameraMatrices();
        CBR_LOG_INFO("Swapchain recreated: frame parity and history reset.");
    }
}

bool CBREngine::HasDisallowedCommandLineArgs(const wchar_t* commandLine) {
    if (!commandLine) return false;

    std::wstring lower;
    for (const wchar_t* p = commandLine; *p != L'\0'; ++p) {
        wchar_t ch = *p;
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        }
        lower.push_back(ch);
    }

    static constexpr const wchar_t* kOnlineFlags[] = {
        L"-rdo",
        L"-sconlineonly",
        L"-multiplayer"
    };

    auto isArgBoundaryLeft = [&](size_t pos) -> bool {
        if (pos == 0) return true;
        const wchar_t prev = lower[pos - 1];
        return prev == L' ' || prev == L'\t' || prev == L'\r' || prev == L'\n' ||
               prev == L'"' || prev == L'\'';
    };
    auto isArgBoundaryRight = [&](size_t pos) -> bool {
        if (pos >= lower.size()) return true;
        const wchar_t next = lower[pos];
        return next == L' ' || next == L'\t' || next == L'\r' || next == L'\n' ||
               next == L'"' || next == L'\'' || next == L'=' || next == L':';
    };

    for (const wchar_t* flag : kOnlineFlags) {
        const size_t flagLen = std::wcslen(flag);
        size_t pos = lower.find(flag);
        while (pos != std::wstring::npos) {
            if (isArgBoundaryLeft(pos) && isArgBoundaryRight(pos + flagLen)) {
                return true;
            }
            pos = lower.find(flag, pos + 1);
        }
    }
    return false;
}

bool CBREngine::CheckAntiCheatModules() const {
#if defined(_WIN32)
    if (HasDisallowedCommandLineArgs(GetCommandLineW())) {
        CBR_LOG_ERROR("CRITICAL: Detected online / multiplayer launch argument (-rdo / -scOnlineOnly / -Multiplayer) in command line! Aborting CBREngine initialization.");
        return true;
    }

    static const char* const kKnownAntiCheatModules[] = {
        "BEClient_x64.dll",
        "BEClient.dll",
        "BattlEye.dll",
        "EasyAntiCheat_x64.dll",
        "EasyAntiCheat.dll",
        "eac_server64.dll",
        "eac_server.dll",
        "EOSOVH-Win64-Shipping.dll",
        "vanguard.dll",
        "Denuvo.dll"
    };

    for (const char* modName : kKnownAntiCheatModules) {
        if (GetModuleHandleA(modName) != nullptr) {
            CBR_LOG_ERROR("CRITICAL: Detected active anti-cheat / online security module '%s' in host process! Aborting CBREngine initialization to protect account standing.", modName);
            return true;
        }
    }
#endif
    return false;
}

} // namespace cbr
