#include "cbr/ui_overlay.h"
#include "cbr/config.h"
#include "cbr/cbr_engine.h"
#include "cbr/render_target_manager.h"
#include "cbr/hooks.h"
#include "cbr/logger.h"

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

UIOverlay& UIOverlay::Get() {
    static UIOverlay instance;
    return instance;
}

void UIOverlay::Initialize() {
    m_initialized.store(true, std::memory_order_release);
    const bool show = ConfigManager::Get().GetConfig().showOverlay;
    m_visible.store(show, std::memory_order_release);
    m_lastHotkeyDown.store(false, std::memory_order_relaxed);
    CBR_LOG_INFO("UIOverlay initialized (Visible: %s)", show ? "true" : "false");
}

void UIOverlay::Shutdown() {
    m_initialized.store(false, std::memory_order_release);
    m_visible.store(false, std::memory_order_release);
    CBR_LOG_INFO("UIOverlay shut down.");
}

void UIOverlay::SetVisible(bool visible) {
    m_visible.store(visible, std::memory_order_release);
    ConfigManager::Get().Modify([visible](CBRConfig& c) {
        c.showOverlay = visible;
    });
}

void UIOverlay::ToggleVisibility() {
    const bool newVis = !m_visible.load(std::memory_order_acquire);
    SetVisible(newVis);
}

void UIOverlay::CheckHotkeys() {
#if defined(_WIN32)
    // Only poll when the game process owns the foreground window, to avoid
    // toggling while the user types in another app and to reduce AV/anti-cheat
    // heuristics around global GetAsyncKeyState polling.
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid != GetCurrentProcessId()) return;
    }
    // Non-intrusive async key state polling for F11 and Insert
    const bool f11Down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    const bool isDown = f11Down || insertDown;

    const bool wasDown = m_lastHotkeyDown.load(std::memory_order_relaxed);
    if (isDown && !wasDown) {
        ToggleVisibility();
        CBR_LOG_INFO("UIOverlay hotkey pressed. Visibility is now %s", IsVisible() ? "ON" : "OFF");
    }
    m_lastHotkeyDown.store(isDown, std::memory_order_relaxed);
#endif
}

OverlayMetrics UIOverlay::GetCurrentMetrics() const {
    OverlayMetrics metrics{};
    const auto& config = ConfigManager::Get().GetConfig();
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    metrics.currentFrame = CBREngine::Get().GetCurrentFrameIndex();
    metrics.cbrEnabled = CBREngine::Get().IsEnabled();
    metrics.debugView = config.debugView;
    metrics.targetWidth = dims.fullWidth;
    metrics.targetHeight = dims.fullHeight;
    metrics.quarterWidth = dims.quarterWidth;
    metrics.quarterHeight = dims.quarterHeight;

    const size_t vramBytes = RenderTargetManager::Get().GetTotalAllocatedVramBytes();
    metrics.vramFootprintMiB = static_cast<double>(vramBytes) / (1024.0 * 1024.0);
    metrics.vramFootprintMB = static_cast<double>(vramBytes) / 1000000.0;

    const auto api = CBREngine::Get().GetActiveApi();
    metrics.isHooked = (api == GraphicsApi::Vulkan)
        ? HookManager::Get().IsVulkanHooked()
        : HookManager::Get().IsDX12Hooked();

    return metrics;
}

void UIOverlay::Render() {
    // Check toggle hotkeys every presentation interval regardless of current visibility
    CheckHotkeys();

    if (!m_initialized.load(std::memory_order_acquire) || !m_visible.load(std::memory_order_acquire)) {
        return;
    }

    // Diagnostics are ready for presentation when ImGui context is bound:
    // Controls:
    //   - CBREngine::Get().SetEnabled(...)
    //   - ConfigManager::Get().Modify(...) for debugView, historyWeight, depthTolerance, mipLodBias
    // Telemetry:
    //   - GetCurrentMetrics()
}

} // namespace cbr
