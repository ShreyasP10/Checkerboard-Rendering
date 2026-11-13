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

struct MinimalVkImageCreateInfo;
struct MinimalVkImageViewCreateInfo;
struct MinimalVkFramebufferCreateInfo;
struct MinimalVkRenderPassCreateInfo;

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
struct MinimalVkAcquireNextImageInfoKHR {
    uint32_t    sType;      // VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR = 1000060010
    const void* pNext;
    void*       swapchain;
    uint64_t    timeout;
    void*       semaphore;
    void*       fence;
    uint32_t    deviceMask;
};
static_assert(sizeof(MinimalVkAcquireNextImageInfoKHR) == sizeof(void*) * 7,
              "VkAcquireNextImageInfoKHR ABI size mismatch");

typedef int   (*PFN_vkAcquireNextImageKHR)(void* device, void* swapchain, uint64_t timeout, void* semaphore, void* fence, uint32_t* pImageIndex);
typedef int   (*PFN_vkAcquireNextImage2KHR)(void* device, const MinimalVkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex);
typedef void  (*PFN_vkCmdDraw)(void* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
typedef void  (*PFN_vkCmdDrawIndexed)(void* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
typedef int   (*PFN_vkCreateDevice)(void* physicalDevice, const void* pCreateInfo, const void* pAllocator, void** pDevice);
typedef void  (*PFN_vkDestroySwapchainKHR)(void* device, void* swapchain, const void* pAllocator);
typedef int   (*PFN_vkCreateSampler)(void* device, const MinimalVkSamplerCreateInfo* pCreateInfo, const void* pAllocator, void* pSampler);
typedef int   (*PFN_vkCreateImage)(void* device, const MinimalVkImageCreateInfo* pCreateInfo, const void* pAllocator, void** pImage);
typedef void  (*PFN_vkDestroyImage)(void* device, void* image, const void* pAllocator);
typedef int   (*PFN_vkCreateImageView)(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView);
typedef void  (*PFN_vkDestroyImageView)(void* device, void* imageView, const void* pAllocator);
typedef int   (*PFN_vkCreateFramebuffer)(void* device, const MinimalVkFramebufferCreateInfo* pCreateInfo, const void* pAllocator, void** pFramebuffer);
typedef void  (*PFN_vkDestroyFramebuffer)(void* device, void* framebuffer, const void* pAllocator);
typedef int   (*PFN_vkCreateRenderPass)(void* device, const MinimalVkRenderPassCreateInfo* pCreateInfo, const void* pAllocator, void** pRenderPass);
typedef int   (*PFN_vkCreateRenderPass2)(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass);
typedef void  (*PFN_vkDestroyRenderPass)(void* device, void* renderPass, const void* pAllocator);
typedef int   (*PFN_vkEnumeratePhysicalDevices)(void* instance, uint32_t* pPhysicalDeviceCount, void** pPhysicalDevices);
typedef void  (*PFN_vkGetPhysicalDeviceProperties)(void* physicalDevice, void* pProperties);
typedef void  (*PFN_vkGetPhysicalDeviceMemoryProperties)(void* physicalDevice, void* pMemoryProperties);
typedef void  (*PFN_vkCmdResolveImage)(void* commandBuffer, void* srcImage, uint32_t srcImageLayout, void* dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions);

PFN_vkGetDeviceProcAddr                     g_Original_vkGetDeviceProcAddr = nullptr;
PFN_vkGetInstanceProcAddr                   g_Original_vkGetInstanceProcAddr = nullptr;
PFN_vkQueuePresentKHR                       g_Original_vkQueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR                    g_Original_vkCreateSwapchainKHR = nullptr;
PFN_vkDestroySwapchainKHR                   g_Original_vkDestroySwapchainKHR = nullptr;
PFN_vkCreateDevice                          g_Original_vkCreateDevice = nullptr;
PFN_vkGetSwapchainImagesKHR                 g_Original_vkGetSwapchainImagesKHR = nullptr;
PFN_vkAcquireNextImageKHR                   g_Original_vkAcquireNextImageKHR = nullptr;
PFN_vkAcquireNextImage2KHR                  g_Original_vkAcquireNextImage2KHR = nullptr;
PFN_vkCmdSetViewport                        g_Original_vkCmdSetViewport = nullptr;
PFN_vkCmdSetScissor                         g_Original_vkCmdSetScissor = nullptr;
PFN_vkCmdBeginRenderPass                    g_Original_vkCmdBeginRenderPass = nullptr;
PFN_vkCmdEndRenderPass                      g_Original_vkCmdEndRenderPass = nullptr;
PFN_vkCmdDraw                               g_Original_vkCmdDraw = nullptr;
PFN_vkCmdDrawIndexed                        g_Original_vkCmdDrawIndexed = nullptr;
PFN_vkCmdSetSampleLocationsEXT              g_Original_vkCmdSetSampleLocationsEXT = nullptr;
PFN_vkCreateSampler                         g_Original_vkCreateSampler = nullptr;
PFN_vkCreateImage                           g_Original_vkCreateImage = nullptr;
PFN_vkDestroyImage                          g_Original_vkDestroyImage = nullptr;
PFN_vkCreateImageView                       g_Original_vkCreateImageView = nullptr;
PFN_vkDestroyImageView                      g_Original_vkDestroyImageView = nullptr;
PFN_vkCreateFramebuffer                     g_Original_vkCreateFramebuffer = nullptr;
PFN_vkDestroyFramebuffer                    g_Original_vkDestroyFramebuffer = nullptr;
PFN_vkCreateRenderPass                      g_Original_vkCreateRenderPass = nullptr;
PFN_vkCreateRenderPass2                     g_Original_vkCreateRenderPass2 = nullptr;
PFN_vkCreateRenderPass2                     g_Original_vkCreateRenderPass2KHR = nullptr;
PFN_vkDestroyRenderPass                     g_Original_vkDestroyRenderPass = nullptr;
PFN_vkEnumeratePhysicalDevices              g_Original_vkEnumeratePhysicalDevices = nullptr;
PFN_vkGetPhysicalDeviceProperties           g_Original_vkGetPhysicalDeviceProperties = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties     g_Original_vkGetPhysicalDeviceMemoryProperties = nullptr;
PFN_vkCmdResolveImage                       g_Original_vkCmdResolveImage = nullptr;
static std::atomic<void*>                   g_pendingResolveColorImage{ nullptr };

std::mutex         g_vkHookMutex;
std::vector<void*> g_HookedVulkanTargets;

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName);
void* Hooked_vkGetInstanceProcAddr(void* instance, const char* pName);
int   Hooked_vkCreateDevice(void* physicalDevice, const void* pCreateInfo, const void* pAllocator, void** pDevice);
void  Hooked_vkDestroySwapchainKHR(void* device, void* swapchain, const void* pAllocator);
int   Hooked_vkGetSwapchainImagesKHR(void* device, void* swapchain, uint32_t* pSwapchainImageCount, void** pSwapchainImages);
int   Hooked_vkAcquireNextImageKHR(void* device, void* swapchain, uint64_t timeout, void* semaphore, void* fence, uint32_t* pImageIndex);
int   Hooked_vkAcquireNextImage2KHR(void* device, const MinimalVkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex);
void  Hooked_vkCmdSetViewport(void* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const MinimalVkViewport* pViewports);
void  Hooked_vkCmdSetScissor(void* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const MinimalVkRect2D* pScissors);
void  Hooked_vkCmdBeginRenderPass(void* commandBuffer, const void* pRenderPassBegin, uint32_t contents);
void  Hooked_vkCmdEndRenderPass(void* commandBuffer);
void  Hooked_vkCmdDraw(void* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void  Hooked_vkCmdDrawIndexed(void* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void  Hooked_vkCmdSetSampleLocationsEXT(void* commandBuffer, const MinimalVkSampleLocationsInfoEXT* pSampleLocationsInfo);
int   Hooked_vkCreateSampler(void* device, const MinimalVkSamplerCreateInfo* pCreateInfo, const void* pAllocator, void* pSampler);
int   Hooked_vkCreateImage(void* device, const MinimalVkImageCreateInfo* pCreateInfo, const void* pAllocator, void** pImage);
void  Hooked_vkDestroyImage(void* device, void* image, const void* pAllocator);
int   Hooked_vkCreateImageView(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView);
void  Hooked_vkDestroyImageView(void* device, void* imageView, const void* pAllocator);
int   Hooked_vkCreateFramebuffer(void* device, const MinimalVkFramebufferCreateInfo* pCreateInfo, const void* pAllocator, void** pFramebuffer);
void  Hooked_vkDestroyFramebuffer(void* device, void* framebuffer, const void* pAllocator);
int   Hooked_vkCreateRenderPass(void* device, const MinimalVkRenderPassCreateInfo* pCreateInfo, const void* pAllocator, void** pRenderPass);
int   Hooked_vkCreateRenderPass2(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass);
int   Hooked_vkCreateRenderPass2KHR(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass);
void  Hooked_vkDestroyRenderPass(void* device, void* renderPass, const void* pAllocator);
int   Hooked_vkEnumeratePhysicalDevices(void* instance, uint32_t* pPhysicalDeviceCount, void** pPhysicalDevices);
void  Hooked_vkGetPhysicalDeviceProperties(void* physicalDevice, void* pProperties);
void  Hooked_vkGetPhysicalDeviceMemoryProperties(void* physicalDevice, void* pMemoryProperties);
void  Hooked_vkCmdResolveImage(void* commandBuffer, void* srcImage, uint32_t srcImageLayout, void* dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions);

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

struct MinimalVkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
};

struct MinimalVkImageCreateInfo {
    uint32_t          sType;
    const void*       pNext;
    uint32_t          flags;
    uint32_t          imageType;
    uint32_t          format;
    MinimalVkExtent3D extent;
    uint32_t          mipLevels;
    uint32_t          arrayLayers;
    uint32_t          samples;
    uint32_t          tiling;
    uint32_t          usage;
};
static_assert(offsetof(MinimalVkImageCreateInfo, sType) == 0, "ABI drift");
static_assert(offsetof(MinimalVkImageCreateInfo, format) == (sizeof(void*) == 8 ? 24 : 16), "ABI drift: format offset");
static_assert(offsetof(MinimalVkImageCreateInfo, extent) == (sizeof(void*) == 8 ? 28 : 20), "ABI drift: extent offset");
static_assert(offsetof(MinimalVkImageCreateInfo, samples) == (sizeof(void*) == 8 ? 48 : 40), "ABI drift: samples offset");
static_assert(offsetof(MinimalVkImageCreateInfo, usage) == (sizeof(void*) == 8 ? 56 : 48), "ABI drift: usage offset");

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

struct MinimalVkAttachmentDescription {
    uint32_t flags;
    int32_t  format;
    uint32_t samples;
    uint32_t loadOp;
    uint32_t storeOp;
    uint32_t stencilLoadOp;
    uint32_t stencilStoreOp;
    uint32_t initialLayout;
    uint32_t finalLayout;
};
static_assert(sizeof(MinimalVkAttachmentDescription) == 36, "VkAttachmentDescription size mismatch");

struct MinimalVkRenderPassCreateInfo {
    uint32_t                             sType;
    const void*                          pNext;
    uint32_t                             flags;
    uint32_t                             attachmentCount;
    const MinimalVkAttachmentDescription* pAttachments;
    uint32_t                             subpassCount;
    const void*                          pSubpasses;
    uint32_t                             dependencyCount;
    const void*                          pDependencies;
};

struct MinimalVkAttachmentDescription2 {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    int32_t     format;
    uint32_t    samples;
    uint32_t    loadOp;
    uint32_t    storeOp;
    uint32_t    stencilLoadOp;
    uint32_t    stencilStoreOp;
    uint32_t    initialLayout;
    uint32_t    finalLayout;
};

struct MinimalVkRenderPassCreateInfo2 {
    uint32_t                              sType;
    const void*                           pNext;
    uint32_t                              flags;
    uint32_t                              attachmentCount;
    const MinimalVkAttachmentDescription2* pAttachments;
};

struct MinimalVkRenderPassAttachmentBeginInfo {
    uint32_t           sType;
    const void*        pNext;
    uint32_t           attachmentCount;
    const void* const* pAttachments;
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
static_assert(offsetof(MinimalVkRenderPassCreateInfo, attachmentCount) == 20, "ABI drift: rp attachmentCount");
static_assert(offsetof(MinimalVkRenderPassCreateInfo, pAttachments) == 24, "ABI drift: rp pAttachments");
static_assert(offsetof(MinimalVkRenderPassCreateInfo2, attachmentCount) == 20, "ABI drift: rp2 attachmentCount");
static_assert(offsetof(MinimalVkRenderPassCreateInfo2, pAttachments) == 24, "ABI drift: rp2 pAttachments");
static_assert(offsetof(MinimalVkRenderPassAttachmentBeginInfo, attachmentCount) == 16, "ABI drift: rpAttBegin count");
static_assert(offsetof(MinimalVkRenderPassAttachmentBeginInfo, pAttachments) == 24, "ABI drift: rpAttBegin pAttachments");
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

inline bool IsSwapchainImage(const void* vkImage) {
    if (!vkImage) return false;
    std::lock_guard<std::mutex> lock(g_swapchainMutex);
    for (const auto& entry : g_swapchainImages) {
        for (const void* img : entry.second) {
            if (img == vkImage) return true;
        }
    }
    return false;
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

struct TrackedRenderPassAttachment {
    int32_t  format{ 0 };
    uint32_t samples{ 1 };
    uint32_t finalLayout{ 2 };
};

struct TrackedRenderPass {
    bool                                     isCandidateGeometryPass{ false };
    std::vector<TrackedRenderPassAttachment> attachments;
};

std::mutex g_attachmentMutex;
std::unordered_map<void*, VulkanImageMetadata> g_images;
std::unordered_map<void*, TrackedImageView> g_imageViews;
std::unordered_map<void*, TrackedFramebuffer> g_framebuffers;
std::unordered_map<void*, TrackedRenderPass> g_renderPasses;

struct ResolvedSceneAttachments {
    void*               colorImage{ nullptr };
    void*               resolveColorImage{ nullptr };
    void*               depthImage{ nullptr };
    void*               velocityImage{ nullptr };
    uint32_t            colorAspect{ 1 };
    uint32_t            depthAspect{ 2 };
    uint32_t            velocityAspect{ 1 };
    uint32_t            colorFinalLayout{ 2 };
    uint32_t            resolveFinalLayout{ 2 };
    uint32_t            depthFinalLayout{ 3 };
    uint32_t            velocityFinalLayout{ 2 };
    VulkanImageMetadata colorMeta{};
    VulkanImageMetadata resolveColorMeta{};
    VulkanImageMetadata depthMeta{};
    VulkanImageMetadata velocityMeta{};
    bool                colorIsSwapchain{ false };
    bool                rpTracked{ false };
    bool                rpCandidateGeometryPass{ true };
};

inline bool IsVelocityCandidateFormat(int32_t fmt) {
    // 2-component velocity vector formats: RG16_UNORM(78)/SNORM(79)/SFLOAT(83)/SINT(84), RG32_SFLOAT(103)/SINT(104)
    return fmt == 78 || fmt == 79 || fmt == 83 || fmt == 84 || fmt == 103 || fmt == 104;
}

inline ResolvedSceneAttachments ResolveAttachmentViews(const std::vector<void*>& overrideViews,
                                                      void* framebuffer,
                                                      void* renderPass) {
    ResolvedSceneAttachments resolved{};

    struct ViewCandidate {
        void*               image{ nullptr };
        int32_t             format{ 0 };
        uint32_t            aspectMask{ 0 };
        uint32_t            finalLayout{ 0 };
        bool                hasFinalLayout{ false };
        VulkanImageMetadata meta{};
    };
    std::vector<ViewCandidate> candidates;
    {
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        const TrackedFramebuffer* fbPtr = nullptr;
        if (framebuffer) {
            auto fbit = g_framebuffers.find(framebuffer);
            if (fbit != g_framebuffers.end()) {
                fbPtr = &fbit->second;
            }
        }
        const TrackedRenderPass* rpPtr = nullptr;
        if (renderPass) {
            auto rpit = g_renderPasses.find(renderPass);
            if (rpit != g_renderPasses.end()) {
                rpPtr = &rpit->second;
                resolved.rpTracked = true;
                resolved.rpCandidateGeometryPass = rpPtr->isCandidateGeometryPass;
            }
        }

        const std::vector<void*>* views = !overrideViews.empty() ? &overrideViews : (fbPtr ? &fbPtr->attachments : nullptr);
        if (!views) {
            return resolved;
        }
        candidates.reserve(views->size());
        for (size_t idx = 0; idx < views->size(); ++idx) {
            void* attView = (*views)[idx];
            auto vit = g_imageViews.find(attView);
            if (vit == g_imageViews.end() || !vit->second.image) continue;
            ViewCandidate c{};
            c.image = vit->second.image;
            c.format = vit->second.format;
            c.aspectMask = vit->second.aspectMask;
            auto iit = g_images.find(c.image);
            if (iit != g_images.end()) {
                c.meta = iit->second;
            }
            if (c.meta.format == 0 && c.format > 0) {
                c.meta.format = static_cast<uint32_t>(c.format);
            }
            if (c.meta.width == 0 && fbPtr && fbPtr->width > 0) {
                c.meta.width = fbPtr->width;
                c.meta.height = fbPtr->height;
            }
            if (rpPtr && idx < rpPtr->attachments.size()) {
                c.finalLayout = rpPtr->attachments[idx].finalLayout;
                c.hasFinalLayout = true;
                if (c.meta.format == 0 && rpPtr->attachments[idx].format > 0) {
                    c.meta.format = static_cast<uint32_t>(rpPtr->attachments[idx].format);
                }
                if (c.meta.samples == 0 && rpPtr->attachments[idx].samples > 0) {
                    c.meta.samples = rpPtr->attachments[idx].samples;
                }
            }
            candidates.push_back(c);
        }
    }

    // Check swapchain membership outside g_attachmentMutex to preserve strict lock hierarchy
    for (const auto& c : candidates) {
        const int32_t effectiveFormat = (c.format != 0) ? c.format : static_cast<int32_t>(c.meta.format);
        const bool isDepth = RenderTargetManager::IsVulkanDepthFormat(effectiveFormat) || ((c.aspectMask & 2u) != 0u);
        if (isDepth) {
            if (!resolved.depthImage) {
                resolved.depthImage = c.image;
                resolved.depthAspect = (c.aspectMask != 0u) ? c.aspectMask : 2u;
                resolved.depthFinalLayout = (c.hasFinalLayout && c.finalLayout != 0u) ? c.finalLayout : 3u;
                resolved.depthMeta = c.meta;
            }
            continue;
        }

        const bool isSwapchain = IsSwapchainImage(c.image);
        if (isSwapchain) {
            if (!resolved.colorImage) {
                resolved.colorImage = c.image;
                resolved.colorAspect = (c.aspectMask != 0u) ? c.aspectMask : 1u;
                resolved.colorFinalLayout = (c.hasFinalLayout && c.finalLayout != 0u) ? c.finalLayout : 2u;
                resolved.colorMeta = c.meta;
                resolved.colorIsSwapchain = true;
            }
            continue;
        }

        const uint32_t attLayout = (c.hasFinalLayout && c.finalLayout != 0u) ? c.finalLayout : 2u;
        const bool isVelFormat = IsVelocityCandidateFormat(effectiveFormat);

        // Prefer multi-sampled / first non-swapchain offscreen HDR scene color attachment over swapchain backbuffers
        if (!resolved.colorImage || resolved.colorIsSwapchain || (c.meta.samples > 1u && resolved.colorMeta.samples <= 1u)) {
            if (resolved.colorImage && !resolved.colorIsSwapchain && resolved.colorMeta.samples <= 1u && !resolved.resolveColorImage) {
                resolved.resolveColorImage = resolved.colorImage;
                resolved.resolveFinalLayout = resolved.colorFinalLayout;
                resolved.resolveColorMeta = resolved.colorMeta;
            }
            resolved.colorImage = c.image;
            resolved.colorAspect = (c.aspectMask != 0u) ? c.aspectMask : 1u;
            resolved.colorFinalLayout = attLayout;
            resolved.colorMeta = c.meta;
            resolved.colorIsSwapchain = false;
        } else if (isVelFormat && !resolved.velocityImage) {
            resolved.velocityImage = c.image;
            resolved.velocityAspect = (c.aspectMask != 0u) ? c.aspectMask : 1u;
            resolved.velocityFinalLayout = attLayout;
            resolved.velocityMeta = c.meta;
        } else if (c.meta.samples <= 1u && !resolved.resolveColorImage && resolved.colorMeta.samples > 1u) {
            // Track 1x single-sample resolved offscreen color attachment separately from MSAA capture source (Item C-04)
            resolved.resolveColorImage = c.image;
            resolved.resolveFinalLayout = attLayout;
            resolved.resolveColorMeta = c.meta;
        } else if (!resolved.velocityImage) {
            resolved.velocityImage = c.image;
            resolved.velocityAspect = (c.aspectMask != 0u) ? c.aspectMask : 1u;
            resolved.velocityFinalLayout = attLayout;
            resolved.velocityMeta = c.meta;
        }
    }
    return resolved;
}

inline ResolvedSceneAttachments ResolveFramebufferAttachments(void* framebuffer, void* renderPass = nullptr) {
    return ResolveAttachmentViews({}, framebuffer, renderPass);
}

inline VulkanImageMetadata LookupImageMetadata(const void* vkImage) {
    if (!vkImage) return {};
    std::lock_guard<std::mutex> lock(g_attachmentMutex);
    auto it = g_images.find(const_cast<void*>(vkImage));
    if (it != g_images.end()) {
        return it->second;
    }
    return {};
}

// Scene pass identification and draw tracking using lock-free slot pool
struct PassSlot {
    std::atomic<void*>    cmdBuffer{ nullptr };
    std::atomic<bool>     hasMatchingExtent{ false };
    std::atomic<bool>     viewportDownscaled{ false };
    std::atomic<uint32_t> drawCount{ 0 };
    std::atomic<void*>    activeFramebuffer{ nullptr };
    std::atomic<void*>    activeRenderPass{ nullptr };
    std::atomic<uint32_t> renderWidth{ 0 };
    std::atomic<uint32_t> renderHeight{ 0 };
    std::atomic<void*>    sceneColorImage{ nullptr };
    std::atomic<void*>    sceneResolveColorImage{ nullptr };
    std::atomic<void*>    sceneDepthImage{ nullptr };
    std::atomic<void*>    sceneVelocityImage{ nullptr };
    std::atomic<uint32_t> colorAspect{ 1 };
    std::atomic<uint32_t> depthAspect{ 2 };
    std::atomic<uint32_t> velocityAspect{ 1 };
    std::atomic<uint32_t> colorFinalLayout{ 2 };
    std::atomic<uint32_t> resolveFinalLayout{ 2 };
    std::atomic<uint32_t> depthFinalLayout{ 3 };
    std::atomic<uint32_t> velocityFinalLayout{ 2 };
    std::atomic<bool>     colorIsSwapchain{ false };
    std::atomic<bool>     rpCandidateGeometryPass{ true };
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

    g_pendingResolveColorImage.store(nullptr, std::memory_order_relaxed);

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
            ReconstructionPass::Get().ResetLayoutTransitions();
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

int Hooked_vkAcquireNextImage2KHR(void* device, const MinimalVkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) {
    if (!g_Original_vkAcquireNextImage2KHR) {
        return kVkErrorInitializationFailed;
    }
    int result = g_Original_vkAcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
    if (result == 0 && pImageIndex) {
        constexpr uint32_t kVkStructureTypeAcquireNextImageInfoKHR = 1000060010u;
        if (pAcquireInfo && pAcquireInfo->sType == kVkStructureTypeAcquireNextImageInfoKHR && pAcquireInfo->swapchain) {
            g_activeSwapchain.store(pAcquireInfo->swapchain);
        }
        g_activeImageIndex.store(*pImageIndex);
    }
    return result;
}

int Hooked_vkCreateImage(void* device, const MinimalVkImageCreateInfo* pCreateInfo, const void* pAllocator, void** pImage) {
    if (!g_Original_vkCreateImage) {
        return kVkErrorInitializationFailed;
    }
    const int result = g_Original_vkCreateImage(device, pCreateInfo, pAllocator, pImage);
    if (result == 0 && pImage && *pImage && pCreateInfo) {
        uint32_t sType = 0;
        std::memcpy(&sType, pCreateInfo, sizeof(sType));
        constexpr uint32_t kVkStructureTypeImageCreateInfo = 14;
        if (sType == kVkStructureTypeImageCreateInfo) {
            MinimalVkImageCreateInfo info{};
            std::memcpy(&info, pCreateInfo, sizeof(info));
            VulkanImageMetadata meta{};
            meta.width = info.extent.width;
            meta.height = info.extent.height;
            meta.format = info.format;
            meta.samples = info.samples;
            meta.usage = info.usage;
            std::lock_guard<std::mutex> lock(g_attachmentMutex);
            g_images[*pImage] = meta;
        }
    }
    return result;
}

void Hooked_vkDestroyImage(void* device, void* image, const void* pAllocator) {
    if (image) {
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        g_images.erase(image);
    }
    if (g_Original_vkDestroyImage) {
        g_Original_vkDestroyImage(device, image, pAllocator);
    }
}

int Hooked_vkCreateImageView(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView) {
    if (!g_Original_vkCreateImageView) {
        return kVkErrorInitializationFailed;
    }
    const int result = g_Original_vkCreateImageView(device, pCreateInfo, pAllocator, pView);
    if (result == 0 && pView && *pView && pCreateInfo) {
        uint32_t sType = 0;
        std::memcpy(&sType, pCreateInfo, sizeof(sType));
        constexpr uint32_t kVkStructureTypeImageViewCreateInfo = 15;
        if (sType == kVkStructureTypeImageViewCreateInfo) {
            MinimalVkImageViewCreateInfo info{};
            std::memcpy(&info, pCreateInfo, sizeof(info));
            std::lock_guard<std::mutex> lock(g_attachmentMutex);
            g_imageViews[*pView] = { info.image, info.format, info.subresourceRange.aspectMask };
        }
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
        uint32_t sType = 0;
        std::memcpy(&sType, pCreateInfo, sizeof(sType));
        constexpr uint32_t kVkStructureTypeFramebufferCreateInfo = 37;
        if (sType == kVkStructureTypeFramebufferCreateInfo) {
            MinimalVkFramebufferCreateInfo info{};
            std::memcpy(&info, pCreateInfo, sizeof(info));
            TrackedFramebuffer fb{};
            fb.width = info.width;
            fb.height = info.height;
            if ((info.flags & 0x1u) == 0u && info.attachmentCount > 0 && info.attachmentCount < 32 && info.pAttachments) {
                for (uint32_t i = 0; i < info.attachmentCount; ++i) {
                    void* attView = nullptr;
                    std::memcpy(&attView, &info.pAttachments[i], sizeof(attView));
                    fb.attachments.push_back(attView);
                }
            }
            std::lock_guard<std::mutex> lock(g_attachmentMutex);
            g_framebuffers[*pFramebuffer] = std::move(fb);
        }
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

inline void RecordTrackedRenderPass(void* rpHandle, const std::vector<TrackedRenderPassAttachment>& atts) {
    TrackedRenderPass rp{};
    rp.attachments = atts;
    bool hasHdrColor = false;
    bool hasDepth = false;
    bool hasMsaa2x = false;
    bool hasVelocityFmt = false;
    uint32_t colorCount = 0;
    for (const auto& att : rp.attachments) {
        if (RenderTargetManager::IsVulkanDepthFormat(att.format)) {
            hasDepth = true;
        } else {
            ++colorCount;
            if (att.format == 97 /* R16G16B16A16_SFLOAT */ ||
                att.format == 122 /* B10G11R11_UFLOAT_PACK32 */ ||
                att.format == 109 /* R32G32B32A32_SFLOAT */) {
                hasHdrColor = true;
            }
            if (IsVelocityCandidateFormat(att.format)) {
                hasVelocityFmt = true;
            }
            if (att.samples == 2u) {
                hasMsaa2x = true;
            }
        }
    }
    rp.isCandidateGeometryPass = hasHdrColor && hasDepth && (hasMsaa2x || hasVelocityFmt || colorCount >= 2u);
    std::lock_guard<std::mutex> lock(g_attachmentMutex);
    g_renderPasses[rpHandle] = std::move(rp);
}

int Hooked_vkCreateRenderPass(void* device, const MinimalVkRenderPassCreateInfo* pCreateInfo, const void* pAllocator, void** pRenderPass) {
    if (!g_Original_vkCreateRenderPass) {
        return kVkErrorInitializationFailed;
    }
    const int result = g_Original_vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    if (result == 0 && pRenderPass && *pRenderPass && pCreateInfo) {
        uint32_t sType = 0;
        std::memcpy(&sType, pCreateInfo, sizeof(sType));
        constexpr uint32_t kVkStructureTypeRenderPassCreateInfo = 38;
        if (sType == kVkStructureTypeRenderPassCreateInfo) {
            MinimalVkRenderPassCreateInfo info{};
            std::memcpy(&info, pCreateInfo, sizeof(info));
            if (info.attachmentCount < 64 && (!info.attachmentCount || info.pAttachments)) {
                std::vector<TrackedRenderPassAttachment> atts;
                atts.reserve(info.attachmentCount);
                for (uint32_t i = 0; i < info.attachmentCount; ++i) {
                    MinimalVkAttachmentDescription desc{};
                    std::memcpy(&desc, &info.pAttachments[i], sizeof(desc));
                    atts.push_back({ desc.format, desc.samples, desc.finalLayout });
                }
                RecordTrackedRenderPass(*pRenderPass, atts);
            }
        }
    }
    return result;
}

template <typename Fn>
inline int HandleCreateRenderPass2(Fn origFn, void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass) {
    if (!origFn) {
        return kVkErrorInitializationFailed;
    }
    const int result = origFn(device, pCreateInfo, pAllocator, pRenderPass);
    if (result == 0 && pRenderPass && *pRenderPass && pCreateInfo) {
        uint32_t sType = 0;
        std::memcpy(&sType, pCreateInfo, sizeof(sType));
        constexpr uint32_t kVkStructureTypeRenderPassCreateInfo2 = 1000109004;
        if (sType == kVkStructureTypeRenderPassCreateInfo2) {
            MinimalVkRenderPassCreateInfo2 info{};
            std::memcpy(&info, pCreateInfo, sizeof(info));
            if (info.attachmentCount < 64 && (!info.attachmentCount || info.pAttachments)) {
                std::vector<TrackedRenderPassAttachment> atts;
                atts.reserve(info.attachmentCount);
                for (uint32_t i = 0; i < info.attachmentCount; ++i) {
                    MinimalVkAttachmentDescription2 desc{};
                    std::memcpy(&desc, &info.pAttachments[i], sizeof(desc));
                    atts.push_back({ desc.format, desc.samples, desc.finalLayout });
                }
                RecordTrackedRenderPass(*pRenderPass, atts);
            }
        }
    }
    return result;
}

int Hooked_vkCreateRenderPass2(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass) {
    return HandleCreateRenderPass2(g_Original_vkCreateRenderPass2, device, pCreateInfo, pAllocator, pRenderPass);
}

int Hooked_vkCreateRenderPass2KHR(void* device, const void* pCreateInfo, const void* pAllocator, void** pRenderPass) {
    return HandleCreateRenderPass2(g_Original_vkCreateRenderPass2KHR, device, pCreateInfo, pAllocator, pRenderPass);
}

void Hooked_vkDestroyRenderPass(void* device, void* renderPass, const void* pAllocator) {
    if (renderPass) {
        std::lock_guard<std::mutex> lock(g_attachmentMutex);
        g_renderPasses.erase(renderPass);
    }
    if (g_Original_vkDestroyRenderPass) {
        g_Original_vkDestroyRenderPass(device, renderPass, pAllocator);
    }
}

inline void CapturePhysicalDeviceIfUnset(void* physDev) {
    if (!physDev) return;
    void* expected = nullptr;
    if (g_activePhysicalDevice.compare_exchange_strong(expected, physDev)) {
        CBR_LOG_INFO("Late Vulkan physical device captured: %p (Item H-06)", physDev);
    }
}

int Hooked_vkEnumeratePhysicalDevices(void* instance, uint32_t* pPhysicalDeviceCount, void** pPhysicalDevices) {
    if (!g_Original_vkEnumeratePhysicalDevices) {
        return kVkErrorInitializationFailed;
    }
    const int result = g_Original_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if ((result == 0 || result == 5 /* VK_INCOMPLETE */) &&
        pPhysicalDeviceCount && *pPhysicalDeviceCount > 0 && pPhysicalDevices &&
        g_activePhysicalDevice.load() == nullptr) {
        void* chosen = pPhysicalDevices[0];
        if (*pPhysicalDeviceCount > 1 && g_Original_vkGetPhysicalDeviceProperties) {
            for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
                if (!pPhysicalDevices[i]) continue;
                uint32_t propsHeader[256]{};
                g_Original_vkGetPhysicalDeviceProperties(pPhysicalDevices[i], propsHeader);
                if (propsHeader[4] == 2u /* VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU */) {
                    chosen = pPhysicalDevices[i];
                    break;
                }
            }
        }
        CapturePhysicalDeviceIfUnset(chosen);
    }
    return result;
}

void Hooked_vkGetPhysicalDeviceProperties(void* physicalDevice, void* pProperties) {
    CapturePhysicalDeviceIfUnset(physicalDevice);
    if (g_Original_vkGetPhysicalDeviceProperties) {
        g_Original_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    }
}

void Hooked_vkGetPhysicalDeviceMemoryProperties(void* physicalDevice, void* pMemoryProperties) {
    CapturePhysicalDeviceIfUnset(physicalDevice);
    if (g_Original_vkGetPhysicalDeviceMemoryProperties) {
        g_Original_vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
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
            // Ensure projection matrix jitter is applied immediately prior to scene geometry draws (Item H-01)
            if (config.jitterDirection != 0) {
                PatternScanner::UpdateCameraJitter(currentFrame, true);
            }
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
        constexpr uint32_t kVkStructureTypeRenderPassAttachmentBeginInfo = 1000108003;
        if (sType == kVkStructureTypeRenderPassBeginInfo) {
            MinimalVkRenderPassBeginInfo info{};
            std::memcpy(&info, pRenderPassBegin, sizeof(info));
            const auto dims = RenderTargetManager::Get().GetDimensions();
            const uint32_t w = info.renderArea.extent.width;
            const uint32_t h = info.renderArea.extent.height;
            const bool matching = ((w == dims.fullWidth && h == dims.fullHeight) ||
                                   (w == dims.quarterWidth && h == dims.quarterHeight));

            if (matching) {
                const auto config = CBREngine::Get().GetCachedFrameConfig();
                if (config.samplingStrategy == SamplingStrategy::StrategyA && config.jitterDirection != 0) {
                    PatternScanner::UpdateCameraJitter(CBREngine::Get().GetCurrentFrameIndex(), true);
                }
                float* camProj = PatternScanner::GetCameraProjectionMatrix();
                if (camProj) {
                    ReconstructionPass::Get().SetCameraMatrices(camProj, nullptr);
                }
            }

            // Support VK_KHR_imageless_framebuffer / Vulkan 1.2 VkRenderPassAttachmentBeginInfo in pNext chain (Item C-01)
            std::vector<void*> overrideViews;
            if (matching) {
                const void* pNextCursor = info.pNext;
                for (int hop = 0; hop < 8 && pNextCursor != nullptr; ++hop) {
                    uint32_t extSType = 0;
                    std::memcpy(&extSType, pNextCursor, sizeof(extSType));
                    if (extSType == kVkStructureTypeRenderPassAttachmentBeginInfo) {
                        MinimalVkRenderPassAttachmentBeginInfo attBegin{};
                        std::memcpy(&attBegin, pNextCursor, sizeof(attBegin));
                        if (attBegin.attachmentCount > 0 && attBegin.attachmentCount < 32 && attBegin.pAttachments) {
                            overrideViews.reserve(attBegin.attachmentCount);
                            for (uint32_t ai = 0; ai < attBegin.attachmentCount; ++ai) {
                                void* viewPtr = nullptr;
                                std::memcpy(&viewPtr, &attBegin.pAttachments[ai], sizeof(viewPtr));
                                overrideViews.push_back(viewPtr);
                            }
                        }
                        break;
                    }
                    const void* nextPtr = nullptr;
                    std::memcpy(&nextPtr, reinterpret_cast<const uint8_t*>(pNextCursor) + offsetof(MinimalVkRenderPassAttachmentBeginInfo, pNext), sizeof(nextPtr));
                    pNextCursor = nextPtr;
                }
            }

            PassSlot* slot = GetOrCreatePassSlot(commandBuffer);
            if (slot) {
                ResolvedSceneAttachments resolved{};
                if (matching && (info.framebuffer || !overrideViews.empty())) {
                    resolved = ResolveAttachmentViews(overrideViews, info.framebuffer, info.renderPass);
                } else if (info.renderPass) {
                    resolved = ResolveAttachmentViews({}, nullptr, info.renderPass);
                }

                slot->hasMatchingExtent.store(matching, std::memory_order_release);
                slot->viewportDownscaled.store(false, std::memory_order_release);
                slot->drawCount.store(0, std::memory_order_release);
                slot->activeFramebuffer.store(info.framebuffer, std::memory_order_release);
                slot->activeRenderPass.store(info.renderPass, std::memory_order_release);
                slot->renderWidth.store(w, std::memory_order_release);
                slot->renderHeight.store(h, std::memory_order_release);
                slot->sceneColorImage.store(resolved.colorImage, std::memory_order_release);
                slot->sceneResolveColorImage.store(resolved.resolveColorImage, std::memory_order_release);
                slot->sceneDepthImage.store(resolved.depthImage, std::memory_order_release);
                slot->sceneVelocityImage.store(resolved.velocityImage, std::memory_order_release);
                slot->colorAspect.store(resolved.colorAspect, std::memory_order_release);
                slot->depthAspect.store(resolved.depthAspect, std::memory_order_release);
                slot->velocityAspect.store(resolved.velocityAspect, std::memory_order_release);
                slot->colorFinalLayout.store(resolved.colorFinalLayout, std::memory_order_release);
                slot->resolveFinalLayout.store(resolved.resolveFinalLayout, std::memory_order_release);
                slot->depthFinalLayout.store(resolved.depthFinalLayout, std::memory_order_release);
                slot->velocityFinalLayout.store(resolved.velocityFinalLayout, std::memory_order_release);
                slot->colorIsSwapchain.store(resolved.colorIsSwapchain, std::memory_order_release);
                slot->rpCandidateGeometryPass.store(!resolved.rpTracked || resolved.rpCandidateGeometryPass, std::memory_order_release);
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
            void* activeRp = nullptr;
            uint32_t rw = 0, rh = 0;
            void* gameColorImage = nullptr;
            void* gameResolveColorImage = nullptr;
            void* gameDepthImage = nullptr;
            void* gameVelocityImage = nullptr;
            uint32_t colorAspect = 1;
            uint32_t depthAspect = 2;
            uint32_t velocityAspect = 1;
            uint32_t colorFinalLayout = 2;
            uint32_t resolveFinalLayout = 2;
            uint32_t depthFinalLayout = 3;
            uint32_t velocityFinalLayout = 2;
            bool colorIsSwapchain = false;
            bool rpCandidate = true;

            PassSlot* slot = FindPassSlot(commandBuffer);
            if (slot) {
                const bool downscaled = slot->viewportDownscaled.load(std::memory_order_acquire);
                const bool matching = slot->hasMatchingExtent.load(std::memory_order_acquire);
                const uint32_t draws = slot->drawCount.load(std::memory_order_acquire);
                activeFb = slot->activeFramebuffer.load(std::memory_order_acquire);
                activeRp = slot->activeRenderPass.load(std::memory_order_acquire);
                rw = slot->renderWidth.load(std::memory_order_acquire);
                rh = slot->renderHeight.load(std::memory_order_acquire);
                gameColorImage = slot->sceneColorImage.load(std::memory_order_acquire);
                gameResolveColorImage = slot->sceneResolveColorImage.load(std::memory_order_acquire);
                gameDepthImage = slot->sceneDepthImage.load(std::memory_order_acquire);
                gameVelocityImage = slot->sceneVelocityImage.load(std::memory_order_acquire);
                colorAspect = slot->colorAspect.load(std::memory_order_acquire);
                depthAspect = slot->depthAspect.load(std::memory_order_acquire);
                velocityAspect = slot->velocityAspect.load(std::memory_order_acquire);
                colorFinalLayout = slot->colorFinalLayout.load(std::memory_order_acquire);
                resolveFinalLayout = slot->resolveFinalLayout.load(std::memory_order_acquire);
                depthFinalLayout = slot->depthFinalLayout.load(std::memory_order_acquire);
                velocityFinalLayout = slot->velocityFinalLayout.load(std::memory_order_acquire);
                colorIsSwapchain = slot->colorIsSwapchain.load(std::memory_order_acquire);
                rpCandidate = slot->rpCandidateGeometryPass.load(std::memory_order_acquire);

                // Gate on HDR+depth RenderPass candidate (when tracked) AND >= 250 draws to ignore early shadow/depth pre-passes (Item H-03)
                isScenePass = (downscaled || matching) && rpCandidate && (draws >= 250u) && !colorIsSwapchain;

                // Release slot back to pool
                slot->hasMatchingExtent.store(false, std::memory_order_relaxed);
                slot->viewportDownscaled.store(false, std::memory_order_relaxed);
                slot->drawCount.store(0, std::memory_order_relaxed);
                slot->activeFramebuffer.store(nullptr, std::memory_order_relaxed);
                slot->activeRenderPass.store(nullptr, std::memory_order_relaxed);
                slot->renderWidth.store(0, std::memory_order_relaxed);
                slot->renderHeight.store(0, std::memory_order_relaxed);
                slot->sceneColorImage.store(nullptr, std::memory_order_relaxed);
                slot->sceneResolveColorImage.store(nullptr, std::memory_order_relaxed);
                slot->sceneDepthImage.store(nullptr, std::memory_order_relaxed);
                slot->sceneVelocityImage.store(nullptr, std::memory_order_relaxed);
                slot->colorIsSwapchain.store(false, std::memory_order_relaxed);
                slot->rpCandidateGeometryPass.store(true, std::memory_order_relaxed);
                slot->cmdBuffer.store(nullptr, std::memory_order_release);
            }

            // Always consume any candidate pass marker for this commandBuffer, while requiring >= 250 draws before dispatch (Item H-03)
            CBREngine::Get().ConsumeCandidatePassActive(commandBuffer);
            if (isScenePass && !colorIsSwapchain) {
                if (!gameColorImage && activeFb) {
                    const ResolvedSceneAttachments fallback = ResolveFramebufferAttachments(activeFb, activeRp);
                    gameColorImage = fallback.colorImage;
                    gameResolveColorImage = fallback.resolveColorImage;
                    gameDepthImage = fallback.depthImage;
                    gameVelocityImage = fallback.velocityImage;
                    colorAspect = fallback.colorAspect;
                    depthAspect = fallback.depthAspect;
                    velocityAspect = fallback.velocityAspect;
                    colorFinalLayout = fallback.colorFinalLayout;
                    resolveFinalLayout = fallback.resolveFinalLayout;
                    depthFinalLayout = fallback.depthFinalLayout;
                    velocityFinalLayout = fallback.velocityFinalLayout;
                    colorIsSwapchain = fallback.colorIsSwapchain;
                }

                if (colorIsSwapchain) {
                    static std::atomic<bool> s_warnedSwapchainPass{ false };
                    if (!s_warnedSwapchainPass.exchange(true)) {
                        CBR_LOG_WARN("Skipping CBR capture/blit on swapchain backbuffer pass inside vkCmdEndRenderPass (Item C-04).");
                    }
                    return;
                }

                auto& rtm = RenderTargetManager::Get();
                if (rtm.AreVulkanResourcesCreated()) {
                    // Transition newly created images out of UNDEFINED and clear history buffers to zero before first copy/dispatch (Item C-03)
                    ReconstructionPass::Get().EnsureInitialLayoutsTransitioned(commandBuffer);

                    const auto& res = rtm.GetVulkanResources();
                    const auto dims = rtm.GetDimensions();

                    // Step 1: Copy compatible offscreen scene geometry attachments into quarterColor, quarterDepth, and velocity (Item C-01)
                    bool copiedColor = false;
                    bool copiedDepth = false;
                    if (gameColorImage) {
                        const VulkanImageMetadata colorMeta = LookupImageMetadata(gameColorImage);
                        if (rtm.CanCopyToQuarterColor(colorMeta)) {
                            ReconstructionPass::CopyImageRegion(
                                commandBuffer,
                                gameColorImage, res.quarterColor.image,
                                0, 0, 0, 0,
                                dims.quarterWidth, dims.quarterHeight,
                                colorAspect,
                                colorFinalLayout,
                                rtm.GetVulkanImageLayout(res.quarterColor.image),
                                colorFinalLayout,
                                5 /* SHADER_READ_ONLY_OPTIMAL */,
                                res.quarterColor.aspectMask
                            );
                            copiedColor = true;
                        } else {
                            static std::atomic<bool> s_warnedColorCopy{ false };
                            if (!s_warnedColorCopy.exchange(true)) {
                                CBR_LOG_WARN("vkCmdCopyImage skipped for scene color (%ux%u, samples=%u, fmt=%u): incompatible with 2x MSAA quarterColor (%ux%u).",
                                             colorMeta.width, colorMeta.height, colorMeta.samples, colorMeta.format, dims.quarterWidth, dims.quarterHeight);
                            }
                        }
                    }
                    if (gameDepthImage) {
                        const VulkanImageMetadata depthMeta = LookupImageMetadata(gameDepthImage);
                        if (rtm.CanCopyToQuarterDepth(depthMeta)) {
                            // Enforce aspectMask matches 2u (DEPTH_BIT) so D32_SFLOAT_S8_UINT depth aspect is isolated (VUID-VkImageCopy-aspectMask-00142)
                            const uint32_t srcDepthAspect = (depthAspect & 2u) ? 2u : depthAspect;
                            const uint32_t dstDepthAspect = (res.quarterDepth.aspectMask & 2u) ? 2u : res.quarterDepth.aspectMask;
                            ReconstructionPass::CopyImageRegion(
                                commandBuffer,
                                gameDepthImage, res.quarterDepth.image,
                                0, 0, 0, 0,
                                dims.quarterWidth, dims.quarterHeight,
                                srcDepthAspect,
                                depthFinalLayout,
                                rtm.GetVulkanImageLayout(res.quarterDepth.image),
                                depthFinalLayout,
                                5 /* SHADER_READ_ONLY_OPTIMAL */,
                                dstDepthAspect
                            );
                            copiedDepth = true;
                        } else {
                            static std::atomic<bool> s_warnedDepthCopy{ false };
                            if (!s_warnedDepthCopy.exchange(true)) {
                                CBR_LOG_WARN("vkCmdCopyImage skipped for scene depth (format=%u, samples=%u): incompatible with 2x MSAA D32_SFLOAT quarterDepth.",
                                             depthMeta.format, depthMeta.samples);
                            }
                        }
                    }
                    if (gameVelocityImage) {
                        const VulkanImageMetadata velMeta = LookupImageMetadata(gameVelocityImage);
                        if (rtm.CanCopyToVelocity(velMeta)) {
                            ReconstructionPass::CopyImageRegion(
                                commandBuffer,
                                gameVelocityImage, res.velocity.image,
                                0, 0, 0, 0,
                                dims.quarterWidth, dims.quarterHeight,
                                velocityAspect,
                                velocityFinalLayout,
                                rtm.GetVulkanImageLayout(res.velocity.image),
                                velocityFinalLayout,
                                5 /* SHADER_READ_ONLY_OPTIMAL */,
                                res.velocity.aspectMask
                            );
                        } else {
                            static std::atomic<bool> s_warnedVelCopy{ false };
                            if (!s_warnedVelCopy.exchange(true)) {
                                CBR_LOG_WARN("vkCmdCopyImage skipped for scene velocity (%ux%u, samples=%u): incompatible with 1x velocity buffer.",
                                             velMeta.width, velMeta.height, velMeta.samples);
                            }
                        }
                    }

                    // Strict C-01 gate: abort dispatch & blit if essential attachments were not captured
                    if (!copiedColor || !copiedDepth) {
                        static std::atomic<bool> s_warnedSkipPass{ false };
                        if (!s_warnedSkipPass.exchange(true)) {
                            CBR_LOG_WARN("Skipping CBR reconstruction & blit: required attachments not copied (copiedColor=%d, copiedDepth=%d). Preserving native rendering.",
                                         copiedColor ? 1 : 0, copiedDepth ? 1 : 0);
                        }
                        return;
                    }

                    // Step 2: Transition layouts and execute CBR reconstruction compute dispatch
                    CBREngine::Get().OnScenePassEnd(commandBuffer);

                    // Step 3: Upscale and blit reconstructed full-resolution output back ONLY into the offscreen 1x scene HDR attachment (Item C-04).
                    // Never blit to a swapchain backbuffer or multi-sampled image, and never emit PRESENT_SRC_KHR mid-frame.
                    void* blitDst = gameResolveColorImage ? gameResolveColorImage : gameColorImage;
                    const uint32_t blitDstLayout = gameResolveColorImage ? resolveFinalLayout : colorFinalLayout;
                    const VulkanImageMetadata blitDstMeta = LookupImageMetadata(blitDst);
                    const bool blitSafe = rtm.ShouldBlitReconstructionToSceneColor(
                        blitDst, IsSwapchainImage(blitDst), rw, rh, blitDstMeta);
                    if (blitSafe) {
                        g_pendingResolveColorImage.store(nullptr, std::memory_order_release);
                        // DispatchVulkan writes historyColor[1 - activeSetIdx] and OnScenePassEnd() then swaps ping-pong state,
                        // so GetCurrentHistoryIndex() here is the just-written reconstructed history buffer (Item C-02).
                        const uint32_t justWrittenIdx = rtm.GetCurrentHistoryIndex() & 1u;
                        void* blitSource = res.historyColor[justWrittenIdx].image ? res.historyColor[justWrittenIdx].image : res.outputColor.image;
                        const uint32_t blitSrcLayout = rtm.GetVulkanImageLayout(blitSource);
                        ReconstructionPass::BlitToImage(
                            commandBuffer,
                            blitSource,
                            blitDst,
                            dims.fullWidth, dims.fullHeight,
                            dims.fullWidth, dims.fullHeight,
                            blitDstLayout,
                            blitSrcLayout,
                            blitDstLayout
                        );
                    } else if (gameColorImage && !gameResolveColorImage) {
                        // Game rendered 2x MSAA without inline resolve attachment; arm vkCmdResolveImage to intercept (Item C-02)
                        g_pendingResolveColorImage.store(gameColorImage, std::memory_order_release);
                    }
                }
            }
        } catch (...) {
        }
    }
}

void Hooked_vkCmdResolveImage(void* commandBuffer, void* srcImage, uint32_t srcImageLayout, void* dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions) {
    if (!g_Original_vkCmdResolveImage) return;

    if (CBREngine::Get().IsEnabled() && srcImage) {
        void* pending = g_pendingResolveColorImage.load(std::memory_order_acquire);
        if (pending && srcImage == pending) {
            g_pendingResolveColorImage.store(nullptr, std::memory_order_release);
            auto& rtm = RenderTargetManager::Get();
            if (rtm.AreVulkanResourcesCreated()) {
                const auto& res = rtm.GetVulkanResources();
                const auto dims = rtm.GetDimensions();
                const uint32_t justWrittenIdx = rtm.GetCurrentHistoryIndex() & 1u;
                void* blitSource = res.historyColor[justWrittenIdx].image ? res.historyColor[justWrittenIdx].image : res.outputColor.image;
                if (blitSource && dstImage) {
                    const uint32_t blitSrcLayout = rtm.GetVulkanImageLayout(blitSource);
                    const VulkanImageMetadata dstMeta = LookupImageMetadata(dstImage);
                    const uint32_t dstW = (dstMeta.width > 0) ? dstMeta.width : dims.fullWidth;
                    const uint32_t dstH = (dstMeta.height > 0) ? dstMeta.height : dims.fullHeight;
                    ReconstructionPass::BlitToImage(
                        commandBuffer,
                        blitSource,
                        dstImage,
                        dims.fullWidth, dims.fullHeight,
                        dstW, dstH,
                        dstImageLayout,
                        blitSrcLayout,
                        dstImageLayout
                    );
                    return;
                }
            }
        }
    }

    g_Original_vkCmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
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
    // Apply mipLodBias selectively only to mipmapped anisotropic scene material samplers (Item M-03)
    if (pCreateInfo && config.mipLodBias != 0.0f &&
        pCreateInfo->maxLod > 1.0f && pCreateInfo->anisotropyEnable == 1u) {
        MinimalVkSamplerCreateInfo modifiedInfo = *pCreateInfo;
        modifiedInfo.mipLodBias += config.mipLodBias;
        return g_Original_vkCreateSampler(device, &modifiedInfo, pAllocator, pSampler);
    }
    return g_Original_vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
}

struct VkHookEntry {
    const char* name;
    void*       hookFn;
    void**      origSlot;
    bool        countsAsCore;
};

inline const VkHookEntry* GetDeviceVkHooks(size_t& outCount) {
    static const VkHookEntry kEntries[] = {
        { "vkCreateDevice",             reinterpret_cast<void*>(&Hooked_vkCreateDevice),             reinterpret_cast<void**>(&g_Original_vkCreateDevice),             true  },
        { "vkQueuePresentKHR",          reinterpret_cast<void*>(&Hooked_vkQueuePresentKHR),          reinterpret_cast<void**>(&g_Original_vkQueuePresentKHR),          true  },
        { "vkCreateSwapchainKHR",       reinterpret_cast<void*>(&Hooked_vkCreateSwapchainKHR),       reinterpret_cast<void**>(&g_Original_vkCreateSwapchainKHR),       true  },
        { "vkDestroySwapchainKHR",      reinterpret_cast<void*>(&Hooked_vkDestroySwapchainKHR),      reinterpret_cast<void**>(&g_Original_vkDestroySwapchainKHR),      true  },
        { "vkGetSwapchainImagesKHR",    reinterpret_cast<void*>(&Hooked_vkGetSwapchainImagesKHR),    reinterpret_cast<void**>(&g_Original_vkGetSwapchainImagesKHR),    true  },
        { "vkAcquireNextImageKHR",      reinterpret_cast<void*>(&Hooked_vkAcquireNextImageKHR),      reinterpret_cast<void**>(&g_Original_vkAcquireNextImageKHR),      true  },
        { "vkAcquireNextImage2KHR",     reinterpret_cast<void*>(&Hooked_vkAcquireNextImage2KHR),     reinterpret_cast<void**>(&g_Original_vkAcquireNextImage2KHR),     false },
        { "vkCmdSetViewport",           reinterpret_cast<void*>(&Hooked_vkCmdSetViewport),           reinterpret_cast<void**>(&g_Original_vkCmdSetViewport),           true  },
        { "vkCmdSetScissor",            reinterpret_cast<void*>(&Hooked_vkCmdSetScissor),            reinterpret_cast<void**>(&g_Original_vkCmdSetScissor),            true  },
        { "vkCmdBeginRenderPass",       reinterpret_cast<void*>(&Hooked_vkCmdBeginRenderPass),       reinterpret_cast<void**>(&g_Original_vkCmdBeginRenderPass),       true  },
        { "vkCmdEndRenderPass",         reinterpret_cast<void*>(&Hooked_vkCmdEndRenderPass),         reinterpret_cast<void**>(&g_Original_vkCmdEndRenderPass),         true  },
        { "vkCmdResolveImage",          reinterpret_cast<void*>(&Hooked_vkCmdResolveImage),          reinterpret_cast<void**>(&g_Original_vkCmdResolveImage),          false },
        { "vkCmdDraw",                  reinterpret_cast<void*>(&Hooked_vkCmdDraw),                  reinterpret_cast<void**>(&g_Original_vkCmdDraw),                  true  },
        { "vkCmdDrawIndexed",           reinterpret_cast<void*>(&Hooked_vkCmdDrawIndexed),           reinterpret_cast<void**>(&g_Original_vkCmdDrawIndexed),           true  },
        { "vkCmdSetSampleLocationsEXT", reinterpret_cast<void*>(&Hooked_vkCmdSetSampleLocationsEXT), reinterpret_cast<void**>(&g_Original_vkCmdSetSampleLocationsEXT), false },
        { "vkCreateSampler",            reinterpret_cast<void*>(&Hooked_vkCreateSampler),            reinterpret_cast<void**>(&g_Original_vkCreateSampler),            true  },
        { "vkCreateImage",              reinterpret_cast<void*>(&Hooked_vkCreateImage),              reinterpret_cast<void**>(&g_Original_vkCreateImage),              true  },
        { "vkDestroyImage",             reinterpret_cast<void*>(&Hooked_vkDestroyImage),             reinterpret_cast<void**>(&g_Original_vkDestroyImage),             true  },
        { "vkCreateImageView",          reinterpret_cast<void*>(&Hooked_vkCreateImageView),          reinterpret_cast<void**>(&g_Original_vkCreateImageView),          true  },
        { "vkDestroyImageView",         reinterpret_cast<void*>(&Hooked_vkDestroyImageView),         reinterpret_cast<void**>(&g_Original_vkDestroyImageView),         true  },
        { "vkCreateFramebuffer",        reinterpret_cast<void*>(&Hooked_vkCreateFramebuffer),        reinterpret_cast<void**>(&g_Original_vkCreateFramebuffer),        true  },
        { "vkDestroyFramebuffer",       reinterpret_cast<void*>(&Hooked_vkDestroyFramebuffer),       reinterpret_cast<void**>(&g_Original_vkDestroyFramebuffer),       true  },
        { "vkCreateRenderPass",         reinterpret_cast<void*>(&Hooked_vkCreateRenderPass),         reinterpret_cast<void**>(&g_Original_vkCreateRenderPass),         true  },
        { "vkCreateRenderPass2",        reinterpret_cast<void*>(&Hooked_vkCreateRenderPass2),        reinterpret_cast<void**>(&g_Original_vkCreateRenderPass2),        true  },
        { "vkCreateRenderPass2KHR",     reinterpret_cast<void*>(&Hooked_vkCreateRenderPass2KHR),     reinterpret_cast<void**>(&g_Original_vkCreateRenderPass2KHR),     false },
        { "vkDestroyRenderPass",        reinterpret_cast<void*>(&Hooked_vkDestroyRenderPass),        reinterpret_cast<void**>(&g_Original_vkDestroyRenderPass),        true  }
    };
    outCount = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

inline const VkHookEntry* GetInstanceVkHooks(size_t& outCount) {
    static const VkHookEntry kEntries[] = {
        { "vkEnumeratePhysicalDevices",              reinterpret_cast<void*>(&Hooked_vkEnumeratePhysicalDevices),              reinterpret_cast<void**>(&g_Original_vkEnumeratePhysicalDevices),              true },
        { "vkGetPhysicalDeviceProperties",           reinterpret_cast<void*>(&Hooked_vkGetPhysicalDeviceProperties),           reinterpret_cast<void**>(&g_Original_vkGetPhysicalDeviceProperties),           true },
        { "vkGetPhysicalDeviceMemoryProperties",     reinterpret_cast<void*>(&Hooked_vkGetPhysicalDeviceMemoryProperties),     reinterpret_cast<void**>(&g_Original_vkGetPhysicalDeviceMemoryProperties),     true }
    };
    outCount = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

void* Hooked_vkGetDeviceProcAddr(void* device, const char* pName) {
    if (!pName || !g_Original_vkGetDeviceProcAddr) {
        return nullptr;
    }

    void* realProc = g_Original_vkGetDeviceProcAddr(device, pName);
    if (!realProc) {
        return nullptr;
    }

    size_t count = 0;
    const VkHookEntry* entries = GetDeviceVkHooks(count);
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(pName, entries[i].name) == 0) {
            if (!*entries[i].origSlot) {
                *entries[i].origSlot = realProc;
            }
            return entries[i].hookFn;
        }
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

    size_t devCount = 0;
    const VkHookEntry* devEntries = GetDeviceVkHooks(devCount);
    for (size_t i = 0; i < devCount; ++i) {
        if (std::strcmp(pName, devEntries[i].name) == 0) {
            if (!*devEntries[i].origSlot) {
                *devEntries[i].origSlot = realProc;
            }
            return devEntries[i].hookFn;
        }
    }

    size_t instCount = 0;
    const VkHookEntry* instEntries = GetInstanceVkHooks(instCount);
    for (size_t i = 0; i < instCount; ++i) {
        if (std::strcmp(pName, instEntries[i].name) == 0) {
            if (!*instEntries[i].origSlot) {
                *instEntries[i].origSlot = realProc;
            }
            return instEntries[i].hookFn;
        }
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

    bool hookedAny = false;
    auto installExportHook = [&](const char* name, void* hookFn, void** origSlot, bool countsAsCore) {
        void* target = reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
        if (!target) return;
        const MH_STATUS st = MH_CreateHook(target, reinterpret_cast<LPVOID>(hookFn), reinterpret_cast<LPVOID*>(origSlot));
        if (st == MH_OK || st == MH_ERROR_ALREADY_CREATED) {
            MH_EnableHook(target);
            {
                std::lock_guard<std::mutex> lock(g_vkHookMutex);
                bool alreadyTracked = false;
                for (void* existing : g_HookedVulkanTargets) {
                    if (existing == target) { alreadyTracked = true; break; }
                }
                if (!alreadyTracked) {
                    g_HookedVulkanTargets.push_back(target);
                }
            }
            CBR_LOG_INFO("MinHook: Successfully detoured %s (export at 0x%p).", name, target);
            if (countsAsCore) hookedAny = true;
        } else if (!countsAsCore) {
            *origSlot = target;
            CBR_LOG_INFO("Directly resolved %s: 0x%p", name, target);
        } else {
            CBR_LOG_WARN("MinHook: Could not detour %s (status: %d).", name, st);
        }
    };

    installExportHook("vkGetInstanceProcAddr",
                      reinterpret_cast<void*>(&Hooked_vkGetInstanceProcAddr),
                      reinterpret_cast<void**>(&g_Original_vkGetInstanceProcAddr),
                      true);

    size_t instCount = 0;
    const VkHookEntry* instEntries = GetInstanceVkHooks(instCount);
    for (size_t i = 0; i < instCount; ++i) {
        installExportHook(instEntries[i].name, instEntries[i].hookFn, instEntries[i].origSlot, instEntries[i].countsAsCore);
    }

    size_t devCount = 0;
    const VkHookEntry* devEntries = GetDeviceVkHooks(devCount);
    for (size_t i = 0; i < devCount; ++i) {
        installExportHook(devEntries[i].name, devEntries[i].hookFn, devEntries[i].origSlot, devEntries[i].countsAsCore);
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
        CBR_LOG_INFO("Restoring original Vulkan function dispatch table (scoped M-04 teardown).");
        std::lock_guard<std::mutex> lock(g_vkHookMutex);
        for (void* target : g_HookedVulkanTargets) {
            if (target) {
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
        }
        g_HookedVulkanTargets.clear();
        m_vulkanHooked.store(false);
        g_Original_vkCmdResolveImage = nullptr;
        g_pendingResolveColorImage.store(nullptr, std::memory_order_release);
    }
}

} // namespace cbr
