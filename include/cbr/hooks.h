#pragma once

#include <atomic>
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

    bool IsVulkanHooked() const { return m_vulkanHooked.load(); }
    bool IsDX12Hooked() const { return m_dx12Hooked.load(); }

private:
    HookManager() = default;
    ~HookManager() = default;

    std::atomic<bool> m_vulkanHooked{ false };
    std::atomic<bool> m_dx12Hooked{ false };
};

} // namespace cbr
