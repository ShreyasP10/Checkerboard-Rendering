#include "cbr/hooks.h"
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

namespace {

// Function pointer typedefs matching Vulkan loader signatures
typedef void* (*PFN_vkGetDeviceProcAddr)(void* device, const char* pName);
typedef void* (*PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);
typedef int   (*PFN_vkQueuePresentKHR)(void* queue, const void* pPresentInfo);
typedef int   (*PFN_vkCreateSwapchainKHR)(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain);

PFN_vkQueuePresentKHR    g_Original_vkQueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR g_Original_vkCreateSwapchainKHR = nullptr;

// VK_ERROR_INITIALIZATION_FAILED: returned if a hook is ever invoked without a valid trampoline,
// so the failure is visible to the caller instead of silently dropping frames / swapchains.
constexpr int kVkErrorInitializationFailed = -3;

int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) {
        return kVkErrorInitializationFailed;
    }

    // Exceptions must never propagate into the game's render thread.
    try {
        CBREngine::Get().OnPrePresent(queue, pPresentInfo);
    } catch (...) {
    }

    int result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);

    try {
        CBREngine::Get().OnPostPresent();
    } catch (...) {
    }
    return result;
}

int Hooked_vkCreateSwapchainKHR(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain) {
    if (!g_Original_vkCreateSwapchainKHR) {
        return kVkErrorInitializationFailed;
    }
    CBR_LOG_INFO("Vulkan Swapchain creation intercepted.");
    return g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}

} // namespace

bool HookManager::InstallVulkanHooks() {
    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) {
        CBR_LOG_WARN("vulkan-1.dll not loaded in host process. Deferring Vulkan hooks.");
        return false;
    }

    CBR_LOG_INFO("Vulkan module located at 0x%p. Initializing Vulkan function interception...", vulkanModule);

    // Dynamic resolution of vkGetInstanceProcAddr
    auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));

    if (!pfnGetInstanceProcAddr) {
        CBR_LOG_ERROR("Failed to locate vkGetInstanceProcAddr in vulkan-1.dll.");
        return false;
    }

    // TODO: install real detours (e.g. MinHook) on vkQueuePresentKHR / vkCreateSwapchainKHR and store
    // the trampolines in g_Original_*. Until then NO hook is active, so do not claim success.
    CBR_LOG_WARN("Vulkan hook installation is not implemented yet; no hooks are active.");
    return false;
}

void HookManager::UninstallVulkanHooks() {
    if (m_vulkanHooked) {
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table.");
        m_vulkanHooked = false;
    }
}

} // namespace cbr
