#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include "cbr/config.h"

namespace cbr {

struct OverlayMetrics {
    uint32_t currentFrame{ 0 };
    bool     cbrEnabled{ true };
    uint32_t debugView{ 0 };
    uint32_t targetWidth{ 0 };
    uint32_t targetHeight{ 0 };
    uint32_t quarterWidth{ 0 };
    uint32_t quarterHeight{ 0 };
    double   vramFootprintMiB{ 0.0 };
    double   vramFootprintMB{ 0.0 };
    bool     isHooked{ false };
};

// Function callback signature for custom in-game HUD or Dear ImGui renderers
typedef void (*HudRenderCallback)(const OverlayMetrics& metrics);

class UIOverlay {
public:
    static UIOverlay& Get();

    void Initialize();
    void Shutdown();

    void Render();
    void CheckHotkeys();
    void ToggleVisibility();
    void SetVisible(bool visible);
    bool IsVisible() const { return m_visible.load(std::memory_order_relaxed); }
    bool IsInitialized() const { return m_initialized.load(std::memory_order_relaxed); }

    OverlayMetrics GetCurrentMetrics() const;
    std::string    GetFormattedTelemetry() const;

    // Interactive Control HUD API (Category 7, Item 96)
    void SetHistoryWeight(float weight);
    void SetDepthTolerance(float tolerance);
    void SetMipLodBias(float bias);
    void SetDebugView(DebugViewMode mode);
    void SetColorSpace(ColorSpace colorSpace);
    void SetColorClamping(bool enable);
    void SetSpatialFallback(bool enable);
    void SetMotionDilation(bool enable);
    void SetCbrEnabled(bool enable);

    void RegisterHudRenderCallback(HudRenderCallback callback);

private:
    UIOverlay() = default;
    ~UIOverlay() = default;

    std::atomic<bool>              m_visible{ false };
    std::atomic<bool>              m_initialized{ false };
    std::atomic<bool>              m_lastHotkeyDown{ false };
    std::atomic<HudRenderCallback> m_hudCallback{ nullptr };
};

} // namespace cbr
