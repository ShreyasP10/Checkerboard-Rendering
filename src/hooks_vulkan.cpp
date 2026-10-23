#include "cbr/hooks.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <MinHook.h>
#include "cbr/cbr_engine.h"
#include "cbr/logger.h"
#include "cbr/reconstruction_pass.h"

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

PFN_vkGetDeviceProcAddr   g_Original_vkGetDeviceProcAddr = nullptr;
PFN_vkGetInstanceProcAddr g_Original_vkGetInstanceProcAddr = nullptr;
PFN_vkQueuePresentKHR     g_Original_vkQueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR  g_Original_vkCreateSwapchainKHR = nullptr;

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName);
void* Hooked_vkGetInstanceProcAddr(void* instance, const char* pName);

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
static_assert(offsetof(MinimalVkSwapchainCreateInfoKHR, imageExtent) == (sizeof(void*) == 8 ? 44 : 28), "ABI drift: imageExtent offset");
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

// =============================================================================
// Vulkan Checkerboard Structures & Enums (CHECKERBOARD.md: Vulkan Setup)
// =============================================================================
constexpr uint32_t kVkSampleCount2Bit = 0x00000002;
constexpr uint32_t kVkStructureTypeSampleLocationsInfoEXT = 1000143000;
constexpr uint32_t kVkStructureTypePipelineSampleLocationsStateCreateInfoEXT = 1000143002;
constexpr uint32_t kVkStructureTypeRenderPassSampleLocationsBeginInfoEXT = 1000143003;
constexpr uint32_t kVkDynamicStateViewport = 0;
constexpr uint32_t kVkDynamicStateSampleLocationsEXT = 1000143000;
constexpr uint32_t kVkImageCreateSampleLocationsCompatibleDepthBitEXT = 0x00001000;

struct MinimalVkSampleLocationEXT {
    float x;
    float y;
};

struct MinimalVkSampleLocationsInfoEXT {
    uint32_t                          sType;
    const void*                       pNext;
    uint32_t                          sampleLocationsPerPixel; // kVkSampleCount2Bit
    MinimalVkExtent2D                 sampleLocationGridSize;  // {1, 1}
    uint32_t                          sampleLocationsCount;    // 2
    const MinimalVkSampleLocationEXT* pSampleLocations;
};

struct MinimalVkPipelineMultisampleStateCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    uint32_t     rasterizationSamples; // VK_SAMPLE_COUNT_2_BIT
    uint32_t     sampleShadingEnable;  // VK_TRUE
    float        minSampleShading;     // 1.0f (CHECKERBOARD.md line 199)
    const uint32_t* pSampleMask;
    uint32_t     alphaToCoverageEnable;
    uint32_t     alphaToOneEnable;
};

struct MinimalVkViewport {
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
};

// Configures alternating 2x MSAA sample locations per frame according to CHECKERBOARD.md (Strategy B).
// NOTE (MUTUAL EXCLUSIVITY): When using programmable sample locations (Strategy B),
// viewport horizontal jittering (Strategy A via GetQuarterViewportShiftX) MUST be disabled (set to 0).
// Combining sample location alternation with viewport shifting cancels out checkerboard coverage.
inline void ConfigureCheckerboardSampleLocations(uint32_t frameIndex,
                                                MinimalVkSampleLocationsInfoEXT& outInfo,
                                                MinimalVkSampleLocationEXT outLocations[2]) {
    outInfo.sType = kVkStructureTypeSampleLocationsInfoEXT;
    outInfo.pNext = nullptr;
    outInfo.sampleLocationsPerPixel = kVkSampleCount2Bit;
    outInfo.sampleLocationGridSize = { 1, 1 };
    outInfo.sampleLocationsCount = 2;

    if ((frameIndex & 1u) == 0u) {
        // Frame 0: Standard 2x MSAA (0.75, 0.75) and (0.25, 0.25)
        outLocations[0] = { 0.75f, 0.75f };
        outLocations[1] = { 0.25f, 0.25f };
    } else {
        // Frame 1: Alternating positions (0.25, 0.75) and (0.75, 0.25)
        outLocations[0] = { 0.25f, 0.75f };
        outLocations[1] = { 0.75f, 0.25f };
    }
    outInfo.pSampleLocations = outLocations;
}

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
            uint32_t sType = 0;
            std::memcpy(&sType, pCreateInfo, sizeof(sType));
            constexpr uint32_t kVkStructureTypeSwapchainCreateInfoKHR = 1000001000;
            if (sType == kVkStructureTypeSwapchainCreateInfoKHR) {
                MinimalVkSwapchainCreateInfoKHR info{};
                std::memcpy(&info, pCreateInfo, sizeof(info));
                w = info.imageExtent.width;
                h = info.imageExtent.height;
                if (w > 16384 || h > 16384) { w = 0; h = 0; } // untrusted: let engine ignore
            } else {
                CBR_LOG_WARN("vkCreateSwapchainKHR: unexpected sType (%u), skipping extent extraction.", sType);
            }
        }
        try { CBREngine::Get().OnSwapchainRecreated(w, h); } catch (...) {}
        try { ReconstructionPass::Get().InitializeVulkan(device, nullptr); } catch (...) {}
    }
    return result;
}

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName) {
    if (!pName || !g_Original_vkGetDeviceProcAddr) {
        return nullptr;
    }

    void* realProc = g_Original_vkGetDeviceProcAddr(device, pName);
    if (!realProc) {
        return nullptr;
    }

    if (std::strcmp(pName, "vkQueuePresentKHR") == 0) {
        if (!g_Original_vkQueuePresentKHR) {
            g_Original_vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkQueuePresentKHR);
    }
    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        if (!g_Original_vkCreateSwapchainKHR) {
            g_Original_vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateSwapchainKHR);
    }
    return realProc;
}

void* Hooked_vkGetInstanceProcAddr(void* instance, const char* pName) {
    if (!pName || !g_Original_vkGetInstanceProcAddr) {
        return nullptr;
    }

    void* realProc = g_Original_vkGetInstanceProcAddr(instance, pName);
    if (!realProc) {
        return nullptr;
    }

    if (std::strcmp(pName, "vkQueuePresentKHR") == 0) {
        if (!g_Original_vkQueuePresentKHR) {
            g_Original_vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkQueuePresentKHR);
    }
    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        if (!g_Original_vkCreateSwapchainKHR) {
            g_Original_vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateSwapchainKHR);
    }
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        if (!g_Original_vkGetDeviceProcAddr) {
            g_Original_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkGetDeviceProcAddr);
    }
    return realProc;
}

} // namespace

bool HookManager::InstallVulkanHooks() {
    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) {
        static std::atomic<bool> s_warnedMissing{ false };
        if (!s_warnedMissing.exchange(true)) {
            CBR_LOG_WARN("vulkan-1.dll not loaded in host process. Deferring Vulkan hooks.");
        }
        return false;
    }

    static std::atomic<bool> s_warnedFound{ false };
    if (!s_warnedFound.exchange(true)) {
        CBR_LOG_INFO("Vulkan module located at 0x%p. Initializing Vulkan function interception via MinHook...", vulkanModule);
    }

    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
        CBR_LOG_ERROR("Failed to initialize MinHook core (MH_STATUS: %d).", mhStatus);
        return false;
    }

    // Resolve exported function addresses from vulkan-1.dll
    auto pfnQueuePresent = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkQueuePresentKHR"));
    auto pfnCreateSwapchain = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCreateSwapchainKHR"));
    auto pfnGetInstanceProcAddr = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkGetInstanceProcAddr"));

    bool hookedAny = false;

    // 1. Detour vkQueuePresentKHR
    if (pfnQueuePresent) {
        mhStatus = MH_CreateHook(
            pfnQueuePresent,
            reinterpret_cast<LPVOID>(&Hooked_vkQueuePresentKHR),
            reinterpret_cast<LPVOID*>(&g_Original_vkQueuePresentKHR)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnQueuePresent);
            CBR_LOG_INFO("MinHook: Successfully detoured vkQueuePresentKHR (export at 0x%p).", pfnQueuePresent);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkQueuePresentKHR (status: %d).", mhStatus);
        }
    }

    // 2. Detour vkCreateSwapchainKHR
    if (pfnCreateSwapchain) {
        mhStatus = MH_CreateHook(
            pfnCreateSwapchain,
            reinterpret_cast<LPVOID>(&Hooked_vkCreateSwapchainKHR),
            reinterpret_cast<LPVOID*>(&g_Original_vkCreateSwapchainKHR)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCreateSwapchain);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCreateSwapchainKHR (export at 0x%p).", pfnCreateSwapchain);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkCreateSwapchainKHR (status: %d).", mhStatus);
        }
    }

    // 3. Detour vkGetInstanceProcAddr to intercept dynamic device/instance queries
    if (pfnGetInstanceProcAddr) {
        mhStatus = MH_CreateHook(
            pfnGetInstanceProcAddr,
            reinterpret_cast<LPVOID>(&Hooked_vkGetInstanceProcAddr),
            reinterpret_cast<LPVOID*>(&g_Original_vkGetInstanceProcAddr)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnGetInstanceProcAddr);
            CBR_LOG_INFO("MinHook: Successfully detoured vkGetInstanceProcAddr (export at 0x%p).", pfnGetInstanceProcAddr);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkGetInstanceProcAddr (status: %d).", mhStatus);
        }
    }

    if (hookedAny) {
        m_vulkanHooked.store(true);
        CBR_LOG_INFO("Vulkan graphics interception is now ACTIVE.");
        return true;
    }

    CBR_LOG_ERROR("Failed to establish any Vulkan detour hooks.");
    return false;
}

void HookManager::UninstallVulkanHooks() {
    if (m_vulkanHooked.load()) {
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table.");
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        m_vulkanHooked.store(false);
    }
}

} // namespace cbr
