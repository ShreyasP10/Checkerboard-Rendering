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
#else
#include <dlfcn.h>
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
// VK_FORMAT_D32_SFLOAT = 126
constexpr uint32_t kVkFormatD32Sfloat                     = 126;

constexpr uint32_t kVkImageTilingOptimal                  = 0;
constexpr uint32_t kVkSharingModeExclusive                = 0;
constexpr uint32_t kVkImageLayoutUndefined                = 0;
constexpr uint32_t kVkSampleCount1Bit                     = 0x00000001;
constexpr uint32_t kVkSampleCount2Bit                     = 0x00000002;

constexpr uint32_t kVkImageUsageTransferSrcBit            = 0x00000001;
constexpr uint32_t kVkImageUsageTransferDstBit            = 0x00000002;
constexpr uint32_t kVkImageUsageSampledBit                = 0x00000004;
constexpr uint32_t kVkImageUsageStorageBit                = 0x00000008;
constexpr uint32_t kVkImageUsageColorAttachmentBit        = 0x00000010;
constexpr uint32_t kVkImageUsageDepthStencilAttachmentBit = 0x00000020;

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

inline void* GetVulkanLoaderModule() {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetModuleHandleA("vulkan-1.dll"));
#else
    void* h = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!h) {
        static void* s_vkLib = []() -> void* {
            void* lib = dlopen("libvulkan.so.1", RTLD_LAZY);
            if (!lib) lib = dlopen("libvulkan.so", RTLD_LAZY);
            return lib;
        }();
        h = s_vkLib;
    }
    return h;
#endif
}

inline void* GetVulkanLoaderSymbol(void* mod, const char* name) {
    if (!mod || !name) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(mod), name));
#else
    return dlsym(mod, name);
#endif
}

#if defined(_WIN32)
// Minimal headerless D3D12 ABI structures (Item H-02)
constexpr uint32_t kDxgiFormatR16G16B16A16Float     = 10;
constexpr uint32_t kDxgiFormatR32Float              = 41;
constexpr uint32_t kD3D12HeapTypeDefault            = 1;
constexpr uint32_t kD3D12ResourceDimensionTexture2D = 3;
constexpr uint32_t kD3D12TextureLayoutUnknown       = 0;
constexpr uint32_t kD3D12ResourceFlagNone           = 0x0;
constexpr uint32_t kD3D12ResourceFlagAllowUAV       = 0x4;
constexpr uint32_t kD3D12ResourceStateCommon        = 0x0;
constexpr uint32_t kD3D12ResourceStateNonPixelSRV   = 0x40;
constexpr uint32_t kD3D12ResourceStateUAV           = 0x8;
constexpr uint32_t kD3D12DescriptorHeapTypeCbvSrvUav = 0;
constexpr uint32_t kD3D12DescriptorHeapTypeSampler   = 1;
constexpr uint32_t kD3D12DescriptorHeapFlagShaderVisible = 0x1;
constexpr uint32_t kD3D12FilterMinMagMipPoint       = 0x0;
constexpr uint32_t kD3D12FilterMinMagMipLinear      = 0x15;
constexpr uint32_t kD3D12TextureAddressClamp        = 3;
constexpr uint32_t kD3D12ComparisonNever            = 1;

struct MinimalD3D12HeapProperties {
    uint32_t Type;
    uint32_t CPUPageProperty;
    uint32_t MemoryPoolPreference;
    uint32_t CreationNodeMask;
    uint32_t VisibleNodeMask;
};
static_assert(sizeof(MinimalD3D12HeapProperties) == 20, "D3D12_HEAP_PROPERTIES size mismatch");

struct MinimalDxgiSampleDesc {
    uint32_t Count;
    uint32_t Quality;
};

struct MinimalD3D12ResourceDesc {
    uint32_t              Dimension;
    uint32_t              _pad0{ 0 };
    uint64_t              Alignment;
    uint64_t              Width;
    uint32_t              Height;
    uint16_t              DepthOrArraySize;
    uint16_t              MipLevels;
    uint32_t              Format;
    MinimalDxgiSampleDesc SampleDesc;
    uint32_t              Layout;
    uint32_t              Flags;
};
static_assert(sizeof(MinimalD3D12ResourceDesc) == 56, "D3D12_RESOURCE_DESC size mismatch");

struct MinimalD3D12DescriptorHeapDesc {
    uint32_t Type;
    uint32_t NumDescriptors;
    uint32_t Flags;
    uint32_t NodeMask;
};
static_assert(sizeof(MinimalD3D12DescriptorHeapDesc) == 16, "D3D12_DESCRIPTOR_HEAP_DESC size mismatch");

struct MinimalD3D12SamplerDesc {
    uint32_t Filter;
    uint32_t AddressU;
    uint32_t AddressV;
    uint32_t AddressW;
    float    MipLODBias;
    uint32_t MaxAnisotropy;
    uint32_t ComparisonFunc;
    float    BorderColor[4];
    float    MinLOD;
    float    MaxLOD;
};
static_assert(sizeof(MinimalD3D12SamplerDesc) == 52, "D3D12_SAMPLER_DESC size mismatch");

struct MinimalD3D12CpuDescriptorHandle {
    size_t ptr;
};

struct MinimalD3D12GpuDescriptorHandle {
    uint64_t ptr;
};

inline void ReleaseD3D12Com(void*& obj) {
    if (!obj) return;
    typedef ULONG (__stdcall *PFN_Release)(void*);
    void** vmt = *reinterpret_cast<void***>(obj);
    if (vmt && vmt[2]) {
        reinterpret_cast<PFN_Release>(vmt[2])(obj);
    }
    obj = nullptr;
}
#endif

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
    DestroyDX12Resources();
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

    DestroyVulkanResources();
    m_vkDevice = vkDevice;

    void* vulkanModule = GetVulkanLoaderModule();
    if (!vulkanModule) {
        CBR_LOG_WARN("CreateVulkanResources: Vulkan loader library not loaded.");
        m_vkDevice = nullptr;
        return false;
    }

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && vkDevice) {
            void* p = pfnGetDeviceProcAddr(vkDevice, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanModule, name);
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
        GetVulkanLoaderSymbol(vulkanModule, "vkGetPhysicalDeviceMemoryProperties"));

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
        out.aspectMask = aspectMask;
        out.currentLayout = kVkImageLayoutUndefined;
        return true;
    };

    // 2x MSAA sampled inputs must NOT carry STORAGE_BIT (VUID-VkImageCreateInfo-samples-02258).
    constexpr uint32_t msaaInputUsage = kVkImageUsageSampledBit | kVkImageUsageTransferSrcBit |
                                        kVkImageUsageTransferDstBit | kVkImageUsageColorAttachmentBit;
    constexpr uint32_t msaaDepthUsage = kVkImageUsageSampledBit | kVkImageUsageTransferSrcBit |
                                        kVkImageUsageTransferDstBit | kVkImageUsageDepthStencilAttachmentBit;
    constexpr uint32_t colorUsage = kVkImageUsageSampledBit | kVkImageUsageStorageBit |
                                    kVkImageUsageTransferSrcBit | kVkImageUsageTransferDstBit;
    constexpr uint32_t depthUsage = kVkImageUsageSampledBit | kVkImageUsageStorageBit |
                                    kVkImageUsageTransferSrcBit | kVkImageUsageTransferDstBit;

    // 1. Create Quarter-Res 2x MSAA / Input Color, Depth, and Velocity targets
    // Binding 0 & 1 in cbr_reconstruct.comp sample these as sampler2DMS (2x MSAA required)
    if (!createTexture(dims.quarterWidth, dims.quarterHeight, kVkFormatR16G16B16A16Sfloat,
                       msaaInputUsage, kVkImageAspectColorBit, m_vkResources.quarterColor, kVkSampleCount2Bit)) {
        CBR_LOG_ERROR("Failed to allocate device-local QuarterColor buffer (2x MSAA)");
        return false;
    }
    if (!createTexture(dims.quarterWidth, dims.quarterHeight, kVkFormatD32Sfloat,
                       msaaDepthUsage, kVkImageAspectDepthBit, m_vkResources.quarterDepth, kVkSampleCount2Bit)) {
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
}

void RenderTargetManager::DestroyVulkanResources() {
    if (!m_vkDevice || !m_vkResourcesCreated.load()) {
        return;
    }

    void* vulkanModule = GetVulkanLoaderModule();
    if (!vulkanModule) return;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_vkDevice) {
            void* p = pfnGetDeviceProcAddr(m_vkDevice, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanModule, name);
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

    m_vkResources = {};
    m_vkResourcesCreated.store(false);
    m_vkDevice = nullptr;
    CBR_LOG_INFO("Vulkan render targets & samplers released.");
}

void RenderTargetManager::SetVulkanImageLayout(void* vkImage, uint32_t newLayout) {
    if (!vkImage) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    VulkanTextureResource* const targets[] = {
        &m_vkResources.quarterColor,
        &m_vkResources.quarterDepth,
        &m_vkResources.velocity,
        &m_vkResources.historyColor[0],
        &m_vkResources.historyColor[1],
        &m_vkResources.historyDepth[0],
        &m_vkResources.historyDepth[1],
        &m_vkResources.outputColor,
        &m_vkResources.outputDepth
    };
    for (VulkanTextureResource* tex : targets) {
        if (tex->image == vkImage) {
            tex->currentLayout = newLayout;
        }
    }
}

uint32_t RenderTargetManager::GetVulkanImageLayout(void* vkImage) const {
    if (!vkImage) return 0;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const VulkanTextureResource* const targets[] = {
        &m_vkResources.quarterColor,
        &m_vkResources.quarterDepth,
        &m_vkResources.velocity,
        &m_vkResources.historyColor[0],
        &m_vkResources.historyColor[1],
        &m_vkResources.historyDepth[0],
        &m_vkResources.historyDepth[1],
        &m_vkResources.outputColor,
        &m_vkResources.outputDepth
    };
    for (const VulkanTextureResource* tex : targets) {
        if (tex->image == vkImage) {
            return tex->currentLayout;
        }
    }
    return 0;
}

bool RenderTargetManager::CreateDX12Resources(void* d3d12Device) {
    if (!d3d12Device) {
        return false;
    }

#if defined(_WIN32)
    DestroyDX12Resources();
    m_dx12Device = d3d12Device;

    void** vmt = *reinterpret_cast<void***>(d3d12Device);
    if (!vmt) {
        m_dx12Device = nullptr;
        return false;
    }

    // Verified ID3D12Device COM vtable indices (Item H-02)
    typedef long     (__stdcall *PFN_CreateDescriptorHeap)(void*, const MinimalD3D12DescriptorHeapDesc*, const GUID*, void**);
    typedef uint32_t (__stdcall *PFN_GetDescriptorHandleIncrementSize)(void*, uint32_t);
    typedef void     (__stdcall *PFN_CreateShaderResourceView)(void*, void*, const void*, MinimalD3D12CpuDescriptorHandle);
    typedef void     (__stdcall *PFN_CreateUnorderedAccessView)(void*, void*, void*, const void*, MinimalD3D12CpuDescriptorHandle);
    typedef void     (__stdcall *PFN_CreateSampler)(void*, const MinimalD3D12SamplerDesc*, MinimalD3D12CpuDescriptorHandle);
    typedef long     (__stdcall *PFN_CreateCommittedResource)(void*, const MinimalD3D12HeapProperties*, uint32_t,
                                                              const MinimalD3D12ResourceDesc*, uint32_t, const void*,
                                                              const GUID*, void**);

    auto pfnCreateHeap      = reinterpret_cast<PFN_CreateDescriptorHeap>(vmt[14]);
    auto pfnGetIncSize      = reinterpret_cast<PFN_GetDescriptorHandleIncrementSize>(vmt[15]);
    auto pfnCreateSRV       = reinterpret_cast<PFN_CreateShaderResourceView>(vmt[18]);
    auto pfnCreateUAV       = reinterpret_cast<PFN_CreateUnorderedAccessView>(vmt[19]);
    auto pfnCreateSampler   = reinterpret_cast<PFN_CreateSampler>(vmt[22]);
    auto pfnCreateCommitted = reinterpret_cast<PFN_CreateCommittedResource>(vmt[27]);

    if (!pfnCreateHeap || !pfnGetIncSize || !pfnCreateSRV || !pfnCreateUAV || !pfnCreateSampler || !pfnCreateCommitted) {
        CBR_LOG_ERROR("CreateDX12Resources: missing ID3D12Device vtable entries.");
        m_dx12Device = nullptr;
        return false;
    }

    static const GUID kIID_ID3D12Resource =
        { 0x696442be, 0xa72e, 0x4059, { 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad } };
    static const GUID kIID_ID3D12DescriptorHeap =
        { 0x8efb471d, 0x616c, 0x4f49, { 0x90, 0xf7, 0x12, 0x7b, 0xb7, 0x63, 0xfa, 0x51 } };

    const TargetDimensions dims = GetDimensions();
    if (dims.quarterWidth == 0u || dims.quarterHeight == 0u || dims.fullWidth == 0u || dims.fullHeight == 0u) {
        m_dx12Device = nullptr;
        return false;
    }

    auto createTex = [&](uint32_t width, uint32_t height, uint32_t format,
                         uint32_t samples, uint32_t flags, uint32_t initialState,
                         DX12TextureResource& outTex) -> bool {
        MinimalD3D12HeapProperties heapProps{};
        heapProps.Type = kD3D12HeapTypeDefault;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        MinimalD3D12ResourceDesc desc{};
        desc.Dimension = kD3D12ResourceDimensionTexture2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc = { samples, 0 };
        desc.Layout = kD3D12TextureLayoutUnknown;
        desc.Flags = flags;

        void* resource = nullptr;
        const long hr = pfnCreateCommitted(d3d12Device, &heapProps, 0, &desc, initialState,
                                           nullptr, &kIID_ID3D12Resource, &resource);
        if (hr < 0 || !resource) {
            return false;
        }
        outTex.resource = resource;
        outTex.width = width;
        outTex.height = height;
        outTex.format = format;
        outTex.samples = samples;
        outTex.currentState = initialState;
        return true;
    };

    if (!createTex(dims.quarterWidth, dims.quarterHeight, kDxgiFormatR16G16B16A16Float,
                   kVkSampleCount2Bit, kD3D12ResourceFlagNone, kD3D12ResourceStateNonPixelSRV,
                   m_dx12Resources.quarterColor) ||
        !createTex(dims.quarterWidth, dims.quarterHeight, kDxgiFormatR32Float,
                   kVkSampleCount2Bit, kD3D12ResourceFlagNone, kD3D12ResourceStateNonPixelSRV,
                   m_dx12Resources.quarterDepth) ||
        !createTex(dims.quarterWidth, dims.quarterHeight, kDxgiFormatR16G16B16A16Float,
                   1, kD3D12ResourceFlagNone, kD3D12ResourceStateNonPixelSRV,
                   m_dx12Resources.velocity) ||
        !createTex(dims.fullWidth, dims.fullHeight, kDxgiFormatR16G16B16A16Float,
                   1, kD3D12ResourceFlagAllowUAV, kD3D12ResourceStateNonPixelSRV,
                   m_dx12Resources.historyColor[0]) ||
        !createTex(dims.fullWidth, dims.fullHeight, kDxgiFormatR16G16B16A16Float,
                   1, kD3D12ResourceFlagAllowUAV, kD3D12ResourceStateUAV,
                   m_dx12Resources.historyColor[1]) ||
        !createTex(dims.fullWidth, dims.fullHeight, kDxgiFormatR32Float,
                   1, kD3D12ResourceFlagAllowUAV, kD3D12ResourceStateNonPixelSRV,
                   m_dx12Resources.historyDepth[0]) ||
        !createTex(dims.fullWidth, dims.fullHeight, kDxgiFormatR32Float,
                   1, kD3D12ResourceFlagAllowUAV, kD3D12ResourceStateUAV,
                   m_dx12Resources.historyDepth[1]) ||
        !createTex(dims.fullWidth, dims.fullHeight, kDxgiFormatR16G16B16A16Float,
                   1, kD3D12ResourceFlagAllowUAV, kD3D12ResourceStateUAV,
                   m_dx12Resources.outputColor) ||
        !createTex(dims.fullWidth, dims.fullHeight, kDxgiFormatR32Float,
                   1, kD3D12ResourceFlagAllowUAV, kD3D12ResourceStateUAV,
                   m_dx12Resources.outputDepth)) {
        CBR_LOG_ERROR("CreateDX12Resources: failed to allocate one or more D3D12 textures.");
        DestroyDX12Resources();
        return false;
    }

    // Create shader-visible CBV/SRV/UAV heap (2 ping-pong sets x (5 SRVs + 2 UAVs) = 14 descriptors)
    MinimalD3D12DescriptorHeapDesc srvHeapDesc{};
    srvHeapDesc.Type = kD3D12DescriptorHeapTypeCbvSrvUav;
    srvHeapDesc.NumDescriptors = 14;
    srvHeapDesc.Flags = kD3D12DescriptorHeapFlagShaderVisible;

    if (pfnCreateHeap(d3d12Device, &srvHeapDesc, &kIID_ID3D12DescriptorHeap, &m_dx12Resources.srvUavHeap) < 0 ||
        !m_dx12Resources.srvUavHeap) {
        CBR_LOG_ERROR("CreateDX12Resources: failed to create CBV_SRV_UAV descriptor heap.");
        DestroyDX12Resources();
        return false;
    }

    // Create shader-visible Sampler heap (s0 = LinearClamp, s1 = PointClamp)
    MinimalD3D12DescriptorHeapDesc samplerHeapDesc{};
    samplerHeapDesc.Type = kD3D12DescriptorHeapTypeSampler;
    samplerHeapDesc.NumDescriptors = 2;
    samplerHeapDesc.Flags = kD3D12DescriptorHeapFlagShaderVisible;

    if (pfnCreateHeap(d3d12Device, &samplerHeapDesc, &kIID_ID3D12DescriptorHeap, &m_dx12Resources.samplerHeap) < 0 ||
        !m_dx12Resources.samplerHeap) {
        CBR_LOG_ERROR("CreateDX12Resources: failed to create SAMPLER descriptor heap.");
        DestroyDX12Resources();
        return false;
    }

    // MSVC x64 C++ COM ABI: struct-returning GetCPUDescriptorHandleForHeapStart/GPU use explicit out-pointer in rdx
    typedef MinimalD3D12CpuDescriptorHandle* (__stdcall *PFN_GetCpuStart)(void*, MinimalD3D12CpuDescriptorHandle*);
    typedef MinimalD3D12GpuDescriptorHandle* (__stdcall *PFN_GetGpuStart)(void*, MinimalD3D12GpuDescriptorHandle*);

    void** srvHeapVmt = *reinterpret_cast<void***>(m_dx12Resources.srvUavHeap);
    void** sampHeapVmt = *reinterpret_cast<void***>(m_dx12Resources.samplerHeap);
    if (!srvHeapVmt || !sampHeapVmt || !srvHeapVmt[9] || !srvHeapVmt[10] || !sampHeapVmt[9] || !sampHeapVmt[10]) {
        DestroyDX12Resources();
        return false;
    }

    MinimalD3D12CpuDescriptorHandle srvCpuStart{};
    MinimalD3D12GpuDescriptorHandle srvGpuStart{};
    reinterpret_cast<PFN_GetCpuStart>(srvHeapVmt[9])(m_dx12Resources.srvUavHeap, &srvCpuStart);
    reinterpret_cast<PFN_GetGpuStart>(srvHeapVmt[10])(m_dx12Resources.srvUavHeap, &srvGpuStart);

    MinimalD3D12CpuDescriptorHandle sampCpuStart{};
    MinimalD3D12GpuDescriptorHandle sampGpuStart{};
    reinterpret_cast<PFN_GetCpuStart>(sampHeapVmt[9])(m_dx12Resources.samplerHeap, &sampCpuStart);
    reinterpret_cast<PFN_GetGpuStart>(sampHeapVmt[10])(m_dx12Resources.samplerHeap, &sampGpuStart);

    const uint32_t srvInc  = pfnGetIncSize(d3d12Device, kD3D12DescriptorHeapTypeCbvSrvUav);
    const uint32_t sampInc = pfnGetIncSize(d3d12Device, kD3D12DescriptorHeapTypeSampler);

    auto cpuAt = [&](uint32_t slot) -> MinimalD3D12CpuDescriptorHandle {
        return { srvCpuStart.ptr + static_cast<size_t>(slot) * srvInc };
    };

    // Populate ping-pong sets: setIdx=0 reads A(0)/writes B(1); setIdx=1 reads B(1)/writes A(0)
    for (uint32_t setIdx = 0; setIdx < 2; ++setIdx) {
        const uint32_t readIdx = setIdx;
        const uint32_t writeIdx = 1u - setIdx;
        const uint32_t baseSlot = setIdx * 7u;

        pfnCreateSRV(d3d12Device, m_dx12Resources.quarterColor.resource, nullptr, cpuAt(baseSlot + 0u));
        pfnCreateSRV(d3d12Device, m_dx12Resources.quarterDepth.resource, nullptr, cpuAt(baseSlot + 1u));
        pfnCreateSRV(d3d12Device, m_dx12Resources.historyColor[readIdx].resource, nullptr, cpuAt(baseSlot + 2u));
        pfnCreateSRV(d3d12Device, m_dx12Resources.historyDepth[readIdx].resource, nullptr, cpuAt(baseSlot + 3u));
        pfnCreateSRV(d3d12Device, m_dx12Resources.velocity.resource, nullptr, cpuAt(baseSlot + 4u));
        pfnCreateUAV(d3d12Device, m_dx12Resources.historyColor[writeIdx].resource, nullptr, nullptr, cpuAt(baseSlot + 5u));
        pfnCreateUAV(d3d12Device, m_dx12Resources.historyDepth[writeIdx].resource, nullptr, nullptr, cpuAt(baseSlot + 6u));

        m_dx12Resources.srvGpuHandle[setIdx] = srvGpuStart.ptr + static_cast<uint64_t>(baseSlot) * srvInc;
        m_dx12Resources.uavGpuHandle[setIdx] = srvGpuStart.ptr + static_cast<uint64_t>(baseSlot + 5u) * srvInc;
    }

    // Populate LinearClamp (s0) & PointClamp (s1) samplers
    MinimalD3D12SamplerDesc linearSamp{};
    linearSamp.Filter = kD3D12FilterMinMagMipLinear;
    linearSamp.AddressU = kD3D12TextureAddressClamp;
    linearSamp.AddressV = kD3D12TextureAddressClamp;
    linearSamp.AddressW = kD3D12TextureAddressClamp;
    linearSamp.MaxAnisotropy = 1;
    linearSamp.ComparisonFunc = kD3D12ComparisonNever;
    linearSamp.MaxLOD = 1.0f;
    pfnCreateSampler(d3d12Device, &linearSamp, { sampCpuStart.ptr });

    MinimalD3D12SamplerDesc pointSamp = linearSamp;
    pointSamp.Filter = kD3D12FilterMinMagMipPoint;
    pfnCreateSampler(d3d12Device, &pointSamp, { sampCpuStart.ptr + sampInc });

    m_dx12Resources.samplerGpuHandle = sampGpuStart.ptr;
    m_dx12ResourcesCreated.store(true);
    CBR_LOG_INFO("DX12 device-local render targets & ping-pong descriptor heaps created (%ux%u).", dims.fullWidth, dims.fullHeight);
    return true;
#else
    (void)d3d12Device;
    return false;
#endif
}

void RenderTargetManager::DestroyDX12Resources() {
#if defined(_WIN32)
    ReleaseD3D12Com(m_dx12Resources.quarterColor.resource);
    ReleaseD3D12Com(m_dx12Resources.quarterDepth.resource);
    ReleaseD3D12Com(m_dx12Resources.velocity.resource);
    ReleaseD3D12Com(m_dx12Resources.historyColor[0].resource);
    ReleaseD3D12Com(m_dx12Resources.historyColor[1].resource);
    ReleaseD3D12Com(m_dx12Resources.historyDepth[0].resource);
    ReleaseD3D12Com(m_dx12Resources.historyDepth[1].resource);
    ReleaseD3D12Com(m_dx12Resources.outputColor.resource);
    ReleaseD3D12Com(m_dx12Resources.outputDepth.resource);
    ReleaseD3D12Com(m_dx12Resources.srvUavHeap);
    ReleaseD3D12Com(m_dx12Resources.samplerHeap);
#endif
    m_dx12Resources = {};
    m_dx12ResourcesCreated.store(false);
    m_dx12Device = nullptr;
}

void RenderTargetManager::SetDX12ResourceState(void* d3d12Resource, uint32_t newState) {
    if (!d3d12Resource) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    DX12TextureResource* const targets[] = {
        &m_dx12Resources.quarterColor,
        &m_dx12Resources.quarterDepth,
        &m_dx12Resources.velocity,
        &m_dx12Resources.historyColor[0],
        &m_dx12Resources.historyColor[1],
        &m_dx12Resources.historyDepth[0],
        &m_dx12Resources.historyDepth[1],
        &m_dx12Resources.outputColor,
        &m_dx12Resources.outputDepth
    };
    for (DX12TextureResource* tex : targets) {
        if (tex->resource == d3d12Resource) {
            tex->currentState = newState;
        }
    }
}

uint32_t RenderTargetManager::GetDX12ResourceState(void* d3d12Resource) const {
    if (!d3d12Resource) return 0;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const DX12TextureResource* const targets[] = {
        &m_dx12Resources.quarterColor,
        &m_dx12Resources.quarterDepth,
        &m_dx12Resources.velocity,
        &m_dx12Resources.historyColor[0],
        &m_dx12Resources.historyColor[1],
        &m_dx12Resources.historyDepth[0],
        &m_dx12Resources.historyDepth[1],
        &m_dx12Resources.outputColor,
        &m_dx12Resources.outputDepth
    };
    for (const DX12TextureResource* tex : targets) {
        if (tex->resource == d3d12Resource) {
            return tex->currentState;
        }
    }
    return 0;
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

bool RenderTargetManager::IsVulkanDepthFormat(int32_t format) {
    // VK_FORMAT_D16_UNORM (124) .. VK_FORMAT_D32_SFLOAT_S8_UINT (130)
    return format >= 124 && format <= 130;
}

bool RenderTargetManager::IsDepthCopyFormatCompatible(uint32_t srcFormat, uint32_t dstFormat) {
    if (srcFormat == 0u || dstFormat == 0u) return false;
    if (srcFormat == dstFormat) return true;
    const bool srcIsDepth = IsVulkanDepthFormat(static_cast<int32_t>(srcFormat));
    const bool dstIsDepth = IsVulkanDepthFormat(static_cast<int32_t>(dstFormat));
    // Direct vkCmdCopyImage cannot cross depth/color aspect classes (e.g. D24/D32 -> R32_SFLOAT).
    if (srcIsDepth != dstIsDepth) {
        return false;
    }
    if (srcIsDepth && dstIsDepth) {
        // Allow size-compatible 32-bit depth formats (D32_SFLOAT = 126 and D32_SFLOAT_S8_UINT = 130)
        auto is32BitDepth = [](uint32_t f) {
            return f == 126u /* D32_SFLOAT */ || f == 130u /* D32_SFLOAT_S8_UINT */;
        };
        return is32BitDepth(srcFormat) && is32BitDepth(dstFormat);
    }
    // Allow 32-bit color <-> R32_SFLOAT (100) only when neither is a depth/stencil format.
    auto is32BitSingleComponentColor = [](uint32_t f) {
        return f == 98u /* R32_UINT */ || f == 99u /* R32_SINT */ || f == 100u /* R32_SFLOAT */;
    };
    return is32BitSingleComponentColor(srcFormat) && is32BitSingleComponentColor(dstFormat);
}

bool RenderTargetManager::IsColorCopyFormatCompatible(uint32_t srcFormat, uint32_t dstFormat) {
    if (srcFormat == 0u || dstFormat == 0u) return false;
    if (IsVulkanDepthFormat(static_cast<int32_t>(srcFormat)) ||
        IsVulkanDepthFormat(static_cast<int32_t>(dstFormat))) {
        return false;
    }
    if (srcFormat == dstFormat) return true;
    // vkCmdCopyImage permits size-compatible color format copies (64-bit class for RGBA16F = 97)
    auto is64BitColor = [](uint32_t f) {
        return (f >= 91u && f <= 97u) /* R16G16B16A16_* */ ||
               (f >= 101u && f <= 103u) /* R32G32_* */;
    };
    return is64BitColor(srcFormat) && is64BitColor(dstFormat);
}

bool RenderTargetManager::CanCopyToQuarterColor(const VulkanImageMetadata& srcMeta) const {
    if (!m_initialized.load()) return false;
    const TargetDimensions dims = GetDimensions();
    if (dims.quarterWidth == 0u || dims.quarterHeight == 0u) return false;
    // Unknown dimensions (pre-hook image) cannot satisfy copy bounds safely when known to be 0.
    if (srcMeta.width > 0u && srcMeta.height > 0u) {
        if (srcMeta.width < dims.quarterWidth || srcMeta.height < dims.quarterHeight) {
            return false;
        }
    }
    // vkCmdCopyImage requires matching sample count (quarterColor is 2x MSAA when allocated).
    if (srcMeta.samples != 0u && srcMeta.samples != dims.msaaSamples) {
        return false;
    }
    // Verify 64bpp texel compatibility with kVkFormatR16G16B16A16Sfloat when format is known (Item C-01).
    if (srcMeta.format != 0u && !IsColorCopyFormatCompatible(srcMeta.format, kVkFormatR16G16B16A16Sfloat)) {
        return false;
    }
    return true;
}

bool RenderTargetManager::CanCopyToQuarterDepth(const VulkanImageMetadata& srcMeta) const {
    if (!m_initialized.load()) return false;
    const TargetDimensions dims = GetDimensions();
    if (dims.quarterWidth == 0u || dims.quarterHeight == 0u) return false;
    if (srcMeta.width > 0u && srcMeta.height > 0u) {
        if (srcMeta.width < dims.quarterWidth || srcMeta.height < dims.quarterHeight) {
            return false;
        }
    }
    if (srcMeta.samples != 0u && srcMeta.samples != dims.msaaSamples) {
        return false;
    }
    if (srcMeta.format != 0u && !IsDepthCopyFormatCompatible(srcMeta.format, kVkFormatD32Sfloat)) {
        return false;
    }
    return true;
}

bool RenderTargetManager::CanCopyToVelocity(const VulkanImageMetadata& srcMeta) const {
    if (!m_initialized.load()) return false;
    const TargetDimensions dims = GetDimensions();
    if (dims.quarterWidth == 0u || dims.quarterHeight == 0u) return false;
    if (srcMeta.width > 0u && srcMeta.height > 0u) {
        if (srcMeta.width < dims.quarterWidth || srcMeta.height < dims.quarterHeight) {
            return false;
        }
    }
    // m_vkResources.velocity is allocated at 1x sample count.
    if (srcMeta.samples != 0u && srcMeta.samples != kVkSampleCount1Bit) {
        return false;
    }
    return true;
}

bool RenderTargetManager::ShouldBlitReconstructionToSceneColor(const void* gameColorImage,
                                                               bool isSwapchainBackbuffer,
                                                               uint32_t passWidth,
                                                               uint32_t passHeight,
                                                               const VulkanImageMetadata& dstMeta) const {
    if (!m_initialized.load() || !gameColorImage || isSwapchainBackbuffer) {
        return false;
    }
    // VUID-vkCmdBlitImage-dstImage-00233: dstImage must have VK_SAMPLE_COUNT_1_BIT and non-depth format (Item C-04)
    if (dstMeta.samples > kVkSampleCount1Bit) {
        return false;
    }
    if (dstMeta.format != 0u && IsVulkanDepthFormat(static_cast<int32_t>(dstMeta.format))) {
        return false;
    }
    const TargetDimensions dims = GetDimensions();
    if (dstMeta.width > 0u && dstMeta.height > 0u) {
        if (dstMeta.width < dims.fullWidth || dstMeta.height < dims.fullHeight) {
            return false;
        }
    }
    return (passWidth >= dims.fullWidth) && (passHeight >= dims.fullHeight);
}

} // namespace cbr
