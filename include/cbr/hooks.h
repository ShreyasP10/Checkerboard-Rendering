#pragma once

#include <cstdint>

namespace cbr {

class HookManager {
public:
    static HookManager& Get();

    bool Initialize();
    void Shutdown();

    bool InstallVulkanHooks();
    bool InstallDX12Hooks();

    void UninstallVulkanHooks();
    void UninstallDX12Hooks();

    bool IsVulkanHooked() const { return m_vulkanHooked; }
    bool IsDX12Hooked() const { return m_dx12Hooked; }

private:
    HookManager() = default;
    ~HookManager() = default;

    bool m_vulkanHooked{ false };
    bool m_dx12Hooked{ false };
};

} // namespace cbr
