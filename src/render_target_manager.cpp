#include "cbr/render_target_manager.h"
#include "cbr/logger.h"
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Official Vulkan Specification equivalents for minimal ABI definitions:
// VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5
constexpr uint32_t kVkStructureTypeMemoryAllocateInfo   = 5;
// VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14
constexpr uint32_t kVkStructureTypeImageCreateInfo        = 14;
// VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15
constexpr uint32_t kVkStructureTypeImageViewCreateInfo    = 15;
// VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO = 31
constexpr uint32_t kVkStructureTypeSamplerCreateInfo      = 31;

// VK_IMAGE_TYPE_2D = 1
constexpr uint32_t kVkImageType2D                         = 1;
// VK_IMAGE_VIEW_TYPE_2D = 1
constexpr uint32_t kVkImageViewType2D                     = 1;

// VK_FORMAT_R16G16B16A16_SFLOAT = 97
constexpr uint32_t kVkFormatR16G16B16A16Sfloat            = 97;
// VK_FORMAT_R32_SFLOAT = 100
constexpr uint32_t kVkFormatR32Sfloat                     = 100;

constexpr uint32_t kVkImageTilingOptimal                  = 0;
constexpr uint32_t kVkSharingModeExclusive                = 0;
constexpr uint32_t kVkImageLayoutUndefined                = 0;
constexpr uint32_t kVkSampleCount1Bit                     = 0x00000001;
constexpr uint32_t kVkSampleCount2Bit                     = 0x00000002;

constexpr uint32_t kVkImageUsageTransferSrcBit            = 0x00000001;
constexpr uint32_t kVkImageUsageTransferDstBit            = 0x00000002;
constexpr uint32_t kVkImageUsageSampledBit                = 0x00000004;
constexpr uint32_t kVkImageUsageStorageBit                = 0x00000008;

constexpr uint32_t kVkImageAspectColorBit                 = 0x00000001;
constexpr uint32_t kVkImageAspectDepthBit                 = 0x00000002;

constexpr uint32_t kVkMemoryPropertyDeviceLocalBit        = 0x00000001;

constexpr uint32_t kVkFilterNearest                       = 0;
constexpr uint32_t kVkFilterLinear                        = 1;
constexpr uint32_t kVkSamplerMipmapModeLinear             = 1;
constexpr uint32_t kVkSamplerAddressModeClampToEdge       = 2;
constexpr uint32_t kVkBorderColorFloatTransparentBlack    = 0;

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
    uint32_t          sharingMode;
    uint32_t          queueFamilyIndexCount;
    const uint32_t*   pQueueFamilyIndices;
    uint32_t          initialLayout;
};

struct MinimalVkMemoryRequirements {
    uint64_t size;
    uint64_t alignment;
    uint32_t memoryTypeBits;
};

struct MinimalVkMemoryAllocateInfo {
    uint32_t    sType;
    const void* pNext;
    uint64_t    allocationSize;
    uint32_t    memoryTypeIndex;
};

struct MinimalVkComponentMapping {
    uint32_t r;
    uint32_t g;
    uint32_t b;
    uint32_t a;
};

struct MinimalVkImageSubresourceRange {
    uint32_t aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
};

struct MinimalVkImageViewCreateInfo {
    uint32_t                       sType;
    const void*                    pNext;
    uint32_t                       flags;
    void*                          image;
    uint32_t                       viewType;
    uint32_t                       format;
    MinimalVkComponentMapping      components;
    MinimalVkImageSubresourceRange subresourceRange;
};

struct MinimalVkSamplerCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    magFilter;
    uint32_t    minFilter;
    uint32_t    mipmapMode;
    uint32_t    addressModeU;
    uint32_t    addressModeV;
    uint32_t    addressModeW;
    float       mipLodBias;
    uint32_t    anisotropyEnable;
    float       maxAnisotropy;
    uint32_t    compareEnable;
    uint32_t    compareOp;
    float       minLod;
    float       maxLod;
    uint32_t    borderColor;
    uint32_t    unnormalizedCoordinates;
};

struct MinimalVkMemoryType {
    uint32_t propertyFlags;
    uint32_t heapIndex;
};

struct MinimalVkMemoryHeap {
    uint64_t size;
    uint32_t flags;
};

struct MinimalVkPhysicalDeviceMemoryProperties {
    uint32_t            memoryTypeCount;
    MinimalVkMemoryType memoryTypes[32];
    uint32_t            memoryHeapCount;
    MinimalVkMemoryHeap memoryHeaps[16];
};

typedef void* (*PFN_vkGetDeviceProcAddr)(void* device, const char* pName);
typedef int   (*PFN_vkCreateImage)(void* device, const MinimalVkImageCreateInfo* pCreateInfo, const void* pAllocator, void** pImage);
typedef void  (*PFN_vkDestroyImage)(void* device, void* image, const void* pAllocator);
typedef void  (*PFN_vkGetImageMemoryRequirements)(void* device, void* image, MinimalVkMemoryRequirements* pMemoryRequirements);
typedef int   (*PFN_vkAllocateMemory)(void* device, const MinimalVkMemoryAllocateInfo* pAllocateInfo, const void* pAllocator, void** pMemory);
typedef void  (*PFN_vkFreeMemory)(void* device, void* memory, const void* pAllocator);
typedef int   (*PFN_vkBindImageMemory)(void* device, void* image, void* memory, uint64_t memoryOffset);
typedef int   (*PFN_vkCreateImageView)(void* device, const MinimalVkImageViewCreateInfo* pCreateInfo, const void* pAllocator, void** pView);
typedef void  (*PFN_vkDestroyImageView)(void* device, void* imageView, const void* pAllocator);
typedef int   (*PFN_vkCreateSampler)(void* device, const MinimalVkSamplerCreateInfo* pCreateInfo, const void* pAllocator, void** pSampler);
typedef void  (*PFN_vkDestroySampler)(void* device, void* sampler, const void* pAllocator);
typedef void  (*PFN_vkGetPhysicalDeviceMemoryProperties)(void* physicalDevice, MinimalVkPhysicalDeviceMemoryProperties* pMemoryProperties);

} // namespace

namespace cbr {

RenderTargetManager& RenderTargetManager::Get() {
    static RenderTargetManager instance;
    return instance;
}

void RenderTargetManager::Initialize(uint32_t width, uint32_t height) {
    if (width < 2 || height < 2 || width > 16384 || height > 16384) {
        CBR_LOG_ERROR("RenderTargetManager::Initialize rejected invalid %ux%u (must be 2..16384).", width, height);
        return;
    }
    TargetDimensions dims{};
    dims.fullWidth = width;
    dims.fullHeight = height;
    dims.quarterWidth = width / 2;
    dims.quarterHeight = height / 2;
    dims.msaaSamples = 2;
    dims.invFullWidth = (width > 0) ? (1.0f / static_cast<float>(width)) : 0.0f;
    dims.invFullHeight = (height > 0) ? (1.0f / static_cast<float>(height)) : 0.0f;
    m_historyPingPong.store(0);

    // Calculate VRAM footprint:
    size_t qColor = static_cast<size_t>(dims.quarterWidth) * dims.quarterHeight * 8 * 2;
    size_t qDepth = static_cast<size_t>(dims.quarterWidth) * dims.quarterHeight * 4 * 2;
    size_t histColor = static_cast<size_t>(dims.fullWidth) * dims.fullHeight * 8 * 2;
    size_t histDepth = static_cast<size_t>(dims.fullWidth) * dims.fullHeight * 4 * 2;
    size_t outColor = static_cast<size_t>(dims.fullWidth) * dims.fullHeight * 8;

    const size_t total = qColor + qDepth + histColor + histDepth + outColor;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_dims = dims;
        m_totalAllocatedVramBytes.store(total);
        m_initialized.store(true);
    }

    CBR_LOG_INFO("RenderTargetManager initialized for target: %ux%u", width, height);
    CBR_LOG_INFO("Quarter-Resolution 2x MSAA Buffer size: %ux%u", dims.quarterWidth, dims.quarterHeight);
    CBR_LOG_INFO("Total CBR VRAM Footprint: %.2f MiB (%.2f MB)",
        static_cast<double>(total) / (1024.0 * 1024.0),
        static_cast<double>(total) / 1000000.0);
}

void RenderTargetManager::Shutdown() {
    DestroyVulkanResources();
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_initialized.store(false);
        m_totalAllocatedVramBytes.store(0);
    }
    CBR_LOG_INFO("RenderTargetManager shut down.");
}

bool RenderTargetManager::CreateVulkanResources(void* vkDevice, void* vkPhysicalDevice) {
    if (!vkDevice) {
        return false;
    }

#if defined(_WIN32)
    DestroyVulkanResources();
    m_vkDevice = vkDevice;

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) {
        CBR_LOG_WARN("CreateVulkanResources: vulkan-1.dll not loaded.");
        return false;
    }

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && vkDevice) {
            void* p = pfnGetDeviceProcAddr(vkDevice, name);
            if (p) return p;
        }
        return reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
    };

    auto pfnCreateImage = reinterpret_cast<PFN_vkCreateImage>(resolveDeviceFunc("vkCreateImage"));
    auto pfnDeviceWaitIdle = reinterpret_cast<int (*)(void*)>(resolveDeviceFunc("vkDeviceWaitIdle"));
    if (m_vkDevice && pfnDeviceWaitIdle) {
        pfnDeviceWaitIdle(m_vkDevice);
    }

    auto pfnDestroyImage = reinterpret_cast<PFN_vkDestroyImage>(resolveDeviceFunc("vkDestroyImage"));
    auto pfnGetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(resolveDeviceFunc("vkGetImageMemoryRequirements"));
    auto pfnAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(resolveDeviceFunc("vkAllocateMemory"));
    auto pfnFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(resolveDeviceFunc("vkFreeMemory"));
    auto pfnBindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(resolveDeviceFunc("vkBindImageMemory"));
    auto pfnCreateImageView = reinterpret_cast<PFN_vkCreateImageView>(resolveDeviceFunc("vkCreateImageView"));
    auto pfnCreateSampler = reinterpret_cast<PFN_vkCreateSampler>(resolveDeviceFunc("vkCreateSampler"));
    auto pfnGetPhysMemProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        GetProcAddress(vulkanModule, "vkGetPhysicalDeviceMemoryProperties"));

    if (!pfnCreateImage || !pfnGetImageMemoryRequirements || !pfnAllocateMemory ||
        !pfnBindImageMemory || !pfnCreateImageView || !pfnCreateSampler) {
        CBR_LOG_ERROR("CreateVulkanResources: failed to resolve required Vulkan allocation functions.");
        return false;
    }

    MinimalVkPhysicalDeviceMemoryProperties memProps{};
    bool hasMemProps = false;
    if (vkPhysicalDevice && pfnGetPhysMemProps) {
        pfnGetPhysMemProps(vkPhysicalDevice, &memProps);
        hasMemProps = true;
    }

    auto findMemoryType = [&](uint32_t typeBits, uint32_t properties) -> uint32_t {
        if (hasMemProps) {
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((typeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
        }
        for (uint32_t i = 0; i < 32; ++i) {
            if (typeBits & (1u << i)) return i;
        }
        return 0;
    };

    const TargetDimensions dims = GetDimensions();
    size_t totalDriverVram = 0;

    auto createTexture = [&](uint32_t width, uint32_t height, uint32_t format,
                             uint32_t usage, uint32_t aspectMask, VulkanTextureResource& out,
                             uint32_t samples = kVkSampleCount1Bit) -> bool {
        MinimalVkImageCreateInfo imageInfo{};
        imageInfo.sType = kVkStructureTypeImageCreateInfo;
        imageInfo.imageType = kVkImageType2D;
        imageInfo.format = format;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = samples;
        imageInfo.tiling = kVkImageTilingOptimal;
        imageInfo.usage = usage;
        imageInfo.sharingMode = kVkSharingModeExclusive;
        imageInfo.initialLayout = kVkImageLayoutUndefined;

        void* image = nullptr;
        if (pfnCreateImage(vkDevice, &imageInfo, nullptr, &image) != 0 || !image) {
            return false;
        }

        MinimalVkMemoryRequirements memReqs{};
        pfnGetImageMemoryRequirements(vkDevice, image, &memReqs);

        MinimalVkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = kVkStructureTypeMemoryAllocateInfo;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, kVkMemoryPropertyDeviceLocalBit);

        void* memory = nullptr;
        if (pfnAllocateMemory(vkDevice, &allocInfo, nullptr, &memory) != 0 || !memory) {
            pfnDestroyImage(vkDevice, image, nullptr);
            return false;
        }

        pfnBindImageMemory(vkDevice, image, memory, 0);

        // Support true depth aspect mask if a native depth format is requested
        constexpr uint32_t kVkFormatD16Unorm = 124;
        constexpr uint32_t kVkFormatD32Sfloat = 126;
        constexpr uint32_t kVkFormatD24UnormS8Uint = 129;
        if (format == kVkFormatD16Unorm || format == kVkFormatD32Sfloat || format == kVkFormatD24UnormS8Uint) {
            aspectMask = kVkImageAspectDepthBit;
        }

        MinimalVkImageViewCreateInfo viewInfo{};
        viewInfo.sType = kVkStructureTypeImageViewCreateInfo;
        viewInfo.image = image;
        viewInfo.viewType = kVkImageViewType2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        void* view = nullptr;
        if (pfnCreateImageView(vkDevice, &viewInfo, nullptr, &view) != 0 || !view) {
            pfnFreeMemory(vkDevice, memory, nullptr);
            pfnDestroyImage(vkDevice, image, nullptr);
            return false;
        }

        totalDriverVram += memReqs.size;

        out.image = image;
        out.memory = memory;
        out.view = view;
        out.width = width;
        out.height = height;
        out.format = format;
        return true;
    };

    constexpr uint32_t colorUsage = kVkImageUsageSampledBit | kVkImageUsageStorageBit |
                                    kVkImageUsageTransferSrcBit | kVkImageUsageTransferDstBit;
    constexpr uint32_t depthUsage = kVkImageUsageSampledBit | kVkImageUsageStorageBit |
                                    kVkImageUsageTransferSrcBit | kVkImageUsageTransferDstBit;

    // 1. Create Quarter-Res 2x MSAA / Input Color, Depth, and Velocity targets
    // Binding 0 & 1 in cbr_reconstruct.comp sample these as sampler2DMS (2x MSAA required)
    if (!createTexture(dims.quarterWidth, dims.quarterHeight, kVkFormatR16G16B16A16Sfloat,
                       colorUsage, kVkImageAspectColorBit, m_vkResources.quarterColor, kVkSampleCount2Bit)) {
        CBR_LOG_ERROR("Failed to allocate device-local QuarterColor buffer (2x MSAA)");
        return false;
    }
    if (!createTexture(dims.quarterWidth, dims.quarterHeight, kVkFormatR32Sfloat,
                       depthUsage, kVkImageAspectColorBit, m_vkResources.quarterDepth, kVkSampleCount2Bit)) {
        CBR_LOG_ERROR("Failed to allocate device-local QuarterDepth buffer (2x MSAA)");
        return false;
    }
    if (!createTexture(dims.quarterWidth, dims.quarterHeight, kVkFormatR16G16B16A16Sfloat,
                       colorUsage, kVkImageAspectColorBit, m_vkResources.velocity)) {
        CBR_LOG_ERROR("Failed to allocate device-local Velocity buffer");
        return false;
    }

    // 2. Create Ping-Pong History Color (A & B)
    for (int i = 0; i < 2; ++i) {
        if (!createTexture(dims.fullWidth, dims.fullHeight, kVkFormatR16G16B16A16Sfloat,
                           colorUsage, kVkImageAspectColorBit, m_vkResources.historyColor[i])) {
            CBR_LOG_ERROR("Failed to allocate device-local HistoryColor buffer [%d]", i);
            return false;
        }
    }

    // 3. Create Ping-Pong History Depth (A & B)
    for (int i = 0; i < 2; ++i) {
        if (!createTexture(dims.fullWidth, dims.fullHeight, kVkFormatR32Sfloat,
                           depthUsage, kVkImageAspectColorBit, m_vkResources.historyDepth[i])) {
            CBR_LOG_ERROR("Failed to allocate device-local HistoryDepth buffer [%d]", i);
            return false;
        }
    }

    // 4. Create Full-Res Output Storage Color & Depth
    if (!createTexture(dims.fullWidth, dims.fullHeight, kVkFormatR16G16B16A16Sfloat,
                       colorUsage, kVkImageAspectColorBit, m_vkResources.outputColor)) {
        CBR_LOG_ERROR("Failed to allocate device-local OutputColor image");
        return false;
    }
    if (!createTexture(dims.fullWidth, dims.fullHeight, kVkFormatR32Sfloat,
                       depthUsage, kVkImageAspectColorBit, m_vkResources.outputDepth)) {
        CBR_LOG_ERROR("Failed to allocate device-local OutputDepth image");
        return false;
    }

    // 5. Create Linear & Point Samplers (Clamp-to-Edge)
    MinimalVkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = kVkStructureTypeSamplerCreateInfo;
    samplerInfo.magFilter = kVkFilterLinear;
    samplerInfo.minFilter = kVkFilterLinear;
    samplerInfo.mipmapMode = kVkSamplerMipmapModeLinear;
    samplerInfo.addressModeU = kVkSamplerAddressModeClampToEdge;
    samplerInfo.addressModeV = kVkSamplerAddressModeClampToEdge;
    samplerInfo.addressModeW = kVkSamplerAddressModeClampToEdge;
    samplerInfo.borderColor = kVkBorderColorFloatTransparentBlack;
    samplerInfo.maxLod = 1.0f;

    void* linearSampler = nullptr;
    pfnCreateSampler(vkDevice, &samplerInfo, nullptr, &linearSampler);
    m_vkResources.linearSampler = linearSampler;

    samplerInfo.magFilter = kVkFilterNearest;
    samplerInfo.minFilter = kVkFilterNearest;
    void* pointSampler = nullptr;
    pfnCreateSampler(vkDevice, &samplerInfo, nullptr, &pointSampler);
    m_vkResources.pointSampler = pointSampler;

    m_totalAllocatedVramBytes.store(totalDriverVram);
    m_vkResourcesCreated.store(true);
    CBR_LOG_INFO("Vulkan device-local render targets & samplers instantiated (%ux%u). Driver VRAM: %.2f MiB (%.2f MB)",
                 dims.fullWidth, dims.fullHeight,
                 static_cast<double>(totalDriverVram) / (1024.0 * 1024.0),
                 static_cast<double>(totalDriverVram) / 1000000.0);
    return true;
#else
    m_vkResourcesCreated.store(true);
    return true;
#endif
}

void RenderTargetManager::DestroyVulkanResources() {
#if defined(_WIN32)
    if (!m_vkDevice || !m_vkResourcesCreated.load()) {
        return;
    }

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) return;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_vkDevice) {
            void* p = pfnGetDeviceProcAddr(m_vkDevice, name);
            if (p) return p;
        }
        return reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
    };

    auto pfnDeviceWaitIdle = reinterpret_cast<int (*)(void*)>(resolveDeviceFunc("vkDeviceWaitIdle"));
    if (m_vkDevice && pfnDeviceWaitIdle) {
        pfnDeviceWaitIdle(m_vkDevice);
    }

    auto pfnDestroyImage = reinterpret_cast<PFN_vkDestroyImage>(resolveDeviceFunc("vkDestroyImage"));
    auto pfnFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(resolveDeviceFunc("vkFreeMemory"));
    auto pfnDestroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(resolveDeviceFunc("vkDestroyImageView"));
    auto pfnDestroySampler = reinterpret_cast<PFN_vkDestroySampler>(resolveDeviceFunc("vkDestroySampler"));

    auto destroyTexture = [&](VulkanTextureResource& tex) {
        if (pfnDestroyImageView && tex.view) {
            pfnDestroyImageView(m_vkDevice, tex.view, nullptr);
            tex.view = nullptr;
        }
        if (pfnFreeMemory && tex.memory) {
            pfnFreeMemory(m_vkDevice, tex.memory, nullptr);
            tex.memory = nullptr;
        }
        if (pfnDestroyImage && tex.image) {
            pfnDestroyImage(m_vkDevice, tex.image, nullptr);
            tex.image = nullptr;
        }
    };

    destroyTexture(m_vkResources.quarterColor);
    destroyTexture(m_vkResources.quarterDepth);
    destroyTexture(m_vkResources.velocity);
    for (int i = 0; i < 2; ++i) {
        destroyTexture(m_vkResources.historyColor[i]);
        destroyTexture(m_vkResources.historyDepth[i]);
    }
    destroyTexture(m_vkResources.outputColor);
    destroyTexture(m_vkResources.outputDepth);

    if (pfnDestroySampler) {
        if (m_vkResources.linearSampler) {
            pfnDestroySampler(m_vkDevice, m_vkResources.linearSampler, nullptr);
            m_vkResources.linearSampler = nullptr;
        }
        if (m_vkResources.pointSampler) {
            pfnDestroySampler(m_vkDevice, m_vkResources.pointSampler, nullptr);
            m_vkResources.pointSampler = nullptr;
        }
    }

    m_vkResourcesCreated.store(false);
    m_vkDevice = nullptr;
    CBR_LOG_INFO("Vulkan render targets & samplers released.");
#else
    m_vkResourcesCreated.store(false);
#endif
}

bool RenderTargetManager::IsTargetInterceptCandidate(uint32_t width, uint32_t height, uint32_t format) const {
    if (!m_initialized.load()) return false;
    (void)format;
    const TargetDimensions dims = GetDimensions();
    return (width == dims.fullWidth) && (height == dims.fullHeight);
}

bool RenderTargetManager::IsQuarterPassCandidate(uint32_t width, uint32_t height) const {
    if (!m_initialized.load()) return false;
    const TargetDimensions dims = GetDimensions();
    return (width == dims.quarterWidth && height == dims.quarterHeight);
}

} // namespace cbr
