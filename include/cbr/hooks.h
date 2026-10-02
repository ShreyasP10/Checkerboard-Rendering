#pragma once

#include <atomic>
#include <cstdint>
#include "cbr/config.h"

namespace cbr {

class HookManager {
public:
    static HookManager& Get();

    bool Initialize();
    void Shutdown();

    // Picks the graphics API whose runtime is already loaded in the host process (Vulkan preferred).
    GraphicsApi DetectLoadedApi() const;

    bool InstallVulkanHooks();
    bool InstallDX12Hooks();

    void UninstallVulkanHooks();
    void UninstallDX12Hooks();

    bool IsVulkanHooked() const { return m_vulkanHooked.load(); }
    bool IsDX12Hooked() const { return m_dx12Hooked.load(); }

private:
    HookManager() = default;
    ~HookManager() = default;

    std::atomic<bool> m_vulkanHooked{ false };
    std::atomic<bool> m_dx12Hooked{ false };
};

} // namespace cbr
