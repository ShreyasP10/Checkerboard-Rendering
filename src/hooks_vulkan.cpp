#include "cbr/hooks.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <MinHook.h>
#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/pattern_scanner.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/render_target_manager.h"

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

namespace {

struct MinimalVkViewport;
struct MinimalVkRect2D;
struct MinimalVkSampleLocationsInfoEXT;
struct MinimalVkSamplerCreateInfo;

struct MinimalVkImageViewCreateInfo;
struct MinimalVkFramebufferCreateInfo;

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
typedef int   (*PFN_vkGetSwapchainImagesKHR)(void* device, void* swapchain, uint32_t* pSwapchainImageCount, void** pSwapchainImages);
typedef int   (*PFN_vkAcquireNextImageKHR)(void* device, void* swapchain, uint64_t timeout, void* semaphore, void* fence, uint32_t* pImageIndex);
typedef void  (*PFN_vkCmdDraw)(void* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
typedef void  (*PFN_vkCmdDrawIndexed)(void* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
typedef int   (*PFN_vkCreateDevice)(void* physicalDevice, const void* pCreateInfo, const void* pAllocator, void** pDevice);
typedef void  (*PFN_vkDestroySwapchainKHR)(void* device, void* swapchain, const void* pAllocator);
typedef int   (*PFN_vkCreateSampler)(void* device, const MinimalVkSamplerCreateInfo* pCreateInfo, const void* pAllocator, void* pSampler);
typedef int   (*PFN_vkCreateImageView)(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView);
typedef void  (*PFN_vkDestroyImageView)(void* device, void* imageView, const void* pAllocator);
typedef int   (*PFN_vkCreateFramebuffer)(void* device, const MinimalVkFramebufferCreateInfo* pCreateInfo, const void* pAllocator, void** pFramebuffer);
typedef void  (*PFN_vkDestroyFramebuffer)(void* device, void* framebuffer, const void* pAllocator);

PFN_vkGetDeviceProcAddr        g_Original_vkGetDeviceProcAddr = nullptr;
PFN_vkGetInstanceProcAddr      g_Original_vkGetInstanceProcAddr = nullptr;
PFN_vkQueuePresentKHR          g_Original_vkQueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR       g_Original_vkCreateSwapchainKHR = nullptr;
PFN_vkDestroySwapchainKHR      g_Original_vkDestroySwapchainKHR = nullptr;
PFN_vkCreateDevice             g_Original_vkCreateDevice = nullptr;
PFN_vkGetSwapchainImagesKHR    g_Original_vkGetSwapchainImagesKHR = nullptr;
PFN_vkAcquireNextImageKHR      g_Original_vkAcquireNextImageKHR = nullptr;
PFN_vkCmdSetViewport           g_Original_vkCmdSetViewport = nullptr;
PFN_vkCmdSetScissor            g_Original_vkCmdSetScissor = nullptr;
PFN_vkCmdBeginRenderPass       g_Original_vkCmdBeginRenderPass = nullptr;
PFN_vkCmdEndRenderPass         g_Original_vkCmdEndRenderPass = nullptr;
PFN_vkCmdDraw                  g_Original_vkCmdDraw = nullptr;
PFN_vkCmdDrawIndexed           g_Original_vkCmdDrawIndexed = nullptr;
PFN_vkCmdSetSampleLocationsEXT g_Original_vkCmdSetSampleLocationsEXT = nullptr;
PFN_vkCreateSampler            g_Original_vkCreateSampler = nullptr;
PFN_vkCreateImageView          g_Original_vkCreateImageView = nullptr;
PFN_vkDestroyImageView         g_Original_vkDestroyImageView = nullptr;
PFN_vkCreateFramebuffer        g_Original_vkCreateFramebuffer = nullptr;
PFN_vkDestroyFramebuffer       g_Original_vkDestroyFramebuffer = nullptr;

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName);
void* Hooked_vkGetInstanceProcAddr(void* instance, const char* pName);
int   Hooked_vkCreateDevice(void* physicalDevice, const void* pCreateInfo, const void* pAllocator, void** pDevice);
void  Hooked_vkDestroySwapchainKHR(void* device, void* swapchain, const void* pAllocator);
int   Hooked_vkGetSwapchainImagesKHR(void* device, void* swapchain, uint32_t* pSwapchainImageCount, void** pSwapchainImages);
int   Hooked_vkAcquireNextImageKHR(void* device, void* swapchain, uint64_t timeout, void* semaphore, void* fence, uint32_t* pImageIndex);
void  Hooked_vkCmdSetViewport(void* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const MinimalVkViewport* pViewports);
void  Hooked_vkCmdSetScissor(void* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const MinimalVkRect2D* pScissors);
void  Hooked_vkCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, uint32_t contents);
void  Hooked_vkCmdEndRenderPass(void* commandBuffer);
void  Hooked_vkCmdDraw(void* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void  Hooked_vkCmdDrawIndexed(void* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void  Hooked_vkCmdSetSampleLocationsEXT(void* commandBuffer, const MinimalVkSampleLocationsInfoEXT* pSampleLocationsInfo);
int   Hooked_vkCreateSampler(void* device, const MinimalVkSamplerCreateInfo* pCreateInfo, const void* pAllocator, void* pSampler);
int   Hooked_vkCreateImageView(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView);
void  Hooked_vkDestroyImageView(void* device, void* imageView, const void* pAllocator);
int   Hooked_vkCreateFramebuffer(void* device, const MinimalVkFramebufferCreateInfo* pCreateInfo, const void* pAllocator, void** pFramebuffer);
void  Hooked_vkDestroyFramebuffer(void* device, void* framebuffer, const void* pAllocator);

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

struct MinimalVkSamplerCreateInfo {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    int32_t      magFilter;
    int32_t      minFilter;
    int32_t      mipmapMode;
    int32_t      addressModeU;
    int32_t      addressModeV;
    int32_t      addressModeW;
    float        mipLodBias;
    uint32_t     anisotropyEnable;
    float        maxAnisotropy;
    uint32_t     compareEnable;
    int32_t      compareOp;
    float        minLod;
    float        maxLod;
    int32_t      borderColor;
    uint32_t     unnormalizedCoordinates;
};
static_assert(offsetof(MinimalVkSamplerCreateInfo, mipLodBias) == 44, "MinimalVkSamplerCreateInfo mipLodBias offset mismatch");

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

struct MinimalVkSubresourceRange {
    uint32_t aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
};

struct MinimalVkImageViewCreateInfo {
    uint32_t                  sType;
    const void*               pNext;
    uint32_t                  flags;
    void*                     image;
    int32_t                   viewType;
    int32_t                   format;
    int32_t                   components[4];
    MinimalVkSubresourceRange subresourceRange;
};

struct MinimalVkFramebufferCreateInfo {
    uint32_t            sType;
    const void*         pNext;
    uint32_t            flags;
    void*               renderPass;
    uint32_t            attachmentCount;
    const void* const*  pAttachments;
    uint32_t            width;
    uint32_t            height;
    uint32_t            layers;
};

#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
static_assert(offsetof(MinimalVkFramebufferCreateInfo, sType) == 0, "ABI drift");
static_assert(offsetof(MinimalVkFramebufferCreateInfo, attachmentCount) == 32, "ABI drift: attachmentCount offset");
static_assert(offsetof(MinimalVkFramebufferCreateInfo, pAttachments) == 40, "ABI drift: pAttachments offset");
static_assert(offsetof(MinimalVkFramebufferCreateInfo, width) == 48, "ABI drift: width offset");
static_assert(offsetof(MinimalVkFramebufferCreateInfo, height) == 52, "ABI drift: height offset");
static_assert(offsetof(MinimalVkImageViewCreateInfo, sType) == 0, "ABI drift");
static_assert(offsetof(MinimalVkImageViewCreateInfo, image) == 24, "ABI drift: image offset");
static_assert(offsetof(MinimalVkImageViewCreateInfo, format) == 36, "ABI drift: format offset");
#endif

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

struct MinimalVkRenderPassBeginInfo {
    uint32_t        sType;
    const void*     pNext;
    void*           renderPass;
    void*           framebuffer;
    MinimalVkRect2D renderArea;
    uint32_t        clearValueCount;
    const void*     pClearValues;
};

// Swapchain backbuffer tracking
std::mutex g_swapchainMutex;
std::unordered_map<void*, std::vector<void*>> g_swapchainImages;
std::atomic<void*> g_activeSwapchain{ nullptr };
std::atomic<uint32_t> g_activeImageIndex{ 0 };
std::atomic<void*> g_activePhysicalDevice{ nullptr };

inline void* GetActiveSwapchainImage() {
    std::lock_guard<std::mutex> lock(g_swapchainMutex);
    void* swp = g_activeSwapchain.load();
    auto it = g_swapchainImages.find(swp);
    if (it != g_swapchainImages.end()) {
        uint32_t idx = g_activeImageIndex.load();
        if (idx < it->second.size()) {
            return it->second[idx];
        }
    }
    return nullptr;
}

// Game Attachment & Framebuffer Tracking (Item C-01)
struct TrackedImageView {
    void*    image{ nullptr };
    int32_t  format{ 0 };
    uint32_t aspectMask{ 0 };
};

struct TrackedFramebuffer {
    uint32_t           width{ 0 };
    uint32_t           height{ 0 };
    std::vector<void*> attachments;
};

std::mutex g_attachmentMutex;
std::unordered_map<void*, TrackedImageView> g_imageViews;
std::unordered_map<void*, TrackedFramebuffer> g_framebuffers;

// Scene pass identification and draw tracking using lock-free slot pool
struct PassSlot {
    std::atomic<void*>    cmdBuffer{ nullptr };
    std::atomic<bool>     hasMatchingExtent{ false };
    std::atomic<bool>     viewportDownscaled{ false };
    std::atomic<uint32_t> drawCount{ 0 };
    std::atomic<void*>    activeFramebuffer{ nullptr };
    std::atomic<uint32_t> renderWidth{ 0 };
    std::atomic<uint32_t> renderHeight{ 0 };
};

constexpr size_t kMaxTrackedPassSlots = 64;
static PassSlot g_passSlots[kMaxTrackedPassSlots];

inline PassSlot* GetOrCreatePassSlot(void* cmdBuffer) {
    if (!cmdBuffer) return nullptr;
    const size_t startIdx = (reinterpret_cast<uintptr_t>(cmdBuffer) >> 4) % kMaxTrackedPassSlots;
    for (size_t i = 0; i < 8; ++i) {
        size_t idx = (startIdx + i) % kMaxTrackedPassSlots;
        if (g_passSlots[idx].cmdBuffer.load(std::memory_order_relaxed) == cmdBuffer) {
            return &g_passSlots[idx];
        }
    }
    for (size_t i = 0; i < 8; ++i) {
        size_t idx = (startIdx + i) % kMaxTrackedPassSlots;
        void* expected = nullptr;
        if (g_passSlots[idx].cmdBuffer.compare_exchange_strong(expected, cmdBuffer, std::memory_order_acq_rel)) {
            return &g_passSlots[idx];
        }
    }
    return nullptr;
}

inline PassSlot* FindPassSlot(void* cmdBuffer) {
    if (!cmdBuffer) return nullptr;
    const size_t startIdx = (reinterpret_cast<uintptr_t>(cmdBuffer) >> 4) % kMaxTrackedPassSlots;
    for (size_t i = 0; i < 8; ++i) {
        size_t idx = (startIdx + i) % kMaxTrackedPassSlots;
        if (g_passSlots[idx].cmdBuffer.load(std::memory_order_relaxed) == cmdBuffer) {
            return &g_passSlots[idx];
        }
    }
    return nullptr;
}

inline void IncrementDrawCount(void* cmdBuffer) {
    PassSlot* slot = FindPassSlot(cmdBuffer);
    if (slot) {
        slot->drawCount.fetch_add(1, std::memory_order_relaxed);
    }
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
            if (first) {
                presentTarget = first;
                g_activeSwapchain.store(first);
            }
            if (info.pImageIndices) {
                uint32_t idx = 0;
                std::memcpy(&idx, info.pImageIndices, sizeof(idx));
                g_activeImageIndex.store(idx);
            }
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
                if (w < 2 || h < 2 || w > 16384 || h > 16384) { w = 0; h = 0; } // untrusted: let engine ignore
            } else {
                CBR_LOG_WARN("vkCreateSwapchainKHR: unexpected sType (%u), skipping extent extraction.", sType);
            }
        }
        try {
            CBREngine::Get().OnSwapchainRecreated(w, h);
            void* physDev = g_activePhysicalDevice.load();
            RenderTargetManager::Get().CreateVulkanResources(device, physDev);
            ReconstructionPass::Get().InitializeVulkan(device, physDev);
            const auto& res = RenderTargetManager::Get().GetVulkanResources();
            ReconstructionPass::Get().UpdateVulkanPingPongDescriptors(
                res.quarterColor.view, res.quarterDepth.view,
                res.historyColor[0].view, res.historyDepth[0].view,
                res.historyColor[1].view, res.historyDepth[1].view,
                res.velocity.view,
                res.outputColor.view, res.outputDepth.view,
                res.linearSampler, res.pointSampler
            );
        } catch (...) {}
    }
    return result;
}

int Hooked_vkCreateDevice(void* physicalDevice, const void* pCreateInfo, const void* pAllocator, void** pDevice) {
    if (!g_Original_vkCreateDevice) {
        return kVkErrorInitializationFailed;
    }
    if (physicalDevice) {
        g_activePhysicalDevice.store(physicalDevice);
        CBR_LOG_INFO("Vulkan physical device captured from vkCreateDevice: %p", physicalDevice);
    }
    return g_Original_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
}

void Hooked_vkDestroySwapchainKHR(void* device, void* swapchain, const void* pAllocator) {
    if (swapchain) {
        {
            std::lock_guard<std::mutex> lock(g_swapchainMutex);
            g_swapchainImages.erase(swapchain);
        }
        void* activeSc = g_activeSwapchain.load();
        if (activeSc == swapchain) {
            g_activeSwapchain.store(nullptr);
            g_activeImageIndex.store(0);
        }
        CBR_LOG_INFO("Vulkan swapchain %p destroyed; resources purged from tracking map.", swapchain);
    }
    if (g_Original_vkDestroySwapchainKHR) {
        g_Original_vkDestroySwapchainKHR(device, swapchain, pAllocator);
    }
}

int Hooked_vkGetSwapchainImagesKHR(void* device, void* swapchain, uint32_t* pSwapchainImageCount, void** pSwapchainImages) {
    if (!g_Original_vkGetSwapchainImagesKHR) {
        return kVkErrorInitializationFailed;
    }
    int result = g_Original_vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
    if (result == 0 && pSwapchainImages && pSwapchainImageCount && *pSwapchainImageCount > 0) {
        std::lock_guard<std::mutex> lock(g_swapchainMutex);
        auto& imgs = g_swapchainImages[swapchain];
        imgs.clear();
        for (uint32_t i = 0; i < *pSwapchainImageCount; ++i) {
            imgs.push_back(pSwapchainImages[i]);
        }
        CBR_LOG_INFO("vkGetSwapchainImagesKHR: %u swapchain image(s) captured for swapchain %p (device %p).",
                     *pSwapchainImageCount, swapchain, device);
    }
    return result;
}

int Hooked_vkAcquireNextImageKHR(void* device, void* swapchain, uint64_t timeout, void* semaphore, void* fence, uint32_t* pImageIndex) {
    if (!g_Original_vkAcquireNextImageKHR) {
        return kVkErrorInitializationFailed;
    }
    int result = g_Original_vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    if (result == 0 && pImageIndex) {
        g_activeSwapchain.store(swapchain);
        g_activeImageIndex.store(*pImageIndex);
    }
    return result;
}

int Hooked_vkCreateImageView(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView) {
    if (!g_Original_vkCreateImageView) {
        return kVkErrorInitializationFailed;
    }
    const int result = g_Original_vkCreateImageView(device, pCreateInfo, pAllocator, pView);
    if (result == 0 && pView && *pView && pCreateInfo) {
        MinimalVkImageViewCreateInfo info{};
        std::memcpy(&info, pCreateInfo, sizeof(info));
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        g_imageViews[*pView] = { info.image, info.format, info.subresourceRange.aspectMask };
    }
    return result;
}

void Hooked_vkDestroyImageView(void* device, void* imageView, const void* pAllocator) {
    if (imageView) {
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        g_imageViews.erase(imageView);
    }
    if (g_Original_vkDestroyImageView) {
        g_Original_vkDestroyImageView(device, imageView, pAllocator);
    }
}

int Hooked_vkCreateFramebuffer(void* device, const MinimalVkFramebufferCreateInfo* pCreateInfo, const void* pAllocator, void** pFramebuffer) {
    if (!g_Original_vkCreateFramebuffer) {
        return kVkErrorInitializationFailed;
    }
    const int result = g_Original_vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    if (result == 0 && pFramebuffer && *pFramebuffer && pCreateInfo) {
        MinimalVkFramebufferCreateInfo info{};
        std::memcpy(&info, pCreateInfo, sizeof(info));
        TrackedFramebuffer fb{};
        fb.width = info.width;
        fb.height = info.height;
        if (info.attachmentCount > 0 && info.attachmentCount < 32 && info.pAttachments) {
            for (uint32_t i = 0; i < info.attachmentCount; ++i) {
                void* attView = nullptr;
                std::memcpy(&attView, &info.pAttachments[i], sizeof(attView));
                fb.attachments.push_back(attView);
            }
        }
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        g_framebuffers[*pFramebuffer] = std::move(fb);
    }
    return result;
}

void Hooked_vkDestroyFramebuffer(void* device, void* framebuffer, const void* pAllocator) {
    if (framebuffer) {
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        g_framebuffers.erase(framebuffer);
    }
    if (g_Original_vkDestroyFramebuffer) {
        g_Original_vkDestroyFramebuffer(device, framebuffer, pAllocator);
    }
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

        // Mark this command buffer as actively executing the intercepted scene geometry pass
        CBREngine::Get().MarkCandidatePassActive(commandBuffer);

        PassSlot* slot = GetOrCreatePassSlot(commandBuffer);
        if (slot) {
            slot->viewportDownscaled.store(true, std::memory_order_release);
        }

        MinimalVkViewport redirected = pViewports[0];
        redirected.width = static_cast<float>(dims.quarterWidth);
        redirected.height = static_cast<float>(dims.quarterHeight);

        const auto config = CBREngine::Get().GetCachedFrameConfig();
        const uint32_t currentFrame = CBREngine::Get().GetCurrentFrameIndex();

        if (config.samplingStrategy == SamplingStrategy::StrategyA) {
            // Strategy A: Viewport Shift Jittering (0.5px horizontal shift on odd frames)
            // Mutual exclusivity guard: If the game's camera projection matrix is actively hooked and
            // jittered by PatternScanner, do NOT apply viewport shifting to prevent double-jittering.
            if (config.jitterDirection != 0 && PatternScanner::GetCameraProjectionMatrix() == nullptr) {
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

    if (pRenderPassBegin && CBREngine::Get().IsEnabled()) {
        uint32_t sType = 0;
        std::memcpy(&sType, pRenderPassBegin, sizeof(sType));
        constexpr uint32_t kVkStructureTypeRenderPassBeginInfo = 43;
        if (sType == kVkStructureTypeRenderPassBeginInfo) {
            MinimalVkRenderPassBeginInfo info{};
            std::memcpy(&info, pRenderPassBegin, sizeof(info));
            const auto dims = RenderTargetManager::Get().GetDimensions();
            const uint32_t w = info.renderArea.extent.width;
            const uint32_t h = info.renderArea.extent.height;

            PassSlot* slot = GetOrCreatePassSlot(commandBuffer);
            if (slot) {
                const bool matching = ((w == dims.fullWidth && h == dims.fullHeight) ||
                                       (w == dims.quarterWidth && h == dims.quarterHeight));
                slot->hasMatchingExtent.store(matching, std::memory_order_release);
                slot->viewportDownscaled.store(false, std::memory_order_release);
                slot->drawCount.store(0, std::memory_order_release);
                slot->activeFramebuffer.store(info.framebuffer, std::memory_order_release);
                slot->renderWidth.store(w, std::memory_order_release);
                slot->renderHeight.store(h, std::memory_order_release);
            }
        }
    }

    g_Original_vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

void Hooked_vkCmdDraw(void* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!g_Original_vkCmdDraw) return;
    if (CBREngine::Get().IsEnabled()) {
        IncrementDrawCount(commandBuffer);
    }
    g_Original_vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void Hooked_vkCmdDrawIndexed(void* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!g_Original_vkCmdDrawIndexed) return;
    if (CBREngine::Get().IsEnabled()) {
        IncrementDrawCount(commandBuffer);
    }
    g_Original_vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Hooked_vkCmdEndRenderPass(void* commandBuffer) {
    if (!g_Original_vkCmdEndRenderPass) return;

    g_Original_vkCmdEndRenderPass(commandBuffer);

    if (CBREngine::Get().IsEnabled()) {
        try {
            bool isScenePass = false;
            void* activeFb = nullptr;
            uint32_t rw = 0, rh = 0;
            PassSlot* slot = FindPassSlot(commandBuffer);
            if (slot) {
                const bool downscaled = slot->viewportDownscaled.load(std::memory_order_acquire);
                const bool matching = slot->hasMatchingExtent.load(std::memory_order_acquire);
                const uint32_t draws = slot->drawCount.load(std::memory_order_acquire);
                isScenePass = downscaled || (matching && draws >= 20);
                activeFb = slot->activeFramebuffer.load(std::memory_order_acquire);
                rw = slot->renderWidth.load(std::memory_order_acquire);
                rh = slot->renderHeight.load(std::memory_order_acquire);

                // Release slot back to pool
                slot->hasMatchingExtent.store(false, std::memory_order_relaxed);
                slot->viewportDownscaled.store(false, std::memory_order_relaxed);
                slot->drawCount.store(0, std::memory_order_relaxed);
                slot->activeFramebuffer.store(nullptr, std::memory_order_relaxed);
                slot->renderWidth.store(0, std::memory_order_relaxed);
                slot->renderHeight.store(0, std::memory_order_relaxed);
                slot->cmdBuffer.store(nullptr, std::memory_order_release);
            }

            // Trigger reconstruction if identified as scene pass or engine marked active
            const bool wasCandidateActive = CBREngine::Get().ConsumeCandidatePassActive(commandBuffer);
            if (isScenePass || wasCandidateActive) {
                void* gameColorImage = nullptr;
                void* gameDepthImage = nullptr;
                void* gameVelocityImage = nullptr;
                uint32_t colorAspect = 1; // VK_IMAGE_ASPECT_COLOR_BIT
                uint32_t depthAspect = 2; // VK_IMAGE_ASPECT_DEPTH_BIT
                uint32_t velocityAspect = 1; // VK_IMAGE_ASPECT_COLOR_BIT

                if (activeFb) {
                    std::lock_guard<std::mutex> lock(g_attachmentMutex);
                    auto fbit = g_framebuffers.find(activeFb);
                    if (fbit != g_framebuffers.end()) {
                        for (void* attView : fbit->second.attachments) {
                            auto vit = g_imageViews.find(attView);
                            if (vit != g_imageViews.end()) {
                                if ((vit->second.aspectMask & 2 /* DEPTH */) ||
                                    (vit->second.format >= 124 && vit->second.format <= 130)) {
                                    if (!gameDepthImage) {
                                        gameDepthImage = vit->second.image;
                                        depthAspect = vit->second.aspectMask;
                                    }
                                } else if (!gameColorImage) {
                                    gameColorImage = vit->second.image;
                                    colorAspect = vit->second.aspectMask;
                                } else if (!gameVelocityImage) {
                                    gameVelocityImage = vit->second.image;
                                    velocityAspect = vit->second.aspectMask;
                                }
                            }
                        }
                    }
                }

                if (RenderTargetManager::Get().AreVulkanResourcesCreated()) {
                    const auto& res = RenderTargetManager::Get().GetVulkanResources();
                    const auto dims = RenderTargetManager::Get().GetDimensions();

                    // Step 1: Copy game's rendered quarter-res geometry into mod's quarterColor, quarterDepth, and velocity (Item C-01)
                    if (gameColorImage) {
                        ReconstructionPass::CopyImageRegion(
                            commandBuffer,
                            gameColorImage, res.quarterColor.image,
                            0, 0, 0, 0,
                            dims.quarterWidth, dims.quarterHeight,
                            colorAspect
                        );
                    }
                    if (gameDepthImage) {
                        ReconstructionPass::CopyImageRegion(
                            commandBuffer,
                            gameDepthImage, res.quarterDepth.image,
                            0, 0, 0, 0,
                            dims.quarterWidth, dims.quarterHeight,
                            depthAspect
                        );
                    }
                    if (gameVelocityImage) {
                        ReconstructionPass::CopyImageRegion(
                            commandBuffer,
                            gameVelocityImage, res.velocity.image,
                            0, 0, 0, 0,
                            dims.quarterWidth, dims.quarterHeight,
                            velocityAspect
                        );
                    }

                    // Step 2: Transition layouts and execute CBR reconstruction compute dispatch
                    CBREngine::Get().OnScenePassEnd(commandBuffer);

                    // Step 3: Upscale and blit reconstructed full-resolution output back into the game's color attachment (Item C-01, C-02, C-04)
                    // Subsequent game passes (post-processing, tonemapping, UI/HUD) now read full-resolution scene geometry!
                    if (gameColorImage) {
                        const uint32_t targetDstW = (rw > 0) ? rw : dims.fullWidth;
                        const uint32_t targetDstH = (rh > 0) ? rh : dims.fullHeight;
                        const uint32_t justWrittenIdx = RenderTargetManager::Get().GetCurrentHistoryIndex() & 1u;
                        void* blitSource = res.historyColor[justWrittenIdx].image ? res.historyColor[justWrittenIdx].image : res.outputColor.image;
                        ReconstructionPass::BlitToImage(
                            commandBuffer,
                            blitSource,
                            gameColorImage,
                            dims.fullWidth, dims.fullHeight,
                            targetDstW, targetDstH,
                            2 /* kVkImageLayoutColorAttachmentOptimal */
                        );
                    }
                }
            }
        } catch (...) {
        }
    }
}

void Hooked_vkCmdSetSampleLocationsEXT(void* commandBuffer, const MinimalVkSampleLocationsInfoEXT* pSampleLocationsInfo) {
    if (!g_Original_vkCmdSetSampleLocationsEXT) return;

    if (CBREngine::Get().IsEnabled()) {
        const auto config = CBREngine::Get().GetCachedFrameConfig();
        if (config.samplingStrategy == SamplingStrategy::StrategyB &&
            CBREngine::Get().IsCandidatePassActive(commandBuffer)) {
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

int Hooked_vkCreateSampler(void* device, const MinimalVkSamplerCreateInfo* pCreateInfo, const void* pAllocator, void* pSampler) {
    if (!g_Original_vkCreateSampler) {
        return -1; // VK_ERROR_INITIALIZATION_FAILED
    }
    const auto& config = ConfigManager::Get().GetConfig();
    if (pCreateInfo && config.mipLodBias != 0.0f) {
        MinimalVkSamplerCreateInfo modifiedInfo = *pCreateInfo;
        modifiedInfo.mipLodBias += config.mipLodBias;
        return g_Original_vkCreateSampler(device, &modifiedInfo, pAllocator, pSampler);
    }
    return g_Original_vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
}

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName) {
    if (!pName || !g_Original_vkGetDeviceProcAddr) {
        return nullptr;
    }

    void* realProc = g_Original_vkGetDeviceProcAddr(device, pName);
    if (!realProc) {
        return nullptr;
    }

    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        if (!g_Original_vkCreateDevice) {
            g_Original_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateDevice);
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
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0) {
        if (!g_Original_vkDestroySwapchainKHR) {
            g_Original_vkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkDestroySwapchainKHR);
    }
    if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0) {
        if (!g_Original_vkGetSwapchainImagesKHR) {
            g_Original_vkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkGetSwapchainImagesKHR);
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
    if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0) {
        if (!g_Original_vkAcquireNextImageKHR) {
            g_Original_vkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkAcquireNextImageKHR);
    }
    if (std::strcmp(pName, "vkCmdDraw") == 0) {
        if (!g_Original_vkCmdDraw) {
            g_Original_vkCmdDraw = reinterpret_cast<PFN_vkCmdDraw>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdDraw);
    }
    if (std::strcmp(pName, "vkCmdDrawIndexed") == 0) {
        if (!g_Original_vkCmdDrawIndexed) {
            g_Original_vkCmdDrawIndexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdDrawIndexed);
    }
    if (std::strcmp(pName, "vkCreateSampler") == 0) {
        if (!g_Original_vkCreateSampler) {
            g_Original_vkCreateSampler = reinterpret_cast<PFN_vkCreateSampler>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateSampler);
    }
    if (std::strcmp(pName, "vkCreateImageView") == 0) {
        if (!g_Original_vkCreateImageView) {
            g_Original_vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateImageView);
    }
    if (std::strcmp(pName, "vkDestroyImageView") == 0) {
        if (!g_Original_vkDestroyImageView) {
            g_Original_vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkDestroyImageView);
    }
    if (std::strcmp(pName, "vkCreateFramebuffer") == 0) {
        if (!g_Original_vkCreateFramebuffer) {
            g_Original_vkCreateFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateFramebuffer);
    }
    if (std::strcmp(pName, "vkDestroyFramebuffer") == 0) {
        if (!g_Original_vkDestroyFramebuffer) {
            g_Original_vkDestroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkDestroyFramebuffer);
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

    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        if (!g_Original_vkCreateDevice) {
            g_Original_vkCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateDevice);
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
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0) {
        if (!g_Original_vkDestroySwapchainKHR) {
            g_Original_vkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkDestroySwapchainKHR);
    }
    if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0) {
        if (!g_Original_vkGetSwapchainImagesKHR) {
            g_Original_vkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkGetSwapchainImagesKHR);
    }
    if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0) {
        if (!g_Original_vkAcquireNextImageKHR) {
            g_Original_vkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkAcquireNextImageKHR);
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
    if (std::strcmp(pName, "vkCmdDraw") == 0) {
        if (!g_Original_vkCmdDraw) {
            g_Original_vkCmdDraw = reinterpret_cast<PFN_vkCmdDraw>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdDraw);
    }
    if (std::strcmp(pName, "vkCmdDrawIndexed") == 0) {
        if (!g_Original_vkCmdDrawIndexed) {
            g_Original_vkCmdDrawIndexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdDrawIndexed);
    }
    if (std::strcmp(pName, "vkCmdSetSampleLocationsEXT") == 0) {
        if (!g_Original_vkCmdSetSampleLocationsEXT) {
            g_Original_vkCmdSetSampleLocationsEXT = reinterpret_cast<PFN_vkCmdSetSampleLocationsEXT>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCmdSetSampleLocationsEXT);
    }
    if (std::strcmp(pName, "vkCreateSampler") == 0) {
        if (!g_Original_vkCreateSampler) {
            g_Original_vkCreateSampler = reinterpret_cast<PFN_vkCreateSampler>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateSampler);
    }
    if (std::strcmp(pName, "vkCreateImageView") == 0) {
        if (!g_Original_vkCreateImageView) {
            g_Original_vkCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateImageView);
    }
    if (std::strcmp(pName, "vkDestroyImageView") == 0) {
        if (!g_Original_vkDestroyImageView) {
            g_Original_vkDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkDestroyImageView);
    }
    if (std::strcmp(pName, "vkCreateFramebuffer") == 0) {
        if (!g_Original_vkCreateFramebuffer) {
            g_Original_vkCreateFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkCreateFramebuffer);
    }
    if (std::strcmp(pName, "vkDestroyFramebuffer") == 0) {
        if (!g_Original_vkDestroyFramebuffer) {
            g_Original_vkDestroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(realProc);
        }
        return reinterpret_cast<void*>(&Hooked_vkDestroyFramebuffer);
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

    MH_STATUS mhStatus = MH_OK;

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

    // 2a. Detour vkDestroySwapchainKHR
    auto pfnDestroySwapchain = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkDestroySwapchainKHR"));
    if (pfnDestroySwapchain) {
        mhStatus = MH_CreateHook(
            pfnDestroySwapchain,
            reinterpret_cast<LPVOID>(&Hooked_vkDestroySwapchainKHR),
            reinterpret_cast<LPVOID*>(&g_Original_vkDestroySwapchainKHR)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnDestroySwapchain);
            CBR_LOG_INFO("MinHook: Successfully detoured vkDestroySwapchainKHR (export at 0x%p).", pfnDestroySwapchain);
            hookedAny = true;
        }
    }

    // 2a2. Detour vkCreateDevice
    auto pfnCreateDevice = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCreateDevice"));
    if (pfnCreateDevice) {
        mhStatus = MH_CreateHook(
            pfnCreateDevice,
            reinterpret_cast<LPVOID>(&Hooked_vkCreateDevice),
            reinterpret_cast<LPVOID*>(&g_Original_vkCreateDevice)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCreateDevice);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCreateDevice (export at 0x%p).", pfnCreateDevice);
            hookedAny = true;
        }
    }

    // 2b. Detour vkGetSwapchainImagesKHR
    auto pfnGetSwapchainImages = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkGetSwapchainImagesKHR"));
    if (pfnGetSwapchainImages) {
        mhStatus = MH_CreateHook(
            pfnGetSwapchainImages,
            reinterpret_cast<LPVOID>(&Hooked_vkGetSwapchainImagesKHR),
            reinterpret_cast<LPVOID*>(&g_Original_vkGetSwapchainImagesKHR)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnGetSwapchainImages);
            CBR_LOG_INFO("MinHook: Successfully detoured vkGetSwapchainImagesKHR (export at 0x%p).", pfnGetSwapchainImages);
            hookedAny = true;
        }
    }

    // 2c. Detour vkAcquireNextImageKHR
    auto pfnAcquireNextImage = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkAcquireNextImageKHR"));
    if (pfnAcquireNextImage) {
        mhStatus = MH_CreateHook(
            pfnAcquireNextImage,
            reinterpret_cast<LPVOID>(&Hooked_vkAcquireNextImageKHR),
            reinterpret_cast<LPVOID*>(&g_Original_vkAcquireNextImageKHR)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnAcquireNextImage);
            CBR_LOG_INFO("MinHook: Successfully detoured vkAcquireNextImageKHR (export at 0x%p).", pfnAcquireNextImage);
            hookedAny = true;
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

    // 6b. Detour vkCmdDraw for scene pass draw call tracking
    auto pfnCmdDraw = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdDraw"));
    if (pfnCmdDraw) {
        mhStatus = MH_CreateHook(
            pfnCmdDraw,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdDraw),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdDraw)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdDraw);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdDraw (export at 0x%p).", pfnCmdDraw);
            hookedAny = true;
        }
    }

    // 6c. Detour vkCmdDrawIndexed for scene pass draw call tracking
    auto pfnCmdDrawIndexed = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCmdDrawIndexed"));
    if (pfnCmdDrawIndexed) {
        mhStatus = MH_CreateHook(
            pfnCmdDrawIndexed,
            reinterpret_cast<LPVOID>(&Hooked_vkCmdDrawIndexed),
            reinterpret_cast<LPVOID*>(&g_Original_vkCmdDrawIndexed)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCmdDrawIndexed);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCmdDrawIndexed (export at 0x%p).", pfnCmdDrawIndexed);
            hookedAny = true;
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

    // 9. Intercept vkCreateSampler for negative LOD bias compensation
    auto pfnCreateSampler = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCreateSampler"));
    if (pfnCreateSampler) {
        mhStatus = MH_CreateHook(
            pfnCreateSampler,
            reinterpret_cast<LPVOID>(&Hooked_vkCreateSampler),
            reinterpret_cast<LPVOID*>(&g_Original_vkCreateSampler)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCreateSampler);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCreateSampler (export at 0x%p).", pfnCreateSampler);
            hookedAny = true;
        } else {
            CBR_LOG_WARN("MinHook: Could not detour vkCreateSampler (status: %d).", mhStatus);
        }
    }

    // 10. Intercept vkCreateImageView for attachment tracking (Item C-01)
    auto pfnCreateImageView = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCreateImageView"));
    if (pfnCreateImageView) {
        mhStatus = MH_CreateHook(
            pfnCreateImageView,
            reinterpret_cast<LPVOID>(&Hooked_vkCreateImageView),
            reinterpret_cast<LPVOID*>(&g_Original_vkCreateImageView)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCreateImageView);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCreateImageView (export at 0x%p).", pfnCreateImageView);
            hookedAny = true;
        }
    }

    // 11. Intercept vkDestroyImageView for attachment cleanup
    auto pfnDestroyImageView = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkDestroyImageView"));
    if (pfnDestroyImageView) {
        mhStatus = MH_CreateHook(
            pfnDestroyImageView,
            reinterpret_cast<LPVOID>(&Hooked_vkDestroyImageView),
            reinterpret_cast<LPVOID*>(&g_Original_vkDestroyImageView)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnDestroyImageView);
            CBR_LOG_INFO("MinHook: Successfully detoured vkDestroyImageView (export at 0x%p).", pfnDestroyImageView);
            hookedAny = true;
        }
    }

    // 12. Intercept vkCreateFramebuffer for attachment mapping (Item C-01)
    auto pfnCreateFramebuffer = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkCreateFramebuffer"));
    if (pfnCreateFramebuffer) {
        mhStatus = MH_CreateHook(
            pfnCreateFramebuffer,
            reinterpret_cast<LPVOID>(&Hooked_vkCreateFramebuffer),
            reinterpret_cast<LPVOID*>(&g_Original_vkCreateFramebuffer)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnCreateFramebuffer);
            CBR_LOG_INFO("MinHook: Successfully detoured vkCreateFramebuffer (export at 0x%p).", pfnCreateFramebuffer);
            hookedAny = true;
        }
    }

    // 13. Intercept vkDestroyFramebuffer for framebuffer cleanup
    auto pfnDestroyFramebuffer = reinterpret_cast<void*>(GetProcAddress(vulkanModule, "vkDestroyFramebuffer"));
    if (pfnDestroyFramebuffer) {
        mhStatus = MH_CreateHook(
            pfnDestroyFramebuffer,
            reinterpret_cast<LPVOID>(&Hooked_vkDestroyFramebuffer),
            reinterpret_cast<LPVOID*>(&g_Original_vkDestroyFramebuffer)
        );
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(pfnDestroyFramebuffer);
            CBR_LOG_INFO("MinHook: Successfully detoured vkDestroyFramebuffer (export at 0x%p).", pfnDestroyFramebuffer);
            hookedAny = true;
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
        m_vulkanHooked.store(false);
    }
}

} // namespace cbr
