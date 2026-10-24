#include "cbr/hooks.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <MinHook.h>
#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/pattern_scanner.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/render_target_manager.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cbr {

namespace {

struct MinimalVkViewport;
struct MinimalVkRect2D;
struct MinimalVkSampleLocationsInfoEXT;

// Function pointer typedefs matching Vulkan loader signatures
typedef void* (*PFN_vkGetDeviceProcAddr)(void* device, const char* pName);
typedef void* (*PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);
typedef int   (*PFN_vkQueuePresentKHR)(void* queue, const void* pPresentInfo);
typedef int   (*PFN_vkCreateSwapchainKHR)(void* device, const void* pCreateInfo, const void* pAllocator, void* pSwapchain);
typedef void  (*PFN_vkCmdSetViewport)(void* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const MinimalVkViewport* pViewports);
typedef void  (*PFN_vkCmdSetScissor)(void* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const MinimalVkRect2D* pScissors);
typedef void  (*PFN_vkCmdBeginRenderPass)(void* commandBuffer, const void* pRenderPassBegin, uint32_t contents);
typedef void  (*PFN_vkCmdEndRenderPass)(void* commandBuffer);
typedef void  (*PFN_vkCmdSetSampleLocationsEXT)(void* commandBuffer, const MinimalVkSampleLocationsInfoEXT* pSampleLocationsInfo);

PFN_vkGetDeviceProcAddr        g_Original_vkGetDeviceProcAddr = nullptr;
PFN_vkGetInstanceProcAddr      g_Original_vkGetInstanceProcAddr = nullptr;
PFN_vkQueuePresentKHR          g_Original_vkQueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR       g_Original_vkCreateSwapchainKHR = nullptr;
PFN_vkCmdSetViewport           g_Original_vkCmdSetViewport = nullptr;
PFN_vkCmdSetScissor            g_Original_vkCmdSetScissor = nullptr;
PFN_vkCmdBeginRenderPass       g_Original_vkCmdBeginRenderPass = nullptr;
PFN_vkCmdEndRenderPass         g_Original_vkCmdEndRenderPass = nullptr;
PFN_vkCmdSetSampleLocationsEXT g_Original_vkCmdSetSampleLocationsEXT = nullptr;

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName);
void* Hooked_vkGetInstanceProcAddr(void* instance, const char* pName);
void  Hooked_vkCmdSetViewport(void* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const MinimalVkViewport* pViewports);
void  Hooked_vkCmdSetScissor(void* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const MinimalVkRect2D* pScissors);
void  Hooked_vkCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, uint32_t contents);
void  Hooked_vkCmdEndRenderPass(void* commandBuffer);
void  Hooked_vkCmdSetSampleLocationsEXT(void* commandBuffer, const MinimalVkSampleLocationsInfoEXT* pSampleLocationsInfo);

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

struct MinimalVkOffset2D {
    int32_t x;
    int32_t y;
};

struct MinimalVkRect2D {
    MinimalVkOffset2D offset;
    MinimalVkExtent2D extent;
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

void Hooked_vkCmdSetViewport(void* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const MinimalVkViewport* pViewports) {
    if (!g_Original_vkCmdSetViewport) return;

    if (!pViewports || viewportCount == 0 || !CBREngine::Get().IsEnabled()) {
        g_Original_vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
        return;
    }

    const auto dims = RenderTargetManager::Get().GetDimensions();
    if (viewportCount == 1 && RenderTargetManager::Get().IsTargetInterceptCandidate(
            static_cast<uint32_t>(pViewports[0].width),
            static_cast<uint32_t>(pViewports[0].height),
            0)) {

        MinimalVkViewport redirected = pViewports[0];
        redirected.width = static_cast<float>(dims.quarterWidth);
        redirected.height = static_cast<float>(dims.quarterHeight);

        const auto config = ConfigManager::Get().GetConfig();
        const uint32_t currentFrame = CBREngine::Get().GetCurrentFrameIndex();

        if (config.samplingStrategy == SamplingStrategy::StrategyA) {
            // Strategy A: Viewport Shift Jittering (0.5px horizontal shift on odd frames)
            if (config.jitterDirection != 0) {
                const float shift = JitterManager::Get().GetQuarterViewportShiftX(currentFrame);
                redirected.x += shift;
            }
        } else if (config.samplingStrategy == SamplingStrategy::StrategyB) {
            // Strategy B: Hardware Programmable Sample Locations (VK_EXT_sample_locations)
            // Viewport is stationary (shift = 0.0f). Alternate dynamic sample locations.
            if (g_Original_vkCmdSetSampleLocationsEXT) {
                MinimalVkSampleLocationsInfoEXT sampleInfo{};
                MinimalVkSampleLocationEXT sampleLocs[2]{};
                ConfigureCheckerboardSampleLocations(currentFrame, sampleInfo, sampleLocs);
                g_Original_vkCmdSetSampleLocationsEXT(commandBuffer, &sampleInfo);
            }
        }

        g_Original_vkCmdSetViewport(commandBuffer, firstViewport, 1, &redirected);
        return;
    }

    g_Original_vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}

void Hooked_vkCmdSetScissor(void* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const MinimalVkRect2D* pScissors) {
    if (!g_Original_vkCmdSetScissor) return;

    if (!pScissors || scissorCount == 0 || !CBREngine::Get().IsEnabled()) {
        g_Original_vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
        return;
    }

    const auto dims = RenderTargetManager::Get().GetDimensions();
    if (scissorCount == 1 && RenderTargetManager::Get().IsTargetInterceptCandidate(
            pScissors[0].extent.width,
            pScissors[0].extent.height,
            0)) {

        MinimalVkRect2D redirected = pScissors[0];
        redirected.extent.width = dims.quarterWidth;
        redirected.extent.height = dims.quarterHeight;

        g_Original_vkCmdSetScissor(commandBuffer, firstScissor, 1, &redirected);
        return;
    }

    g_Original_vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

void Hooked_vkCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, uint32_t contents) {
    if (!g_Original_vkCmdBeginRenderPass) return;

    if (CBREngine::Get().IsEnabled()) {
        const auto config = ConfigManager::Get().GetConfig();
        if (config.samplingStrategy == SamplingStrategy::StrategyB && g_Original_vkCmdSetSampleLocationsEXT) {
            const uint32_t currentFrame = CBREngine::Get().GetCurrentFrameIndex();
            MinimalVkSampleLocationsInfoEXT sampleInfo{};
            MinimalVkSampleLocationEXT sampleLocs[2]{};
            ConfigureCheckerboardSampleLocations(currentFrame, sampleInfo, sampleLocs);
            g_Original_vkCmdSetSampleLocationsEXT(commandBuffer, &sampleInfo);
        }
    }

    g_Original_vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

void Hooked_vkCmdEndRenderPass(void* commandBuffer) {
    if (!g_Original_vkCmdEndRenderPass) return;

    g_Original_vkCmdEndRenderPass(commandBuffer);

    if (CBREngine::Get().IsEnabled()) {
        try {
            CBREngine::Get().OnScenePassEnd(commandBuffer);
        } catch (...) {
        }
    }
}

void Hooked_vkCmdSetSampleLocationsEXT(void* commandBuffer, const MinimalVkSampleLocationsInfoEXT* pSampleLocationsInfo) {
    if (!g_Original_vkCmdSetSampleLocationsEXT) return;

    if (CBREngine::Get().IsEnabled()) {
        const auto config = ConfigManager::Get().GetConfig();
        if (config.samplingStrategy == SamplingStrategy::StrategyB) {
            const uint32_t currentFrame = CBREngine::Get().GetCurrentFrameIndex();
            MinimalVkSampleLocationsInfoEXT sampleInfo{};
            MinimalVkSampleLocationEXT sampleLocs[2]{};
            ConfigureCheckerboardSampleLocations(currentFrame, sampleInfo, sampleLocs);
            g_Original_vkCmdSetSampleLocationsEXT(commandBuffer, &sampleInfo);
            return;
        }
    }

    g_Original_vkCmdSetSampleLocationsEXT(commandBuffer, pSampleLocationsInfo);
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
    if (std::strcmp(pName, "vkCmdSetViewport") == 0) {
        if (!g_Original_vkCmdSetViewport) {
            g_Original_vkCmdSetViewport = reinterpret_cast<PFN_vkCmdSetViewport>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetViewport);
    }
    if (std::strcmp(pName, "vkCmdSetScissor") == 0) {
        if (!g_Original_vkCmdSetScissor) {
            g_Original_vkCmdSetScissor = reinterpret_cast<PFN_vkCmdSetScissor>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetScissor);
    }
    if (std::strcmp(pName, "vkCmdEndRenderPass") == 0) {
        if (!g_Original_vkCmdEndRenderPass) {
            g_Original_vkCmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdEndRenderPass);
    }
    if (std::strcmp(pName, "vkCmdBeginRenderPass") == 0) {
        if (!g_Original_vkCmdBeginRenderPass) {
            g_Original_vkCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdBeginRenderPass);
    }
    if (std::strcmp(pName, "vkCmdSetSampleLocationsEXT") == 0) {
        if (!g_Original_vkCmdSetSampleLocationsEXT) {
            g_Original_vkCmdSetSampleLocationsEXT = reinterpret_cast<PFN_vkCmdSetSampleLocationsEXT>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetSampleLocationsEXT);
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
    if (std::strcmp(pName, "vkCmdSetViewport") == 0) {
        if (!g_Original_vkCmdSetViewport) {
            g_Original_vkCmdSetViewport = reinterpret_cast<PFN_vkCmdSetViewport>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetViewport);
    }
    if (std::strcmp(pName, "vkCmdSetScissor") == 0) {
        if (!g_Original_vkCmdSetScissor) {
            g_Original_vkCmdSetScissor = reinterpret_cast<PFN_vkCmdSetScissor>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetScissor);
    }
    if (std::strcmp(pName, "vkCmdBeginRenderPass") == 0) {
        if (!g_Original_vkCmdBeginRenderPass) {
            g_Original_vkCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdBeginRenderPass);
    }
    if (std::strcmp(pName, "vkCmdEndRenderPass") == 0) {
        if (!g_Original_vkCmdEndRenderPass) {
            g_Original_vkCmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdEndRenderPass);
    }
    if (std::strcmp(pName, "vkCmdSetSampleLocationsEXT") == 0) {
        if (!g_Original_vkCmdSetSampleLocationsEXT) {
            g_Original_vkCmdSetSampleLocationsEXT = reinterpret_cast<PFN_vkCmdSetSampleLocationsEXT>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetSampleLocationsEXT);
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

    // 4. Detour vkCmdSetViewport for viewport downscaling & jitter redirection
    auto pfnCmdSetViewport = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdSetViewport"));
    if (pfnCmdSetViewport) {
        mhStatus = MH_CreateHook(
            pfnCmdSetViewport,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdSetViewport),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdSetViewport)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdSetViewport);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdSetViewport (export at 0x%p).", pfnCmdSetViewport);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkCmdSetViewport (status: %d).", mhStatus);
        }
    }

    // 5. Detour vkCmdSetScissor for scissor bounds redirection
    auto pfnCmdSetScissor = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdSetScissor"));
    if (pfnCmdSetScissor) {
        mhStatus = MH_CreateHook(
            pfnCmdSetScissor,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdSetScissor),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdSetScissor)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdSetScissor);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdSetScissor (export at 0x%p).", pfnCmdSetScissor);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkCmdSetScissor (status: %d).", mhStatus);
        }
    }

    // 6. Detour vkCmdBeginRenderPass for geometry pass sample location injection
    auto pfnCmdBeginRenderPass = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdBeginRenderPass"));
    if (pfnCmdBeginRenderPass) {
        mhStatus = MH_CreateHook(
            pfnCmdBeginRenderPass,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdBeginRenderPass),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdBeginRenderPass)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdBeginRenderPass);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdBeginRenderPass (export at 0x%p).", pfnCmdBeginRenderPass);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkCmdBeginRenderPass (status: %d).", mhStatus);
        }
    }

    // 7. Detour vkCmdEndRenderPass for scene pass completion detection
    auto pfnCmdEndRenderPass = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdEndRenderPass"));
    if (pfnCmdEndRenderPass) {
        mhStatus = MH_CreateHook(
            pfnCmdEndRenderPass,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdEndRenderPass),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdEndRenderPass)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdEndRenderPass);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdEndRenderPass (export at 0x%p).", pfnCmdEndRenderPass);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkCmdEndRenderPass (status: %d).", mhStatus);
        }
    }

    // 8. Resolve and detour vkCmdSetSampleLocationsEXT for Strategy B
    auto pfnCmdSetSampleLocationsEXT = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdSetSampleLocationsEXT"));
    if (pfnCmdSetSampleLocationsEXT) {
        mhStatus = MH_CreateHook(
            pfnCmdSetSampleLocationsEXT,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdSetSampleLocationsEXT),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdSetSampleLocationsEXT)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdSetSampleLocationsEXT);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdSetSampleLocationsEXT (export at 0x%p).", pfnCmdSetSampleLocationsEXT);
        } else {
            g_Original_vkCmdSetSampleLocationsEXT = reinterpret_cast<PFN_vkCmdSetSampleLocationsEXT>(pfnCmdSetSampleLocationsEXT);
            CBR_LOG_INFO("Directly resolved vkCmdSetSampleLocationsEXT: 0x%p", pfnCmdSetSampleLocationsEXT);
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
