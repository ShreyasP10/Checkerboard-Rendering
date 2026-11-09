#include "cbr/ui_overlay.h"
#include <algorithm>
#include <cstdio>
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

void UIOverlay::SetHistoryWeight(float weight) {
    const float clamped = std::clamp(weight, 0.0f, 1.0f);
    ConfigManager::Get().Modify([clamped](CBRConfig& c) {
        c.historyWeight = clamped;
    });
}

void UIOverlay::SetDepthTolerance(float tolerance) {
    const float clamped = std::clamp(tolerance, 1e-5f, 1.0f);
    ConfigManager::Get().Modify([clamped](CBRConfig& c) {
        c.depthTolerance = clamped;
    });
}

void UIOverlay::SetMipLodBias(float bias) {
    const float clamped = std::clamp(bias, -4.0f, 4.0f);
    ConfigManager::Get().Modify([clamped](CBRConfig& c) {
        c.mipLodBias = clamped;
    });
}

void UIOverlay::SetDebugView(DebugViewMode mode) {
    ConfigManager::Get().Modify([mode](CBRConfig& c) {
        c.debugView = static_cast<uint32_t>(mode);
    });
}

void UIOverlay::SetColorSpace(ColorSpace colorSpace) {
    ConfigManager::Get().Modify([colorSpace](CBRConfig& c) {
        c.colorSpace = colorSpace;
    });
}

void UIOverlay::SetColorClamping(bool enable) {
    ConfigManager::Get().Modify([enable](CBRConfig& c) {
        c.enableColorClamping = enable;
    });
}

void UIOverlay::SetSpatialFallback(bool enable) {
    ConfigManager::Get().Modify([enable](CBRConfig& c) {
        c.enableSpatialFallback = enable;
    });
}

void UIOverlay::SetMotionDilation(bool enable) {
    ConfigManager::Get().Modify([enable](CBRConfig& c) {
        c.enableMotionDilation = enable;
    });
}

void UIOverlay::SetCbrEnabled(bool enable) {
    CBREngine::Get().SetEnabled(enable);
}

void UIOverlay::RegisterHudRenderCallback(HudRenderCallback callback) {
    m_hudCallback.store(callback, std::memory_order_release);
}

std::string UIOverlay::GetFormattedTelemetry() const {
    const auto m = GetCurrentMetrics();
    const auto& config = ConfigManager::Get().GetConfig();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "=== RDR2 CBR Telemetry Overlay ===\n"
                  "Status:          %s\n"
                  "Frame:           %u\n"
                  "Resolution:      %ux%u (Quarter: %ux%u)\n"
                  "VRAM Allocated:  %.2f MiB\n"
                  "API Hooked:      %s\n"
                  "Debug View:      %u\n"
                  "History Weight:  %.2f\n"
                  "Depth Tolerance: %.4f\n"
                  "Mip LOD Bias:    %.2f\n"
                  "Color Space:     %s\n",
                  m.cbrEnabled ? "ACTIVE (Checkerboard 2x MSAA)" : "BYPASS",
                  m.currentFrame,
                  m.targetWidth, m.targetHeight, m.quarterWidth, m.quarterHeight,
                  m.vramFootprintMiB,
                  m.isHooked ? "YES" : "NO",
                  m.debugView,
                  config.historyWeight,
                  config.depthTolerance,
                  config.mipLodBias,
                  (config.colorSpace == ColorSpace::RGB) ? "RGB" : ((config.colorSpace == ColorSpace::ICtCp) ? "ICtCp" : "YCoCg"));
    return std::string(buf);
}

void UIOverlay::Render() {
    // Check toggle hotkeys every presentation interval regardless of current visibility
    CheckHotkeys();

    if (!m_initialized.load(std::memory_order_acquire) || !m_visible.load(std::memory_order_acquire)) {
        return;
    }

    auto cb = m_hudCallback.load(std::memory_order_acquire);
    if (cb) {
        cb(GetCurrentMetrics());
    } else {
        // Built-in Telemetry HUD fallback (Item H-04):
        // When no external ImGui callback is hooked, periodically emit OSD telemetry
        static std::atomic<uint32_t> s_lastOutputFrame{ ~0u };
        const uint32_t curFrame = CBREngine::Get().GetCurrentFrameIndex();
        const uint32_t last = s_lastOutputFrame.load(std::memory_order_relaxed);
        if (last == ~0u || (curFrame >= last && curFrame - last >= 60)) {
            s_lastOutputFrame.store(curFrame, std::memory_order_relaxed);
            std::string text = GetFormattedTelemetry();
#if defined(_WIN32)
            OutputDebugStringA(text.c_str());
#endif
            CBR_LOG_INFO("HUD OSD:\n%s", text.c_str());
        }
    }
}

} // namespace cbr
