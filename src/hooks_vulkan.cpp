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

int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    CBREngine::Get().OnPrePresent(queue, pPresentInfo);
    int result = 0;
    if (g_Original_vkQueuePresentKHR) {
        result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);
    }
    CBREngine::Get().OnPostPresent();
    return result;
}

int Hooked_vkCreateSwapchainKHR(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain) {
    CBR_LOG_INFO("Vulkan Swapchain creation intercepted.");
    if (g_Original_vkCreateSwapchainKHR) {
        return g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }
    return 0;
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

    m_vulkanHooked = true;
    CBR_LOG_INFO("Vulkan interception hooks successfully registered.");
    return true;
}

void HookManager::UninstallVulkanHooks() {
    if (m_vulkanHooked) {
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table.");
        m_vulkanHooked = false;
    }
}

} // namespace cbr
