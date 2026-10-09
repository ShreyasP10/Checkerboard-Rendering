#include "cbr/hooks.h"
#include <atomic>
#include <cstddef>
#include <cstring>
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

// Minimal Vulkan struct layouts for headerless extraction of swapchain and extent.
// WARNING: headerless ABI hack. Offsets must match VkSwapchainCreateInfoKHR /
// VkPresentInfoKHR from the Vulkan headers. Prefer including <vulkan/vulkan.h>
// when the SDK is available. Static asserts below pin the assumed layout.
struct MinimalVkExtent2D {
    uint32_t width;
    uint32_t height;
};

struct MinimalVkSwapchainCreateInfoKHR {
    uint32_t          sType;
    const void*       pNext;
    uint32_t          flags;
    uint64_t          surface;
    uint32_t          minImageCount;
    int32_t           imageFormat;
    int32_t           imageColorSpace;
    MinimalVkExtent2D imageExtent;
};
static_assert(offsetof(MinimalVkSwapchainCreateInfoKHR, sType) == 0, "ABI drift");
static_assert(sizeof(MinimalVkSwapchainCreateInfoKHR) >= sizeof(uint32_t) + sizeof(void*) + sizeof(uint32_t) + sizeof(uint64_t), "ABI drift");

struct MinimalVkPresentInfoKHR {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     waitSemaphoreCount;
    const void*  pWaitSemaphores;
    uint32_t     swapchainCount;
    const void** pSwapchains;
    const uint32_t* pImageIndices;
    int*         pResults;
};

int Hooked_vkQueuePresentKHR(void* queue, const void* pPresentInfo) {
    if (!g_Original_vkQueuePresentKHR) {
        return kVkErrorInitializationFailed;
    }

    void* presentTarget = queue;
    if (pPresentInfo) {
        // Safe copy-out: avoid strict-aliasing violation from reinterpret_cast
        // of the game's struct; copy only the fields we need.
        MinimalVkPresentInfoKHR info{};
        std::memcpy(&info, pPresentInfo, sizeof(info));
        if (info.swapchainCount > 0 && info.swapchainCount < 16 && info.pSwapchains) {
            void* first = nullptr;
            std::memcpy(&first, info.pSwapchains, sizeof(first));
            if (first) presentTarget = first;
        }
    }

    // Exceptions must never propagate into the game's render thread.
    try {
        CBREngine::Get().OnPrePresent(presentTarget, pPresentInfo);
    } catch (...) {
    }

    int result = g_Original_vkQueuePresentKHR(queue, pPresentInfo);

    try {
        CBREngine::Get().OnPostPresent(presentTarget);
    } catch (...) {
    }
    return result;
}

int Hooked_vkCreateSwapchainKHR(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain) {
    if (!g_Original_vkCreateSwapchainKHR) {
        return kVkErrorInitializationFailed;
    }
    CBR_LOG_INFO("Vulkan Swapchain creation intercepted.");
    const int result = g_Original_vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    if (result == 0) { // VK_SUCCESS
        uint32_t w = 0, h = 0;
        if (pCreateInfo) {
            MinimalVkSwapchainCreateInfoKHR info{};
            std::memcpy(&info, pCreateInfo, sizeof(info));
            w = info.imageExtent.width;
            h = info.imageExtent.height;
            if (w > 16384 || h > 16384) { w = 0; h = 0; } // untrusted: let engine ignore
        }
        try { CBREngine::Get().OnSwapchainRecreated(w, h); } catch (...) {}
    }
    return result;
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
    // Logged once only: this function is retried from a polling loop.
    static std::atomic<bool> s_warned{ false };
    if (!s_warned.exchange(true)) {
        CBR_LOG_WARN("Vulkan hook installation is not implemented yet; no hooks are active.");
    }
    return false;
}

void HookManager::UninstallVulkanHooks() {
    if (m_vulkanHooked) {
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table.");
        m_vulkanHooked = false;
    }
}

} // namespace cbr
