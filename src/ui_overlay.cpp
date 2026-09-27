#include "cbr/ui_overlay.h"
#include "cbr/config.h"
#include "cbr/cbr_engine.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

namespace cbr {

UIOverlay& UIOverlay::Get() {
    static UIOverlay instance;
    return instance;
}

void UIOverlay::Initialize() {
    m_initialized = true;
    m_visible = ConfigManager::Get().GetConfig().showOverlay;
    CBR_LOG_INFO("UIOverlay initialized (Visible: %s)", m_visible ? "true" : "false");
}

void UIOverlay::Shutdown() {
    m_initialized = false;
    CBR_LOG_INFO("UIOverlay shut down.");
}

void UIOverlay::Render() {
    if (!m_initialized || !m_visible) return;

    // This method is called inside the swapchain present hook.
    // When ImGui is integrated, it draws the CBR control panel:
    // - Checkbox: CBR Enabled
    // - ComboBox: Debug View (Normal, Checkerboard Mask, Disocclusion, Motion Vectors)
    // - Sliders: Depth Tolerance, History Weight, MIP LOD Bias
    // - Memory usage metrics and frame dispatch times
}

} // namespace cbr
