#include "cbr/config.h"
#include "cbr/jitter_manager.h"
#include "cbr/logger.h"
#include "cbr/reconstruction_pass.h"
#include "cbr/render_target_manager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

// Minimal headless Vulkan ABI definitions (matching Khronos Vulkan 1.1+ x64 C ABI without SDK headers)
constexpr uint32_t kVkStructureTypeApplicationInfo = 0;
constexpr uint32_t kVkStructureTypeInstanceCreateInfo = 1;
constexpr uint32_t kVkStructureTypeDeviceQueueCreateInfo = 2;
constexpr uint32_t kVkStructureTypeDeviceCreateInfo = 3;
constexpr uint32_t kVkStructureTypeSubmitInfo = 4;
constexpr uint32_t kVkStructureTypeMemoryAllocateInfo = 5;
constexpr uint32_t kVkStructureTypeBufferCreateInfo = 12;
constexpr uint32_t kVkStructureTypeFramebufferCreateInfo = 37;
constexpr uint32_t kVkStructureTypeRenderPassCreateInfo = 38;
constexpr uint32_t kVkStructureTypeCommandPoolCreateInfo = 39;
constexpr uint32_t kVkStructureTypeCommandBufferAllocateInfo = 40;
constexpr uint32_t kVkStructureTypeCommandBufferBeginInfo = 42;
constexpr uint32_t kVkStructureTypeRenderPassBeginInfo = 43;
constexpr uint32_t kVkStructureTypeImageMemoryBarrier = 45;

constexpr uint32_t kVkQueueGraphicsBit = 0x00000001;
constexpr uint32_t kVkQueueComputeBit = 0x00000002;
constexpr uint32_t kVkCommandPoolCreateResetCommandBufferBit = 0x00000002;
constexpr uint32_t kVkCommandBufferLevelPrimary = 0;
constexpr uint32_t kVkCommandBufferUsageOneTimeSubmitBit = 0x00000001;
constexpr uint32_t kVkBufferUsageTransferDstBit = 0x00000002;
constexpr uint32_t kVkMemoryPropertyHostVisibleCoherentBits = 0x00000006;

constexpr uint32_t kVkFormatR16G16B16A16Sfloat = 97;
constexpr uint32_t kVkFormatR32Sfloat = 100;
constexpr uint32_t kVkFormatD32Sfloat = 126;
constexpr uint32_t kVkSampleCount2Bit = 0x00000002;
constexpr uint32_t kVkAttachmentLoadOpClear = 1;
constexpr uint32_t kVkAttachmentStoreOpStore = 0;
constexpr uint32_t kVkAttachmentLoadOpDontCare = 2;
constexpr uint32_t kVkAttachmentStoreOpDontCare = 1;
constexpr uint32_t kVkPipelineBindPointGraphics = 0;
constexpr uint32_t kVkImageLayoutUndefined = 0;
constexpr uint32_t kVkImageLayoutColorAttachmentOptimal = 2;
constexpr uint32_t kVkImageLayoutDepthStencilAttachmentOptimal = 3;
constexpr uint32_t kVkImageLayoutShaderReadOnlyOptimal = 5;
constexpr uint32_t kVkImageLayoutTransferSrcOptimal = 6;
constexpr uint32_t kVkImageLayoutTransferDstOptimal = 7;
constexpr uint32_t kVkImageAspectColorBit = 0x00000001;
constexpr uint32_t kVkPipelineStageTopOfPipeBit = 0x00000001;
constexpr uint32_t kVkPipelineStageComputeShaderBit = 0x00000800;
constexpr uint32_t kVkPipelineStageTransferBit = 0x00001000;
constexpr uint32_t kVkAccessShaderReadBit = 0x00000020;
constexpr uint32_t kVkAccessTransferReadBit = 0x00000800;
constexpr uint32_t kVkAccessTransferWriteBit = 0x00001000;
constexpr uint32_t kVkSubpassContentsInline = 0;

struct VkAppInfo {
    uint32_t    sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t    applicationVersion;
    const char* pEngineName;
    uint32_t    engineVersion;
    uint32_t    apiVersion;
};

struct VkInstanceCreateInfoMin {
    uint32_t          sType;
    const void*       pNext;
    uint32_t          flags;
    const VkAppInfo*  pApplicationInfo;
    uint32_t          enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t          enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

struct VkQueueFamilyPropsMin {
    uint32_t queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
};
static_assert(sizeof(VkQueueFamilyPropsMin) == 24, "VkQueueFamilyProperties size mismatch");

struct VkDeviceQueueCreateInfoMin {
    uint32_t     sType;
    const void*  pNext;
    uint32_t     flags;
    uint32_t     queueFamilyIndex;
    uint32_t     queueCount;
    const float* pQueuePriorities;
};

struct VkDeviceCreateInfoMin {
    uint32_t                         sType;
    const void*                      pNext;
    uint32_t                         flags;
    uint32_t                         queueCreateInfoCount;
    const VkDeviceQueueCreateInfoMin* pQueueCreateInfos;
    uint32_t                         enabledLayerCount;
    const char* const*               ppEnabledLayerNames;
    uint32_t                         enabledExtensionCount;
    const char* const*               ppEnabledExtensionNames;
    const void*                      pEnabledFeatures;
};

struct VkCommandPoolCreateInfoMin {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    queueFamilyIndex;
};

struct VkCommandBufferAllocateInfoMin {
    uint32_t    sType;
    const void* pNext;
    void*       commandPool;
    uint32_t    level;
    uint32_t    commandBufferCount;
};

struct VkCommandBufferBeginInfoMin {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    const void* pInheritanceInfo;
};

struct VkSubmitInfoMin {
    uint32_t        sType;
    const void*     pNext;
    uint32_t        waitSemaphoreCount;
    const void*     pWaitSemaphores;
    const uint32_t* pWaitDstStageMask;
    uint32_t        commandBufferCount;
    void* const*    pCommandBuffers;
    uint32_t        signalSemaphoreCount;
    const void*     pSignalSemaphores;
};

struct VkAttachmentDescriptionMin {
    uint32_t flags;
    uint32_t format;
    uint32_t samples;
    uint32_t loadOp;
    uint32_t storeOp;
    uint32_t stencilLoadOp;
    uint32_t stencilStoreOp;
    uint32_t initialLayout;
    uint32_t finalLayout;
};

struct VkAttachmentReferenceMin {
    uint32_t attachment;
    uint32_t layout;
};

struct VkSubpassDescriptionMin {
    uint32_t                       flags;
    uint32_t                       pipelineBindPoint;
    uint32_t                       inputAttachmentCount;
    const void*                    pInputAttachments;
    uint32_t                       colorAttachmentCount;
    const VkAttachmentReferenceMin* pColorAttachments;
    const void*                    pResolveAttachments;
    const void*                    pDepthStencilAttachment;
    uint32_t                       preserveAttachmentCount;
    const uint32_t*                pPreserveAttachments;
};

struct VkRenderPassCreateInfoMin {
    uint32_t                        sType;
    const void*                     pNext;
    uint32_t                        flags;
    uint32_t                        attachmentCount;
    const VkAttachmentDescriptionMin* pAttachments;
    uint32_t                        subpassCount;
    const VkSubpassDescriptionMin*  pSubpasses;
    uint32_t                        dependencyCount;
    const void*                     pDependencies;
};

struct VkFramebufferCreateInfoMin {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    void*       renderPass;
    uint32_t    attachmentCount;
    void* const* pAttachments;
    uint32_t    width;
    uint32_t    height;
    uint32_t    layers;
};

struct VkRect2DMin {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
};

union VkClearColorValueMin {
    float    float32[4];
    int32_t  int32[4];
    uint32_t uint32[4];
};

union VkClearValueMin {
    VkClearColorValueMin color;
    struct {
        float    depth;
        uint32_t stencil;
    } depthStencil;
};

struct VkRenderPassBeginInfoMin {
    uint32_t               sType;
    const void*            pNext;
    void*                  renderPass;
    void*                  framebuffer;
    VkRect2DMin            renderArea;
    uint32_t               clearValueCount;
    const VkClearValueMin* pClearValues;
};

struct VkClearAttachmentMin {
    uint32_t        aspectMask;
    uint32_t        colorAttachment;
    VkClearValueMin clearValue;
};

struct VkClearRectMin {
    VkRect2DMin rect;
    uint32_t    baseArrayLayer;
    uint32_t    layerCount;
};

struct VkBufferCreateInfoMin {
    uint32_t        sType;
    const void*     pNext;
    uint32_t        flags;
    uint64_t        size;
    uint32_t        usage;
    uint32_t        sharingMode;
    uint32_t        queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
};

struct VkMemoryRequirementsMin {
    uint64_t size;
    uint64_t alignment;
    uint32_t memoryTypeBits;
};

struct VkMemoryAllocateInfoMin {
    uint32_t    sType;
    const void* pNext;
    uint64_t    allocationSize;
    uint32_t    memoryTypeIndex;
};

struct VkMemTypeMin {
    uint32_t propertyFlags;
    uint32_t heapIndex;
};

struct VkMemHeapMin {
    uint64_t size;
    uint32_t flags;
};

struct VkPhysMemPropsMin {
    uint32_t     memoryTypeCount;
    VkMemTypeMin memoryTypes[32];
    uint32_t     memoryHeapCount;
    VkMemHeapMin memoryHeaps[16];
};

struct VkImageSubresourceLayersMin {
    uint32_t aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
};

struct VkOffset3DMin {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct VkExtent3DMin {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
};

struct VkBufferImageCopyMin {
    uint64_t                    bufferOffset;
    uint32_t                    bufferRowLength;
    uint32_t                    bufferImageHeight;
    VkImageSubresourceLayersMin imageSubresource;
    VkOffset3DMin               imageOffset;
    VkExtent3DMin               imageExtent;
};
static_assert(sizeof(VkBufferImageCopyMin) == 56, "VkBufferImageCopy size mismatch");

struct VkImageSubresourceRangeMin {
    uint32_t aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
};

// Decode IEEE-754 binary16 (VK_FORMAT_R16G16B16A16_SFLOAT) to float32
float HalfToFloat(uint16_t h) {
    const uint32_t sign = (static_cast<uint32_t>(h & 0x8000u)) << 16;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t bits = 0;
    if (exp == 0u) {
        if (mant == 0u) {
            bits = sign;
        } else {
            uint32_t m = mant;
            uint32_t e = 127u - 14u;
            while ((m & 0x0400u) == 0u) {
                m <<= 1;
                --e;
            }
            m &= 0x03FFu;
            bits = sign | (e << 23) | (m << 13);
        }
    } else if (exp == 31u) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

class DynamicVulkanLoader {
public:
    bool Open() {
#if defined(_WIN32)
        m_mod = LoadLibraryA("vulkan-1.dll");
#else
        m_mod = dlopen("libvulkan.so.1", RTLD_LAZY);
        if (!m_mod) m_mod = dlopen("libvulkan.so", RTLD_LAZY);
#endif
        return m_mod != nullptr;
    }

    ~DynamicVulkanLoader() {
        // Leave loader mapped across singleton destructors unless explicitly needed
    }

    void* GetSym(const char* name) const {
        if (!m_mod || !name) return nullptr;
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_mod), name));
#else
        return dlsym(m_mod, name);
#endif
    }

private:
    void* m_mod{ nullptr };
};

} // namespace

int main() {
    cbr::Logger::Get().Disable();

    DynamicVulkanLoader vk;
    if (!vk.Open()) {
        std::cout << "[SKIP] Vulkan loader library not installed on host; skipping headless Vulkan compute test.\n";
        return 0;
    }

    using PFN_vkGetInstanceProcAddr = void* (*)(void*, const char*);
    using PFN_vkCreateInstance = int (*)(const VkInstanceCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyInstance = void (*)(void*, const void*);
    using PFN_vkEnumeratePhysicalDevices = int (*)(void*, uint32_t*, void**);
    using PFN_vkGetPhysicalDeviceProperties = void (*)(void*, void*);
    using PFN_vkGetPhysicalDeviceMemoryProperties = void (*)(void*, VkPhysMemPropsMin*);
    using PFN_vkGetPhysicalDeviceQueueFamilyProperties = void (*)(void*, uint32_t*, VkQueueFamilyPropsMin*);
    using PFN_vkCreateDevice = int (*)(void*, const VkDeviceCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyDevice = void (*)(void*, const void*);
    using PFN_vkGetDeviceQueue = void (*)(void*, uint32_t, uint32_t, void**);
    using PFN_vkDeviceWaitIdle = int (*)(void*);
    using PFN_vkQueueSubmit = int (*)(void*, uint32_t, const VkSubmitInfoMin*, void*);
    using PFN_vkQueueWaitIdle = int (*)(void*);
    using PFN_vkCreateCommandPool = int (*)(void*, const VkCommandPoolCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyCommandPool = void (*)(void*, void*, const void*);
    using PFN_vkAllocateCommandBuffers = int (*)(void*, const VkCommandBufferAllocateInfoMin*, void**);
    using PFN_vkBeginCommandBuffer = int (*)(void*, const VkCommandBufferBeginInfoMin*);
    using PFN_vkEndCommandBuffer = int (*)(void*);
    using PFN_vkResetCommandBuffer = int (*)(void*, uint32_t);
    using PFN_vkCreateRenderPass = int (*)(void*, const VkRenderPassCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyRenderPass = void (*)(void*, void*, const void*);
    using PFN_vkCreateFramebuffer = int (*)(void*, const VkFramebufferCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyFramebuffer = void (*)(void*, void*, const void*);
    using PFN_vkCmdBeginRenderPass = void (*)(void*, const VkRenderPassBeginInfoMin*, uint32_t);
    using PFN_vkCmdEndRenderPass = void (*)(void*);
    using PFN_vkCmdClearAttachments = void (*)(void*, uint32_t, const VkClearAttachmentMin*, uint32_t, const VkClearRectMin*);
    using PFN_vkCmdClearColorImage = void (*)(void*, void*, uint32_t, const VkClearColorValueMin*, uint32_t, const VkImageSubresourceRangeMin*);
    using PFN_vkCmdCopyImageToBuffer = void (*)(void*, void*, uint32_t, void*, uint32_t, const VkBufferImageCopyMin*);
    using PFN_vkCreateBuffer = int (*)(void*, const VkBufferCreateInfoMin*, const void*, void**);
    using PFN_vkDestroyBuffer = void (*)(void*, void*, const void*);
    using PFN_vkGetBufferMemoryRequirements = void (*)(void*, void*, VkMemoryRequirementsMin*);
    using PFN_vkAllocateMemory = int (*)(void*, const VkMemoryAllocateInfoMin*, const void*, void**);
    using PFN_vkFreeMemory = void (*)(void*, void*, const void*);
    using PFN_vkBindBufferMemory = int (*)(void*, void*, void*, uint64_t);
    using PFN_vkMapMemory = int (*)(void*, void*, uint64_t, uint64_t, uint32_t, void**);
    using PFN_vkUnmapMemory = void (*)(void*, void*);

    auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vk.GetSym("vkGetInstanceProcAddr"));
    if (!pfnGetInstanceProcAddr) {
        std::cout << "[SKIP] vkGetInstanceProcAddr unavailable; skipping headless Vulkan compute test.\n";
        return 0;
    }

    auto pfnCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(pfnGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (!pfnCreateInstance) {
        std::cout << "[SKIP] vkCreateInstance unavailable; skipping headless Vulkan compute test.\n";
        return 0;
    }

    VkAppInfo appInfo{};
    appInfo.sType = kVkStructureTypeApplicationInfo;
    appInfo.pApplicationName = "cbr_vulkan_headless";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "CBR";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1u << 22) | (1u << 12); // Vulkan 1.1

    VkInstanceCreateInfoMin instInfo{};
    instInfo.sType = kVkStructureTypeInstanceCreateInfo;
    instInfo.pApplicationInfo = &appInfo;

    void* instance = nullptr;
    if (pfnCreateInstance(&instInfo, nullptr, &instance) != 0 || !instance) {
        std::cout << "[SKIP] No headless Vulkan ICD available on this machine (vkCreateInstance failed).\n";
        return 0;
    }

    auto instSym = [&](const char* name) -> void* {
        void* p = pfnGetInstanceProcAddr(instance, name);
        return p ? p : vk.GetSym(name);
    };

    auto pfnDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(instSym("vkDestroyInstance"));
    auto pfnEnumPhys = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(instSym("vkEnumeratePhysicalDevices"));
    auto pfnGetPhysProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(instSym("vkGetPhysicalDeviceProperties"));
    auto pfnGetPhysMemProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(instSym("vkGetPhysicalDeviceMemoryProperties"));
    auto pfnGetQueueProps = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instSym("vkGetPhysicalDeviceQueueFamilyProperties"));
    auto pfnCreateDevice = reinterpret_cast<PFN_vkCreateDevice>(instSym("vkCreateDevice"));

    uint32_t physCount = 0;
    if (!pfnEnumPhys || pfnEnumPhys(instance, &physCount, nullptr) != 0 || physCount == 0) {
        std::cout << "[SKIP] No Vulkan physical devices / ICD available on host.\n";
        if (pfnDestroyInstance) pfnDestroyInstance(instance, nullptr);
        return 0;
    }
    std::vector<void*> physDevs(physCount);
    pfnEnumPhys(instance, &physCount, physDevs.data());

    void* physDev = nullptr;
    uint32_t chosenQueueFamily = 0;
    bool foundQueue = false;
    for (void* cand : physDevs) {
        uint32_t qCount = 0;
        pfnGetQueueProps(cand, &qCount, nullptr);
        std::vector<VkQueueFamilyPropsMin> qProps(qCount);
        pfnGetQueueProps(cand, &qCount, qProps.data());
        for (uint32_t q = 0; q < qCount; ++q) {
            if ((qProps[q].queueFlags & (kVkQueueGraphicsBit | kVkQueueComputeBit)) ==
                (kVkQueueGraphicsBit | kVkQueueComputeBit)) {
                physDev = cand;
                chosenQueueFamily = q;
                foundQueue = true;
                break;
            }
        }
        if (foundQueue) break;
    }
    if (!foundQueue || !physDev) {
        std::cout << "[SKIP] No Vulkan queue family supporting both graphics and compute.\n";
        if (pfnDestroyInstance) pfnDestroyInstance(instance, nullptr);
        return 0;
    }

    if (pfnGetPhysProps) {
        uint8_t propsBuf[1024]{};
        pfnGetPhysProps(physDev, propsBuf);
        const char* devName = reinterpret_cast<const char*>(propsBuf + 20);
        std::cout << "[INFO] Headless Vulkan physical device: " << devName << "\n";
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfoMin queueInfo{};
    queueInfo.sType = kVkStructureTypeDeviceQueueCreateInfo;
    queueInfo.queueFamilyIndex = chosenQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfoMin devInfo{};
    devInfo.sType = kVkStructureTypeDeviceCreateInfo;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &queueInfo;

    void* device = nullptr;
    if (pfnCreateDevice(physDev, &devInfo, nullptr, &device) != 0 || !device) {
        std::cerr << "[FAIL] vkCreateDevice failed.\n";
        if (pfnDestroyInstance) pfnDestroyInstance(instance, nullptr);
        return 1;
    }

    auto pfnDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(vk.GetSym("vkDestroyDevice"));
    auto pfnGetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(vk.GetSym("vkGetDeviceQueue"));
    auto pfnDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(vk.GetSym("vkDeviceWaitIdle"));
    auto pfnQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(vk.GetSym("vkQueueSubmit"));
    auto pfnQueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(vk.GetSym("vkQueueWaitIdle"));
    auto pfnCreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(vk.GetSym("vkCreateCommandPool"));
    auto pfnDestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(vk.GetSym("vkDestroyCommandPool"));
    auto pfnAllocCmdBufs = reinterpret_cast<PFN_vkAllocateCommandBuffers>(vk.GetSym("vkAllocateCommandBuffers"));
    auto pfnBeginCmdBuf = reinterpret_cast<PFN_vkBeginCommandBuffer>(vk.GetSym("vkBeginCommandBuffer"));
    auto pfnEndCmdBuf = reinterpret_cast<PFN_vkEndCommandBuffer>(vk.GetSym("vkEndCommandBuffer"));
    auto pfnResetCmdBuf = reinterpret_cast<PFN_vkResetCommandBuffer>(vk.GetSym("vkResetCommandBuffer"));
    auto pfnCreateRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(vk.GetSym("vkCreateRenderPass"));
    auto pfnDestroyRenderPass = reinterpret_cast<PFN_vkDestroyRenderPass>(vk.GetSym("vkDestroyRenderPass"));
    auto pfnCreateFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(vk.GetSym("vkCreateFramebuffer"));
    auto pfnDestroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(vk.GetSym("vkDestroyFramebuffer"));
    auto pfnCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(vk.GetSym("vkCmdBeginRenderPass"));
    auto pfnCmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(vk.GetSym("vkCmdEndRenderPass"));
    auto pfnCmdClearAttachments = reinterpret_cast<PFN_vkCmdClearAttachments>(vk.GetSym("vkCmdClearAttachments"));
    auto pfnCmdClearColorImage = reinterpret_cast<PFN_vkCmdClearColorImage>(vk.GetSym("vkCmdClearColorImage"));
    auto pfnCmdCopyImageToBuffer = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(vk.GetSym("vkCmdCopyImageToBuffer"));
    auto pfnCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(vk.GetSym("vkCreateBuffer"));
    auto pfnDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(vk.GetSym("vkDestroyBuffer"));
    auto pfnGetBufMemReqs = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(vk.GetSym("vkGetBufferMemoryRequirements"));
    auto pfnAllocMem = reinterpret_cast<PFN_vkAllocateMemory>(vk.GetSym("vkAllocateMemory"));
    auto pfnFreeMem = reinterpret_cast<PFN_vkFreeMemory>(vk.GetSym("vkFreeMemory"));
    auto pfnBindBufMem = reinterpret_cast<PFN_vkBindBufferMemory>(vk.GetSym("vkBindBufferMemory"));
    auto pfnMapMem = reinterpret_cast<PFN_vkMapMemory>(vk.GetSym("vkMapMemory"));
    auto pfnUnmapMem = reinterpret_cast<PFN_vkUnmapMemory>(vk.GetSym("vkUnmapMemory"));

    void* queue = nullptr;
    pfnGetDeviceQueue(device, chosenQueueFamily, 0, &queue);

    // Configure synthetic 64x32 full-res (32x16 quarter-res 2x MSAA) CBR test environment
    constexpr uint32_t kTargetW = 64;
    constexpr uint32_t kTargetH = 32;
    constexpr uint32_t kQuarterW = kTargetW / 2;
    constexpr uint32_t kQuarterH = kTargetH / 2;

    cbr::ConfigManager::Get().Modify([&](cbr::CBRConfig& c) {
        c.targetWidth = kTargetW;
        c.targetHeight = kTargetH;
        c.enabled = true;
        c.enableSpatialFallback = true;
        c.enableColorClamping = true;
        c.historyWeight = 0.90f;
        c.depthTolerance = 0.05f;
        c.jitterDirection = 1;
        c.depthConvention = cbr::DepthConvention::Reversed;
        c.depthNear = 0.1f;
        c.depthFar = 0.0f;
        c.debugView = 0;
        c.sharpeningStrength = 0.0f;
        c.enableCameraMotionFallback = false;
    });

    auto& rtm = cbr::RenderTargetManager::Get();
    auto& pass = cbr::ReconstructionPass::Get();
    auto& jitter = cbr::JitterManager::Get();

    rtm.Initialize(kTargetW, kTargetH);
    jitter.Initialize(kTargetW, kTargetH);

    if (!rtm.CreateVulkanResources(device, physDev)) {
        std::cerr << "[FAIL] RenderTargetManager::CreateVulkanResources failed.\n";
        pfnDestroyDevice(device, nullptr);
        pfnDestroyInstance(instance, nullptr);
        return 1;
    }

    // Resolve compiled cbr_reconstruct.spv across build/source layouts
    std::filesystem::path spvDir;
    const std::filesystem::path candidates[] = {
#ifdef CBR_DEFAULT_SHADER_DIR
        std::filesystem::path(CBR_DEFAULT_SHADER_DIR),
#endif
#ifdef CBR_SOURCE_SHADER_SPV_DIR
        std::filesystem::path(CBR_SOURCE_SHADER_SPV_DIR),
#endif
        std::filesystem::path("shaders/spv"),
        std::filesystem::path("shaders"),
        std::filesystem::path("../shaders/spv")
    };
    for (const auto& dir : candidates) {
        std::error_code ec;
        if (!dir.empty() && std::filesystem::exists(dir / "cbr_reconstruct.spv", ec)) {
            spvDir = dir;
            break;
        }
    }
    if (spvDir.empty()) {
        std::cout << "[SKIP] Compiled cbr_reconstruct.spv not present on disk; skipping GPU dispatch verification.\n";
        rtm.DestroyVulkanResources();
        pfnDestroyDevice(device, nullptr);
        pfnDestroyInstance(instance, nullptr);
        return 0;
    }
    pass.SetShaderDirectory(spvDir);

    if (!pass.InitializeVulkan(device, physDev) || !pass.GetPipeline()) {
        std::cerr << "[FAIL] ReconstructionPass::InitializeVulkan failed to build compute pipeline from "
                  << (spvDir / "cbr_reconstruct.spv").string() << ".\n";
        pass.Shutdown();
        rtm.DestroyVulkanResources();
        pfnDestroyDevice(device, nullptr);
        pfnDestroyInstance(instance, nullptr);
        return 1;
    }

    const auto& res = rtm.GetVulkanResources();
    if (!pass.UpdateVulkanPingPongDescriptors(
            res.quarterColor.view,
            res.quarterDepth.view,
            res.historyColor[0].view, res.historyDepth[0].view,
            res.historyColor[1].view, res.historyDepth[1].view,
            res.velocity.view,
            res.outputColor.view, res.outputDepth.view,
            res.linearSampler, res.pointSampler)) {
        std::cerr << "[FAIL] UpdateVulkanPingPongDescriptors failed.\n";
        pass.Shutdown();
        rtm.DestroyVulkanResources();
        pfnDestroyDevice(device, nullptr);
        pfnDestroyInstance(instance, nullptr);
        return 1;
    }

    // Command pool & buffer
    VkCommandPoolCreateInfoMin poolInfo{};
    poolInfo.sType = kVkStructureTypeCommandPoolCreateInfo;
    poolInfo.flags = kVkCommandPoolCreateResetCommandBufferBit;
    poolInfo.queueFamilyIndex = chosenQueueFamily;

    void* cmdPool = nullptr;
    void* cmdBuf = nullptr;
    pfnCreateCommandPool(device, &poolInfo, nullptr, &cmdPool);
    VkCommandBufferAllocateInfoMin allocBufInfo{};
    allocBufInfo.sType = kVkStructureTypeCommandBufferAllocateInfo;
    allocBufInfo.commandPool = cmdPool;
    allocBufInfo.level = kVkCommandBufferLevelPrimary;
    allocBufInfo.commandBufferCount = 1;
    pfnAllocCmdBufs(device, &allocBufInfo, &cmdBuf);

    // 2x MSAA RenderPass to populate quarterColor & quarterDepth without TRANSFER MSAA restrictions
    VkAttachmentDescriptionMin rpAtts[2]{};
    rpAtts[0].format = kVkFormatR16G16B16A16Sfloat;
    rpAtts[0].samples = kVkSampleCount2Bit;
    rpAtts[0].loadOp = kVkAttachmentLoadOpClear;
    rpAtts[0].storeOp = kVkAttachmentStoreOpStore;
    rpAtts[0].stencilLoadOp = kVkAttachmentLoadOpDontCare;
    rpAtts[0].stencilStoreOp = kVkAttachmentStoreOpDontCare;
    rpAtts[0].initialLayout = kVkImageLayoutUndefined;
    rpAtts[0].finalLayout = kVkImageLayoutShaderReadOnlyOptimal;

    rpAtts[1].format = kVkFormatD32Sfloat;
    rpAtts[1].samples = kVkSampleCount2Bit;
    rpAtts[1].loadOp = kVkAttachmentLoadOpClear;
    rpAtts[1].storeOp = kVkAttachmentStoreOpStore;
    rpAtts[1].stencilLoadOp = kVkAttachmentLoadOpDontCare;
    rpAtts[1].stencilStoreOp = kVkAttachmentStoreOpDontCare;
    rpAtts[1].initialLayout = kVkImageLayoutUndefined;
    rpAtts[1].finalLayout = kVkImageLayoutShaderReadOnlyOptimal;

    VkAttachmentReferenceMin colorRef = { 0, kVkImageLayoutColorAttachmentOptimal };
    VkAttachmentReferenceMin depthRef = { 1, kVkImageLayoutDepthStencilAttachmentOptimal };

    VkSubpassDescriptionMin subpass{};
    subpass.pipelineBindPoint = kVkPipelineBindPointGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfoMin rpInfo{};
    rpInfo.sType = kVkStructureTypeRenderPassCreateInfo;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = rpAtts;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    void* renderPass = nullptr;
    void* framebuffer = nullptr;
    if (pfnCreateRenderPass(device, &rpInfo, nullptr, &renderPass) != 0 || !renderPass) {
        std::cerr << "[FAIL] vkCreateRenderPass failed.\n";
        return 1;
    }

    void* fbViews[2] = { res.quarterColor.view, res.quarterDepth.view };
    VkFramebufferCreateInfoMin fbInfo{};
    fbInfo.sType = kVkStructureTypeFramebufferCreateInfo;
    fbInfo.renderPass = renderPass;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = fbViews;
    fbInfo.width = kQuarterW;
    fbInfo.height = kQuarterH;
    fbInfo.layers = 1;
    if (pfnCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer) != 0 || !framebuffer) {
        std::cerr << "[FAIL] vkCreateFramebuffer failed.\n";
        return 1;
    }

    // Host-visible staging buffer for 64x32 RGBA16F readback (8 bytes/pixel)
    constexpr size_t kStagingSize = static_cast<size_t>(kTargetW) * kTargetH * 8u;
    VkBufferCreateInfoMin bufInfo{};
    bufInfo.sType = kVkStructureTypeBufferCreateInfo;
    bufInfo.size = kStagingSize;
    bufInfo.usage = kVkBufferUsageTransferDstBit;
    bufInfo.sharingMode = 0;

    void* stagingBuf = nullptr;
    void* stagingMem = nullptr;
    pfnCreateBuffer(device, &bufInfo, nullptr, &stagingBuf);
    VkMemoryRequirementsMin memReqs{};
    pfnGetBufMemReqs(device, stagingBuf, &memReqs);

    VkPhysMemPropsMin memProps{};
    pfnGetPhysMemProps(physDev, &memProps);
    uint32_t hostMemType = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & kVkMemoryPropertyHostVisibleCoherentBits) ==
                kVkMemoryPropertyHostVisibleCoherentBits) {
            hostMemType = i;
            break;
        }
    }
    VkMemoryAllocateInfoMin memAlloc{};
    memAlloc.sType = kVkStructureTypeMemoryAllocateInfo;
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = hostMemType;
    pfnAllocMem(device, &memAlloc, nullptr, &stagingMem);
    pfnBindBufMem(device, stagingBuf, stagingMem, 0);

    bool failed = false;

    // Execute Frame 0 (parity 0, writes history[1]) and Frame 1 (parity 1, reads history[1] and writes history[0])
    for (uint32_t frame = 0; frame < 2; ++frame) {
        jitter.Update(frame);
        pfnResetCmdBuf(cmdBuf, 0);

        VkCommandBufferBeginInfoMin beginInfo{};
        beginInfo.sType = kVkStructureTypeCommandBufferBeginInfo;
        beginInfo.flags = kVkCommandBufferUsageOneTimeSubmitBit;
        pfnBeginCmdBuf(cmdBuf, &beginInfo);

        // Populate 2x MSAA quarterColor with alternating 4x4 quarter-res (8x8 full-res) checkerboard tiles
        // Tile A = (0.75, 0.25, 0.50), Tile B = (0.25, 0.75, 0.50); quarterDepth = 0.50
        VkClearValueMin rpClears[2]{};
        rpClears[0].color = { { 0.75f, 0.25f, 0.50f, 1.0f } };
        rpClears[1].depthStencil = { 0.50f, 0u };

        VkRenderPassBeginInfoMin rpBegin{};
        rpBegin.sType = kVkStructureTypeRenderPassBeginInfo;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea = { 0, 0, kQuarterW, kQuarterH };
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = rpClears;

        pfnCmdBeginRenderPass(cmdBuf, &rpBegin, kVkSubpassContentsInline);
        std::vector<VkClearRectMin> oddTileRects;
        for (uint32_t ty = 0; ty < kQuarterH; ty += 4u) {
            for (uint32_t tx = 0; tx < kQuarterW; tx += 4u) {
                if ((((tx / 4u) + (ty / 4u)) & 1u) != 0u) {
                    VkClearRectMin cr{};
                    cr.rect = { static_cast<int32_t>(tx), static_cast<int32_t>(ty), 4u, 4u };
                    cr.baseArrayLayer = 0;
                    cr.layerCount = 1;
                    oddTileRects.push_back(cr);
                }
            }
        }
        VkClearAttachmentMin tileClear{};
        tileClear.aspectMask = kVkImageAspectColorBit;
        tileClear.colorAttachment = 0;
        tileClear.clearValue.color = { { 0.25f, 0.75f, 0.50f, 1.0f } };
        pfnCmdClearAttachments(cmdBuf, 1, &tileClear, static_cast<uint32_t>(oddTileRects.size()), oddTileRects.data());
        pfnCmdEndRenderPass(cmdBuf);

        rtm.SetVulkanImageLayout(res.quarterColor.image, kVkImageLayoutShaderReadOnlyOptimal);
        rtm.SetVulkanImageLayout(res.quarterDepth.image, kVkImageLayoutShaderReadOnlyOptimal);

        // Zero velocity buffer explicitly and keep tracked layout in SHADER_READ_ONLY_OPTIMAL
        const uint32_t oldVelLayout = (frame == 0u) ? kVkImageLayoutUndefined : kVkImageLayoutShaderReadOnlyOptimal;
        cbr::ReconstructionPass::TransitionImageLayout(
            cmdBuf, res.velocity.image,
            oldVelLayout, kVkImageLayoutTransferDstOptimal,
            kVkPipelineStageTopOfPipeBit, kVkPipelineStageTransferBit,
            0, kVkAccessTransferWriteBit, res.velocity.aspectMask);
        const VkClearColorValueMin zeroVel{ { 0.0f, 0.0f, 0.0f, 0.0f } };
        const VkImageSubresourceRangeMin velRange{ kVkImageAspectColorBit, 0, 1, 0, 1 };
        pfnCmdClearColorImage(cmdBuf, res.velocity.image, kVkImageLayoutTransferDstOptimal, &zeroVel, 1, &velRange);
        cbr::ReconstructionPass::TransitionImageLayout(
            cmdBuf, res.velocity.image,
            kVkImageLayoutTransferDstOptimal, kVkImageLayoutShaderReadOnlyOptimal,
            kVkPipelineStageTransferBit, kVkPipelineStageComputeShaderBit,
            kVkAccessTransferWriteBit, kVkAccessShaderReadBit, res.velocity.aspectMask);

        const uint32_t activeSetIdx = rtm.GetCurrentHistoryIndex() & 1u;
        const uint32_t writtenIdx = 1u - activeSetIdx;

        // Dispatch CBR reconstruction compute shader
        pass.DispatchVulkan(cmdBuf, frame);
        rtm.SwapHistoryBuffers();

        // Read back just-written history color buffer into staging buffer
        cbr::ReconstructionPass::TransitionImageLayout(
            cmdBuf, res.historyColor[writtenIdx].image,
            kVkImageLayoutShaderReadOnlyOptimal, kVkImageLayoutTransferSrcOptimal,
            kVkPipelineStageComputeShaderBit, kVkPipelineStageTransferBit,
            kVkAccessShaderReadBit, kVkAccessTransferReadBit, res.historyColor[writtenIdx].aspectMask);

        VkBufferImageCopyMin copyRegion{};
        copyRegion.imageSubresource = { kVkImageAspectColorBit, 0, 0, 1 };
        copyRegion.imageOffset = { 0, 0, 0 };
        copyRegion.imageExtent = { kTargetW, kTargetH, 1 };
        pfnCmdCopyImageToBuffer(cmdBuf, res.historyColor[writtenIdx].image,
                                kVkImageLayoutTransferSrcOptimal, stagingBuf, 1, &copyRegion);

        cbr::ReconstructionPass::TransitionImageLayout(
            cmdBuf, res.historyColor[writtenIdx].image,
            kVkImageLayoutTransferSrcOptimal, kVkImageLayoutShaderReadOnlyOptimal,
            kVkPipelineStageTransferBit, kVkPipelineStageComputeShaderBit,
            kVkAccessTransferReadBit, kVkAccessShaderReadBit, res.historyColor[writtenIdx].aspectMask);

        pfnEndCmdBuf(cmdBuf);

        VkSubmitInfoMin submitInfo{};
        submitInfo.sType = kVkStructureTypeSubmitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        if (pfnQueueSubmit(queue, 1, &submitInfo, nullptr) != 0) {
            std::cerr << "[FAIL] vkQueueSubmit failed on frame " << frame << ".\n";
            failed = true;
            break;
        }
        pfnQueueWaitIdle(queue);

        void* mapped = nullptr;
        if (pfnMapMem(device, stagingMem, 0, kStagingSize, 0, &mapped) != 0 || !mapped) {
            std::cerr << "[FAIL] vkMapMemory failed on frame " << frame << ".\n";
            failed = true;
            break;
        }
        const auto* halfPixels = static_cast<const uint16_t*>(mapped);
        size_t nonZeroCount = 0;
        size_t interiorExactCount = 0;
        size_t interiorTotalCount = 0;
        for (uint32_t y = 0; y < kTargetH; ++y) {
            for (uint32_t x = 0; x < kTargetW; ++x) {
                const size_t base = (static_cast<size_t>(y) * kTargetW + x) * 4u;
                const float r = HalfToFloat(halfPixels[base + 0]);
                const float g = HalfToFloat(halfPixels[base + 1]);
                const float b = HalfToFloat(halfPixels[base + 2]);
                const float a = HalfToFloat(halfPixels[base + 3]);

                const bool finite = std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
                const bool nonzero = finite && (r > 0.1f && g > 0.1f && b > 0.1f) && (std::fabs(a - 1.0f) < 1e-2f);
                if (nonzero) ++nonZeroCount;

                // Each 4x4 quarter tile corresponds to an 8x8 full-res tile; check interior pixels away from 1-pixel borders
                const uint32_t lx = x % 8u;
                const uint32_t ly = y % 8u;
                if (lx >= 1u && lx <= 6u && ly >= 1u && ly <= 6u) {
                    ++interiorTotalCount;
                    const bool oddTile = ((((x / 8u) + (y / 8u)) & 1u) != 0u);
                    const float expR = oddTile ? 0.25f : 0.75f;
                    const float expG = oddTile ? 0.75f : 0.25f;
                    const float expB = 0.50f;
                    if (std::fabs(r - expR) < 1e-2f &&
                        std::fabs(g - expG) < 1e-2f &&
                        std::fabs(b - expB) < 1e-2f) {
                        ++interiorExactCount;
                    }
                }
            }
        }
        pfnUnmapMem(device, stagingMem);

        std::cout << "[INFO] Frame " << frame << " readback (history[" << writtenIdx << "]): "
                  << nonZeroCount << "/" << (kTargetW * kTargetH) << " non-zero, "
                  << interiorExactCount << "/" << interiorTotalCount << " interior matches.\n";

        if (nonZeroCount != static_cast<size_t>(kTargetW) * kTargetH ||
            interiorExactCount != interiorTotalCount) {
            std::cerr << "[FAIL] Frame " << frame << " reconstructed image mismatch.\n";
            failed = true;
            break;
        }
    }

    pfnDeviceWaitIdle(device);
    pass.Shutdown();
    rtm.DestroyVulkanResources();

    pfnFreeMem(device, stagingMem, nullptr);
    pfnDestroyBuffer(device, stagingBuf, nullptr);
    pfnDestroyFramebuffer(device, framebuffer, nullptr);
    pfnDestroyRenderPass(device, renderPass, nullptr);
    pfnDestroyCommandPool(device, cmdPool, nullptr);
    pfnDestroyDevice(device, nullptr);
    if (pfnDestroyInstance) pfnDestroyInstance(instance, nullptr);

    if (failed) return 1;
    std::cout << "Headless Vulkan compute reconstruction test passed (Frame 0 & Frame 1).\n";
    return 0;
}

