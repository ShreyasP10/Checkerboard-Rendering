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
#include <MinHook.h>
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
    return GraphicsApi::Auto; // neither runtime is loaded (yet)
}

bool HookManager::Initialize() {
#if defined(_WIN32)
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        CBR_LOG_ERROR("HookManager: MH_Initialize failed with code: %d", status);
        return false;
    }
    if (status == MH_ERROR_ALREADY_INITIALIZED) {
        CBR_LOG_INFO("HookManager: MinHook was previously initialized by host or companion module (Item 87).");
    }
#endif
    CBR_LOG_INFO("HookManager initialized.");
    return true;
}

void HookManager::Shutdown() {
    UninstallVulkanHooks();
    UninstallDX12Hooks();
#if defined(_WIN32)
    MH_Uninitialize();
#endif
    CBR_LOG_INFO("HookManager shut down.");
}

} // namespace cbr
