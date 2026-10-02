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

HookManager& HookManager::Get() {
    static HookManager instance;
    return instance;
}

GraphicsApi HookManager::DetectLoadedApi() const {
#if defined(_WIN32)
    if (GetModuleHandleA("vulkan-1.dll")) return GraphicsApi::Vulkan;
    if (GetModuleHandleA("d3d12.dll"))    return GraphicsApi::D3D12;
#endif
    return GraphicsApi::Vulkan; // neither runtime is loaded yet: fall back to the documented default
}

bool HookManager::Initialize() {
    CBR_LOG_INFO("HookManager initialized.");
    return true;
}

void HookManager::Shutdown() {
    UninstallVulkanHooks();
    UninstallDX12Hooks();
    CBR_LOG_INFO("HookManager shut down.");
}

} // namespace cbr
