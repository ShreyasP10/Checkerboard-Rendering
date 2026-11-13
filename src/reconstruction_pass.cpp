#include "cbr/reconstruction_pass.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>
#include "cbr/config.h"
#include "cbr/depth_convention.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(__has_include) && __has_include(<vulkan/vulkan.h>)
#define CBR_HAS_VULKAN_HEADER 1
#include <vulkan/vulkan.h>
#endif
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace {

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

inline std::filesystem::path GetCurrentModuleDirectory() {
#if defined(_WIN32)
    HMODULE hSelf = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetVulkanLoaderModule),
            &hSelf) && hSelf) {
        char pathBuf[MAX_PATH]{};
        const DWORD len = GetModuleFileNameA(hSelf, pathBuf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            return std::filesystem::path(pathBuf).parent_path();
        }
    }
    return {};
#else
    char exeBuf[4096]{};
    const ssize_t len = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (len > 0) {
        exeBuf[len] = '\0';
        return std::filesystem::path(exeBuf).parent_path();
    }
    return {};
#endif
}

constexpr uint32_t kVkStructureTypeMemoryAllocateInfo = 5;
constexpr uint32_t kVkStructureTypeBufferCreateInfo = 12;
constexpr uint32_t kVkStructureTypeShaderModuleCreateInfo = 16;
constexpr uint32_t kVkStructureTypePipelineShaderStageCreateInfo = 18;
constexpr uint32_t kVkStructureTypeComputePipelineCreateInfo = 29;
constexpr uint32_t kVkStructureTypePipelineLayoutCreateInfo = 30;
constexpr uint32_t kVkStructureTypeDescriptorSetLayoutCreateInfo = 32;
constexpr uint32_t kVkStructureTypeDescriptorPoolCreateInfo = 33;
constexpr uint32_t kVkStructureTypeDescriptorSetAllocateInfo = 34;
constexpr uint32_t kVkStructureTypeWriteDescriptorSet = 35;
constexpr uint32_t kVkStructureTypeMemoryBarrier = 46;
constexpr uint32_t kVkStructureTypeImageMemoryBarrier = 45;

constexpr uint32_t kVkImageLayoutGeneral = 1;
constexpr uint32_t kVkImageLayoutShaderReadOnlyOptimal = 5;
constexpr uint32_t kVkImageLayoutTransferSrcOptimal = 6;
constexpr uint32_t kVkImageLayoutTransferDstOptimal = 7;
constexpr uint32_t kVkImageLayoutPresentSrcKHR = 1000001002;
constexpr uint32_t kVkImageLayoutColorAttachmentOptimal = 2;
constexpr uint32_t kVkImageLayoutDepthStencilAttachmentOptimal = 3;
constexpr uint32_t kVkPipelineStageColorAttachmentOutputBit = 0x00000400;
constexpr uint32_t kVkAccessColorAttachmentWriteBit = 0x00000100;
constexpr uint32_t kVkAccessColorAttachmentReadBit = 0x00000080;
constexpr uint32_t kVkFilterLinear = 1;
constexpr uint32_t kVkImageAspectColorBit = 0x00000001;

constexpr uint32_t kVkPipelineStageTopOfPipeBit = 0x00000001;
constexpr uint32_t kVkPipelineStageDepthStencilTestsBits = 0x00000300; // EARLY | LATE_FRAGMENT_TESTS
constexpr uint32_t kVkPipelineStageComputeShaderBit = 0x00000800;
constexpr uint32_t kVkPipelineStageTransferBit = 0x00001000;
constexpr uint32_t kVkPipelineStageBottomOfPipeBit = 0x00002000;
constexpr uint32_t kVkPipelineStageAllCommandsBit = 0x00010000;
constexpr uint32_t kVkAccessShaderWriteBit = 0x00000040;
constexpr uint32_t kVkAccessShaderReadBit = 0x00000020;
constexpr uint32_t kVkAccessTransferReadBit = 0x00000800;
constexpr uint32_t kVkAccessTransferWriteBit = 0x00001000;
constexpr uint32_t kVkAccessDepthStencilReadWriteBits = 0x00000600;
constexpr uint32_t kVkAccessMemoryReadBit = 0x00008000;

void StageAccessForLayout(uint32_t layout, uint32_t aspectMask, uint32_t& outStage, uint32_t& outAccess) {
    switch (layout) {
        case kVkImageLayoutGeneral:
            outStage = kVkPipelineStageComputeShaderBit;
            outAccess = kVkAccessShaderWriteBit | kVkAccessShaderReadBit;
            break;
        case kVkImageLayoutShaderReadOnlyOptimal:
            outStage = kVkPipelineStageComputeShaderBit;
            outAccess = kVkAccessShaderReadBit;
            break;
        case kVkImageLayoutTransferSrcOptimal:
            outStage = kVkPipelineStageTransferBit;
            outAccess = kVkAccessTransferReadBit;
            break;
        case kVkImageLayoutTransferDstOptimal:
            outStage = kVkPipelineStageTransferBit;
            outAccess = kVkAccessTransferWriteBit;
            break;
        case kVkImageLayoutColorAttachmentOptimal:
            outStage = kVkPipelineStageColorAttachmentOutputBit;
            outAccess = kVkAccessColorAttachmentWriteBit | kVkAccessColorAttachmentReadBit;
            break;
        case kVkImageLayoutDepthStencilAttachmentOptimal:
            outStage = kVkPipelineStageDepthStencilTestsBits;
            outAccess = kVkAccessDepthStencilReadWriteBits;
            break;
        case kVkImageLayoutPresentSrcKHR:
            outStage = kVkPipelineStageBottomOfPipeBit;
            outAccess = 0;
            break;
        default: // VK_IMAGE_LAYOUT_UNDEFINED (0) or unknown external state
            outStage = (layout == 0u) ? kVkPipelineStageTopOfPipeBit
                                      : ((aspectMask & 2u) ? kVkPipelineStageDepthStencilTestsBits : kVkPipelineStageAllCommandsBit);
            outAccess = 0;
            break;
    }
}

constexpr uint32_t kVkShaderStageComputeBit = 0x00000020;
constexpr uint32_t kVkPipelineBindPointCompute = 1;

constexpr uint32_t kVkDescriptorTypeCombinedImageSampler = 1;
constexpr uint32_t kVkDescriptorTypeStorageImage = 3;
constexpr uint32_t kVkDescriptorTypeUniformBuffer = 6;
constexpr uint32_t kVkBufferUsageUniformBufferBit = 0x00000010;
constexpr uint32_t kVkMemoryPropertyHostVisibleCoherentBits = 0x00000006;

struct MinimalVkShaderModuleCreateInfo {
    uint32_t        sType;
    const void*     pNext;
    uint32_t        flags;
    size_t          codeSize;
    const uint32_t* pCode;
};

struct MinimalVkPushConstantRange {
    uint32_t stageFlags;
    uint32_t offset;
    uint32_t size;
};

struct MinimalVkDescriptorSetLayoutBinding {
    uint32_t    binding;
    uint32_t    descriptorType;
    uint32_t    descriptorCount;
    uint32_t    stageFlags;
    const void* pImmutableSamplers;
};

struct MinimalVkDescriptorSetLayoutCreateInfo {
    uint32_t                                   sType;
    const void*                                pNext;
    uint32_t                                   flags;
    uint32_t                                   bindingCount;
    const MinimalVkDescriptorSetLayoutBinding* pBindings;
};

struct MinimalVkDescriptorPoolSize {
    uint32_t type;
    uint32_t descriptorCount;
};

struct MinimalVkDescriptorPoolCreateInfo {
    uint32_t                           sType;
    const void*                        pNext;
    uint32_t                           flags;
    uint32_t                           maxSets;
    uint32_t                           poolSizeCount;
    const MinimalVkDescriptorPoolSize* pPoolSizes;
};

struct MinimalVkDescriptorSetAllocateInfo {
    uint32_t    sType;
    const void* pNext;
    void*       descriptorPool;
    uint32_t    descriptorSetCount;
    const void* pSetLayouts;
};

struct MinimalVkMemoryBarrier {
    uint32_t    sType;
    const void* pNext;
    uint32_t    srcAccessMask;
    uint32_t    dstAccessMask;
};

struct MinimalVkDescriptorImageInfo {
    void*    sampler;
    void*    imageView;
    uint32_t imageLayout;
};

struct MinimalVkWriteDescriptorSet {
    uint32_t                            sType;
    const void*                         pNext;
    void*                               dstSet;
    uint32_t                            dstBinding;
    uint32_t                            dstArrayElement;
    uint32_t                            descriptorCount;
    uint32_t                            descriptorType;
    const MinimalVkDescriptorImageInfo* pImageInfo;
    const void*                         pBufferInfo;
    const void*                         pTexelBufferView;
};

struct MinimalVkImageSubresourceRange {
    uint32_t aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
};

struct MinimalVkImageMemoryBarrier {
    uint32_t                       sType;
    const void*                    pNext;
    uint32_t                       srcAccessMask;
    uint32_t                       dstAccessMask;
    uint32_t                       oldLayout;
    uint32_t                       newLayout;
    uint32_t                       srcQueueFamilyIndex;
    uint32_t                       dstQueueFamilyIndex;
    void*                          image;
    MinimalVkImageSubresourceRange subresourceRange;
};

struct MinimalVkPipelineLayoutCreateInfo {
    uint32_t                          sType;
    const void*                       pNext;
    uint32_t                          flags;
    uint32_t                          setLayoutCount;
    const void*                       pSetLayouts;
    uint32_t                          pushConstantRangeCount;
    const MinimalVkPushConstantRange* pPushConstantRanges;
};

struct MinimalVkPipelineShaderStageCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    stage;
    void*       module;
    const char* pName;
    const void* pSpecializationInfo;
};

struct MinimalVkComputePipelineCreateInfo {
    uint32_t                               sType;
    const void*                            pNext;
    uint32_t                               flags;
    MinimalVkPipelineShaderStageCreateInfo stage;
    void*                                  layout;
    void*                                  basePipelineHandle;
    int32_t                                basePipelineIndex;
};

typedef void* (*PFN_vkGetDeviceProcAddr)(void* device, const char* pName);
typedef int   (*PFN_vkCreateShaderModule)(void* device, const MinimalVkShaderModuleCreateInfo* pCreateInfo, const void* pAllocator, void** pShaderModule);
typedef void  (*PFN_vkDestroyShaderModule)(void* device, void* shaderModule, const void* pAllocator);
typedef int   (*PFN_vkCreateDescriptorSetLayout)(void* device, const MinimalVkDescriptorSetLayoutCreateInfo* pCreateInfo, const void* pAllocator, void** pSetLayout);
typedef void  (*PFN_vkDestroyDescriptorSetLayout)(void* device, void* setLayout, const void* pAllocator);
typedef int   (*PFN_vkCreateDescriptorPool)(void* device, const MinimalVkDescriptorPoolCreateInfo* pCreateInfo, const void* pAllocator, void** pDescriptorPool);
typedef void  (*PFN_vkDestroyDescriptorPool)(void* device, void* descriptorPool, const void* pAllocator);
typedef int   (*PFN_vkAllocateDescriptorSets)(void* device, const MinimalVkDescriptorSetAllocateInfo* pAllocateInfo, void** pDescriptorSets);
typedef void  (*PFN_vkUpdateDescriptorSets)(void* device, uint32_t descriptorWriteCount, const MinimalVkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const void* pDescriptorCopies);
typedef int   (*PFN_vkCreatePipelineLayout)(void* device, const MinimalVkPipelineLayoutCreateInfo* pCreateInfo, const void* pAllocator, void** pPipelineLayout);
typedef void  (*PFN_vkDestroyPipelineLayout)(void* device, void* pipelineLayout, const void* pAllocator);
typedef int   (*PFN_vkCreateComputePipelines)(void* device, void* pipelineCache, uint32_t createInfoCount, const MinimalVkComputePipelineCreateInfo* pCreateInfos, const void* pAllocator, void** pPipelines);
typedef void  (*PFN_vkDestroyPipeline)(void* device, void* pipeline, const void* pAllocator);
typedef void  (*PFN_vkCmdBindPipeline)(void* commandBuffer, uint32_t pipelineBindPoint, void* pipeline);
typedef void  (*PFN_vkCmdBindDescriptorSets)(void* commandBuffer, uint32_t pipelineBindPoint, void* layout, uint32_t firstSet, uint32_t descriptorSetCount, const void** pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
typedef void  (*PFN_vkCmdPushConstants)(void* commandBuffer, void* layout, uint32_t stageFlags, uint32_t offset, uint32_t size, const void* pValues);
typedef void  (*PFN_vkCmdDispatch)(void* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
typedef void  (*PFN_vkCmdPipelineBarrier)(void* commandBuffer, uint32_t srcStageMask, uint32_t dstStageMask, uint32_t dependencyFlags, uint32_t memoryBarrierCount, const MinimalVkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void* pImageMemoryBarriers);

struct MinimalVkImageSubresourceLayers {
    uint32_t aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
};

struct MinimalVkOffset3D {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct MinimalVkImageBlit {
    MinimalVkImageSubresourceLayers srcSubresource;
    MinimalVkOffset3D               srcOffsets[2];
    MinimalVkImageSubresourceLayers dstSubresource;
    MinimalVkOffset3D               dstOffsets[2];
};

typedef void (*PFN_vkCmdBlitImage)(void* commandBuffer,
                                   void* srcImage, uint32_t srcImageLayout,
                                   void* dstImage, uint32_t dstImageLayout,
                                   uint32_t regionCount, const MinimalVkImageBlit* pRegions,
                                   uint32_t filter);

struct MinimalVkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
};

struct MinimalVkImageCopy {
    MinimalVkImageSubresourceLayers srcSubresource;
    MinimalVkOffset3D               srcOffset;
    MinimalVkImageSubresourceLayers dstSubresource;
    MinimalVkOffset3D               dstOffset;
    MinimalVkExtent3D               extent;
};

typedef void (*PFN_vkCmdCopyImage)(void* commandBuffer,
                                   void* srcImage, uint32_t srcImageLayout,
                                   void* dstImage, uint32_t dstImageLayout,
                                   uint32_t regionCount, const MinimalVkImageCopy* pRegions);

union MinimalVkClearColorValue {
    float    float32[4];
    int32_t  int32[4];
    uint32_t uint32[4];
};

// Minimal Vulkan QueryPool types for GPU reconstruction timing (Item M-05)
constexpr uint32_t kVkStructureTypeQueryPoolCreateInfo = 11;
constexpr uint32_t kVkQueryTypeTimestamp = 2;
constexpr uint32_t kVkQueryResult64Bit = 0x00000001;

struct MinimalVkQueryPoolCreateInfo {
    uint32_t    sType;
    const void* pNext;
    uint32_t    flags;
    uint32_t    queryType;
    uint32_t    queryCount;
    uint32_t    pipelineStatistics;
};

typedef int  (*PFN_vkCreateQueryPool)(void* device, const MinimalVkQueryPoolCreateInfo* pCreateInfo, const void* pAllocator, void** pQueryPool);
typedef void (*PFN_vkDestroyQueryPool)(void* device, void* queryPool, const void* pAllocator);
typedef int  (*PFN_vkGetQueryPoolResults)(void* device, void* queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, uint64_t stride, uint32_t flags);
typedef void (*PFN_vkCmdResetQueryPool)(void* commandBuffer, void* queryPool, uint32_t firstQuery, uint32_t queryCount);
typedef void (*PFN_vkCmdWriteTimestamp)(void* commandBuffer, uint32_t pipelineStage, void* queryPool, uint32_t query);
typedef void (*PFN_vkGetPhysicalDeviceProperties)(void* physicalDevice, void* pProperties);

struct MinimalVkDescriptorBufferInfo {
    void*    buffer;
    uint64_t offset;
    uint64_t range;
};
static_assert(sizeof(MinimalVkDescriptorBufferInfo) == 24, "VkDescriptorBufferInfo size mismatch");

struct MinimalVkBufferCreateInfo {
    uint32_t        sType;
    const void*     pNext;
    uint32_t        flags;
    uint64_t        size;
    uint32_t        usage;
    uint32_t        sharingMode;
    uint32_t        queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
};
static_assert(sizeof(MinimalVkBufferCreateInfo) == 56, "VkBufferCreateInfo size mismatch");

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

struct MinimalVkPhysMemoryType {
    uint32_t propertyFlags;
    uint32_t heapIndex;
};

struct MinimalVkPhysMemoryHeap {
    uint64_t size;
    uint32_t flags;
};

struct MinimalVkPhysMemoryProps {
    uint32_t                memoryTypeCount;
    MinimalVkPhysMemoryType memoryTypes[32];
    uint32_t                memoryHeapCount;
    MinimalVkPhysMemoryHeap memoryHeaps[16];
};

typedef int   (*PFN_vkCreateBuffer)(void* device, const MinimalVkBufferCreateInfo* pCreateInfo, const void* pAllocator, void** pBuffer);
typedef void  (*PFN_vkDestroyBuffer)(void* device, void* buffer, const void* pAllocator);
typedef void  (*PFN_vkGetBufferMemoryRequirements)(void* device, void* buffer, MinimalVkMemoryRequirements* pMemoryRequirements);
typedef int   (*PFN_vkAllocateMemory)(void* device, const MinimalVkMemoryAllocateInfo* pAllocateInfo, const void* pAllocator, void** pMemory);
typedef void  (*PFN_vkFreeMemory)(void* device, void* memory, const void* pAllocator);
typedef int   (*PFN_vkBindBufferMemory)(void* device, void* buffer, void* memory, uint64_t memoryOffset);
typedef int   (*PFN_vkMapMemory)(void* device, void* memory, uint64_t offset, uint64_t size, uint32_t flags, void** ppData);
typedef void  (*PFN_vkUnmapMemory)(void* device, void* memory);
typedef void  (*PFN_vkGetPhysicalDeviceMemoryProperties)(void* physicalDevice, MinimalVkPhysMemoryProps* pMemoryProperties);

#if defined(_WIN32)
// Minimal DirectX 12 Root Signature, PSO, and Barrier ABI definitions (Item H-02)
constexpr uint32_t kD3D12RootParamTypeDescriptorTable = 0;
constexpr uint32_t kD3D12RootParamType32BitConstants  = 1;
constexpr uint32_t kD3D12RootParamTypeCBV             = 2;
constexpr uint32_t kD3D12DescriptorRangeSRV = 0;
constexpr uint32_t kD3D12DescriptorRangeUAV = 1;
constexpr uint32_t kD3D12DescriptorRangeSampler = 3;
constexpr uint32_t kD3D12ShaderVisibilityAll = 0;
constexpr uint32_t kD3D12RootSignatureVersion1 = 1;
constexpr uint32_t kD3D12HeapTypeUpload = 2;
constexpr uint32_t kD3D12ResourceDimensionBuffer = 1;
constexpr uint32_t kD3D12TextureLayoutRowMajor = 1;
constexpr uint32_t kD3D12ResourceStateGenericRead = 0xAC3;
constexpr uint32_t kD3D12ResourceStateNonPixelSRV = 0x40;
constexpr uint32_t kD3D12ResourceStateUAV = 0x8;
constexpr uint32_t kD3D12ResourceBarrierAllSubresources = 0xFFFFFFFFu;

struct MinimalD3D12HeapProperties {
    uint32_t Type;
    uint32_t CPUPageProperty;
    uint32_t MemoryPoolPreference;
    uint32_t CreationNodeMask;
    uint32_t VisibleNodeMask;
};

struct MinimalD3D12BufferDesc {
    uint32_t Dimension;
    uint32_t _pad0{ 0 };
    uint64_t Alignment;
    uint64_t Width;
    uint32_t Height;
    uint16_t DepthOrArraySize;
    uint16_t MipLevels;
    uint32_t Format;
    uint32_t SampleCount;
    uint32_t SampleQuality;
    uint32_t Layout;
    uint32_t Flags;
};
static_assert(sizeof(MinimalD3D12BufferDesc) == 56, "D3D12_RESOURCE_DESC buffer size mismatch");

struct MinimalD3D12RootDescriptor {
    uint32_t ShaderRegister;
    uint32_t RegisterSpace;
};

struct MinimalD3D12DescriptorRange {
    uint32_t RangeType;
    uint32_t NumDescriptors;
    uint32_t BaseShaderRegister;
    uint32_t RegisterSpace;
    uint32_t OffsetInDescriptorsFromTableStart;
};
static_assert(sizeof(MinimalD3D12DescriptorRange) == 20, "D3D12_DESCRIPTOR_RANGE size mismatch");

struct MinimalD3D12RootDescriptorTable {
    uint32_t                          NumDescriptorRanges;
    const MinimalD3D12DescriptorRange* pDescriptorRanges;
};

struct MinimalD3D12RootConstants {
    uint32_t ShaderRegister;
    uint32_t RegisterSpace;
    uint32_t Num32BitValues;
};

struct MinimalD3D12RootParameter {
    uint32_t ParameterType;
    union {
        MinimalD3D12RootDescriptorTable DescriptorTable;
        MinimalD3D12RootConstants       Constants;
        MinimalD3D12RootDescriptor      Descriptor;
    };
    uint32_t ShaderVisibility;
};
static_assert(sizeof(MinimalD3D12RootParameter) == 32, "D3D12_ROOT_PARAMETER size mismatch");

struct MinimalD3D12RootSignatureDesc {
    uint32_t                        NumParameters;
    const MinimalD3D12RootParameter* pParameters;
    uint32_t                        NumStaticSamplers;
    const void*                     pStaticSamplers;
    uint32_t                        Flags;
};

struct MinimalD3D12ShaderBytecode {
    const void* pShaderBytecode;
    size_t      BytecodeLength;
};

struct MinimalD3D12CachedPipelineState {
    const void* pCachedBlob;
    size_t      CachedBlobSizeInBytes;
};

struct MinimalD3D12ComputePipelineStateDesc {
    void*                          pRootSignature;
    MinimalD3D12ShaderBytecode     CS;
    uint32_t                       NodeMask;
    MinimalD3D12CachedPipelineState CachedPSO;
    uint32_t                       Flags;
};
static_assert(sizeof(MinimalD3D12ComputePipelineStateDesc) == 56, "D3D12_COMPUTE_PIPELINE_STATE_DESC size mismatch");

struct MinimalD3D12ResourceTransitionBarrier {
    void*    pResource;
    uint32_t Subresource;
    uint32_t StateBefore;
    uint32_t StateAfter;
};

struct MinimalD3D12ResourceBarrier {
    uint32_t Type;
    uint32_t Flags;
    MinimalD3D12ResourceTransitionBarrier Transition;
};
static_assert(sizeof(MinimalD3D12ResourceBarrier) == 32, "D3D12_RESOURCE_BARRIER size mismatch");

inline void* GetBlobPtr(void* blob) {
    if (!blob) return nullptr;
    typedef void* (__stdcall *PFN_GetPtr)(void*);
    void** vmt = *reinterpret_cast<void***>(blob);
    return (vmt && vmt[3]) ? reinterpret_cast<PFN_GetPtr>(vmt[3])(blob) : nullptr;
}

inline size_t GetBlobSize(void* blob) {
    if (!blob) return 0;
    typedef size_t (__stdcall *PFN_GetSize)(void*);
    void** vmt = *reinterpret_cast<void***>(blob);
    return (vmt && vmt[4]) ? reinterpret_cast<PFN_GetSize>(vmt[4])(blob) : 0;
}

inline void ReleaseBlobOrCom(void*& obj) {
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

ReconstructionPass& ReconstructionPass::Get() {
    static ReconstructionPass instance;
    return instance;
}

std::vector<uint32_t> ReconstructionPass::LoadSpvFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    const auto fileSize = file.tellg();
    if (fileSize <= 0 || (fileSize % static_cast<std::streamoff>(sizeof(uint32_t))) != 0) {
        return {};
    }
    const size_t wordCount = static_cast<size_t>(fileSize) / sizeof(uint32_t);
    std::vector<uint32_t> buffer(wordCount);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
    if (buffer.empty() || buffer[0] != 0x07230203) {
        CBR_LOG_ERROR("Invalid SPIR-V magic header (0x%08X) in %s", buffer.empty() ? 0 : buffer[0], path.string().c_str());
        return {};
    }
    return buffer;
}

bool ReconstructionPass::InvertMatrix4x4(const float* m, float* outInv) {
    if (!m || !outInv) return false;
    double inv[16]{};
    const double m0 = m[0], m1 = m[1], m2 = m[2], m3 = m[3];
    const double m4 = m[4], m5 = m[5], m6 = m[6], m7 = m[7];
    const double m8 = m[8], m9 = m[9], m10 = m[10], m11 = m[11];
    const double m12 = m[12], m13 = m[13], m14 = m[14], m15 = m[15];

    inv[0] = m5 * m10 * m15 - m5 * m11 * m14 - m9 * m6 * m15 + m9 * m7 * m14 + m13 * m6 * m11 - m13 * m7 * m10;
    inv[4] = -m4 * m10 * m15 + m4 * m11 * m14 + m8 * m6 * m15 - m8 * m7 * m14 - m12 * m6 * m11 + m12 * m7 * m10;
    inv[8] = m4 * m9 * m15 - m4 * m11 * m13 - m8 * m5 * m15 + m8 * m7 * m13 + m12 * m5 * m11 - m12 * m7 * m9;
    inv[12] = -m4 * m9 * m14 + m4 * m10 * m13 + m8 * m5 * m14 - m8 * m6 * m13 - m12 * m5 * m10 + m12 * m6 * m9;

    inv[1] = -m1 * m10 * m15 + m1 * m11 * m14 + m9 * m2 * m15 - m9 * m3 * m14 - m13 * m2 * m11 + m13 * m3 * m10;
    inv[5] = m0 * m10 * m15 - m0 * m11 * m14 - m8 * m2 * m15 + m8 * m3 * m14 + m12 * m2 * m11 - m12 * m3 * m10;
    inv[9] = -m0 * m9 * m15 + m0 * m11 * m13 + m8 * m1 * m15 - m8 * m3 * m13 - m12 * m1 * m11 + m12 * m3 * m9;
    inv[13] = m0 * m9 * m14 - m0 * m10 * m13 - m8 * m1 * m14 + m8 * m2 * m13 + m12 * m1 * m10 - m12 * m2 * m9;

    inv[2] = m1 * m6 * m15 - m1 * m7 * m14 - m5 * m2 * m15 + m5 * m3 * m14 + m13 * m2 * m7 - m13 * m3 * m6;
    inv[6] = -m0 * m6 * m15 + m0 * m7 * m14 + m4 * m2 * m15 - m4 * m3 * m14 - m12 * m2 * m7 + m12 * m3 * m6;
    inv[10] = m0 * m5 * m15 - m0 * m7 * m13 - m4 * m1 * m15 + m4 * m3 * m13 + m12 * m1 * m7 - m12 * m3 * m5;
    inv[14] = -m0 * m5 * m14 + m0 * m6 * m13 + m4 * m1 * m14 - m4 * m2 * m13 - m12 * m1 * m6 + m12 * m2 * m5;

    inv[3] = -m1 * m6 * m11 + m1 * m7 * m10 + m5 * m2 * m11 - m5 * m3 * m10 - m9 * m2 * m7 + m9 * m3 * m6;
    inv[7] = m0 * m6 * m11 - m0 * m7 * m10 - m4 * m2 * m11 + m4 * m3 * m10 + m8 * m2 * m7 - m8 * m3 * m6;
    inv[11] = -m0 * m5 * m11 + m0 * m7 * m9 + m4 * m1 * m11 - m4 * m3 * m9 - m8 * m1 * m7 + m8 * m3 * m5;
    inv[15] = m0 * m5 * m10 - m0 * m6 * m9 - m4 * m1 * m10 + m4 * m2 * m9 + m8 * m1 * m6 - m8 * m2 * m5;

    const double det = m0 * inv[0] + m1 * inv[4] + m2 * inv[8] + m3 * inv[12];
    if (!std::isfinite(det) || std::fabs(det) < 1e-18) {
        return false;
    }
    const double invDet = 1.0 / det;
    for (int i = 0; i < 16; ++i) {
        const float val = static_cast<float>(inv[i] * invDet);
        if (!std::isfinite(val)) {
            return false;
        }
        outInv[i] = val;
    }
    return true;
}

bool ReconstructionPass::SetCameraMatrices(const float* currViewProj4x4, const float* prevViewProj4x4) {
    if (!currViewProj4x4) return false;
    float invCurr[16]{};
    if (!InvertMatrix4x4(currViewProj4x4, invCurr)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    if (prevViewProj4x4) {
        std::memcpy(m_prevViewProj, prevViewProj4x4, sizeof(m_prevViewProj));
    } else if (m_hasPrevMatrix) {
        std::memcpy(m_prevViewProj, m_currViewProj, sizeof(m_prevViewProj));
    } else {
        std::memcpy(m_prevViewProj, currViewProj4x4, sizeof(m_prevViewProj));
    }
    std::memcpy(m_currViewProj, currViewProj4x4, sizeof(m_currViewProj));
    std::memcpy(m_currViewProjInv, invCurr, sizeof(m_currViewProjInv));
    m_hasCameraMatrices = true;
    m_hasPrevMatrix = true;
    return true;
}

void ReconstructionPass::ClearCameraMatrices() {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    std::memset(m_currViewProj, 0, sizeof(m_currViewProj));
    std::memset(m_currViewProjInv, 0, sizeof(m_currViewProjInv));
    std::memset(m_prevViewProj, 0, sizeof(m_prevViewProj));
    m_hasCameraMatrices = false;
    m_hasPrevMatrix = false;
}

CBRCameraUniforms ReconstructionPass::BuildCameraUniforms() const {
    CBRCameraUniforms u{};
    const auto cfg = ConfigManager::Get().GetConfig();
    u.sharpeningStrength = std::clamp(cfg.sharpeningStrength, 0.0f, 1.0f);
    u.enableCameraFallback = cfg.enableCameraMotionFallback ? 1u : 0u;
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    if (m_hasCameraMatrices) {
        std::memcpy(u.currViewProjInv, m_currViewProjInv, sizeof(u.currViewProjInv));
        std::memcpy(u.prevViewProj, m_prevViewProj, sizeof(u.prevViewProj));
        u.hasCameraMatrices = 1u;
    } else {
        u.currViewProjInv[0] = u.currViewProjInv[5] = u.currViewProjInv[10] = u.currViewProjInv[15] = 1.0f;
        u.prevViewProj[0] = u.prevViewProj[5] = u.prevViewProj[10] = u.prevViewProj[15] = 1.0f;
        u.hasCameraMatrices = 0u;
    }
    u.reservedPad = 0u;
    return u;
}

bool ReconstructionPass::CreateVulkanPipeline() {
    if (!m_device) {
        return false;
    }

    void* vulkanModule = GetVulkanLoaderModule();
    if (!vulkanModule) {
        CBR_LOG_WARN("CreateVulkanPipeline: Vulkan loader library not loaded.");
        return false;
    }

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanModule, name);
    };

    auto pfnCreateShaderModule = reinterpret_cast<PFN_vkCreateShaderModule>(resolveDeviceFunc("vkCreateShaderModule"));
    auto pfnDestroyShaderModule = reinterpret_cast<PFN_vkDestroyShaderModule>(resolveDeviceFunc("vkDestroyShaderModule"));
    auto pfnCreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(resolveDeviceFunc("vkCreatePipelineLayout"));
    auto pfnCreateComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(resolveDeviceFunc("vkCreateComputePipelines"));

    if (!pfnCreateShaderModule || !pfnDestroyShaderModule || !pfnCreatePipelineLayout || !pfnCreateComputePipelines) {
        CBR_LOG_ERROR("CreateVulkanPipeline: failed to resolve core Vulkan pipeline functions.");
        return false;
    }

    // Candidate search locations for cbr_reconstruct.spv
    std::vector<std::filesystem::path> searchPaths;
    if (!m_shaderDir.empty()) {
        searchPaths.push_back(m_shaderDir / "cbr_reconstruct.spv");
    }
    const auto modDir = GetCurrentModuleDirectory();
    if (!modDir.empty()) {
        searchPaths.push_back(modDir / "shaders" / "cbr_reconstruct.spv");
        searchPaths.push_back(modDir / "cbr_reconstruct.spv");
    }
    searchPaths.push_back("shaders/cbr_reconstruct.spv");
    searchPaths.push_back("shaders/spv/cbr_reconstruct.spv");
    searchPaths.push_back("package/rdr2-cbr/shaders/cbr_reconstruct.spv");

    std::vector<uint32_t> spvBytecode;
    std::filesystem::path resolvedShaderPath;
    for (const auto& path : searchPaths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            spvBytecode = LoadSpvFile(path);
            if (!spvBytecode.empty()) {
                resolvedShaderPath = path;
                break;
            }
        }
    }

    if (spvBytecode.empty()) {
        static std::atomic<bool> s_loggedMissing{ false };
        if (!s_loggedMissing.exchange(true)) {
            CBR_LOG_WARN("SPIR-V shader binary 'cbr_reconstruct.spv' not found in search paths. Pipeline compilation deferred.");
        }
        return true;
    }

    CBR_LOG_INFO("Loading CBR SPIR-V shader from: %s (%zu bytes)", resolvedShaderPath.string().c_str(), spvBytecode.size() * sizeof(uint32_t));

    MinimalVkShaderModuleCreateInfo smInfo{};
    smInfo.sType = kVkStructureTypeShaderModuleCreateInfo;
    smInfo.codeSize = spvBytecode.size() * sizeof(uint32_t);
    smInfo.pCode = spvBytecode.data();

    void* shaderModule = nullptr;
    int res = pfnCreateShaderModule(m_device, &smInfo, nullptr, &shaderModule);
    if (res != 0 || !shaderModule) {
        CBR_LOG_ERROR("vkCreateShaderModule failed with error code: %d", res);
        return false;
    }

    MinimalVkPushConstantRange pcRange{};
    pcRange.stageFlags = kVkShaderStageComputeBit;
    pcRange.offset = 0;
    pcRange.size = sizeof(ReconstructionPushConstants);

    auto pfnCreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(resolveDeviceFunc("vkCreateDescriptorSetLayout"));
    auto pfnDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(resolveDeviceFunc("vkDestroyDescriptorSetLayout"));

    // 8 bindings matching cbr_reconstruct.comp (set = 0):
    // Bindings 0..4: combined image samplers
    // Bindings 5..6: storage images (OutputImage, OutputDepth)
    // Binding  7:    CBRCameraData uniform buffer (matrices & sharpening)
    MinimalVkDescriptorSetLayoutBinding dsBindings[8]{};
    for (uint32_t i = 0; i < 5; ++i) {
        dsBindings[i].binding = i;
        dsBindings[i].descriptorType = kVkDescriptorTypeCombinedImageSampler;
        dsBindings[i].descriptorCount = 1;
        dsBindings[i].stageFlags = kVkShaderStageComputeBit;
    }
    for (uint32_t i = 5; i < 7; ++i) {
        dsBindings[i].binding = i;
        dsBindings[i].descriptorType = kVkDescriptorTypeStorageImage;
        dsBindings[i].descriptorCount = 1;
        dsBindings[i].stageFlags = kVkShaderStageComputeBit;
    }
    dsBindings[7].binding = 7;
    dsBindings[7].descriptorType = kVkDescriptorTypeUniformBuffer;
    dsBindings[7].descriptorCount = 1;
    dsBindings[7].stageFlags = kVkShaderStageComputeBit;

    MinimalVkDescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.sType = kVkStructureTypeDescriptorSetLayoutCreateInfo;
    dsLayoutInfo.bindingCount = 8;
    dsLayoutInfo.pBindings = dsBindings;

    void* descriptorSetLayout = nullptr;
    if (pfnCreateDescriptorSetLayout) {
        res = pfnCreateDescriptorSetLayout(m_device, &dsLayoutInfo, nullptr, &descriptorSetLayout);
        if (res != 0 || !descriptorSetLayout) {
            CBR_LOG_ERROR("vkCreateDescriptorSetLayout failed with error code: %d", res);
            pfnDestroyShaderModule(m_device, shaderModule, nullptr);
            return false;
        }
    }

    MinimalVkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = kVkStructureTypePipelineLayoutCreateInfo;
    layoutInfo.setLayoutCount = descriptorSetLayout ? 1 : 0;
    layoutInfo.pSetLayouts = descriptorSetLayout ? &descriptorSetLayout : nullptr;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    void* pipelineLayout = nullptr;
    res = pfnCreatePipelineLayout(m_device, &layoutInfo, nullptr, &pipelineLayout);
    if (res != 0 || !pipelineLayout) {
        CBR_LOG_ERROR("vkCreatePipelineLayout failed with error code: %d", res);
        if (descriptorSetLayout && pfnDestroyDescriptorSetLayout) {
            pfnDestroyDescriptorSetLayout(m_device, descriptorSetLayout, nullptr);
        }
        pfnDestroyShaderModule(m_device, shaderModule, nullptr);
        return false;
    }

    MinimalVkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = kVkStructureTypeComputePipelineCreateInfo;
    pipelineInfo.stage.sType = kVkStructureTypePipelineShaderStageCreateInfo;
    pipelineInfo.stage.stage = kVkShaderStageComputeBit;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.basePipelineIndex = -1;

    void* pipeline = nullptr;
    res = pfnCreateComputePipelines(m_device, nullptr, 1, &pipelineInfo, nullptr, &pipeline);
    pfnDestroyShaderModule(m_device, shaderModule, nullptr);

    if (res != 0 || !pipeline) {
        CBR_LOG_ERROR("vkCreateComputePipelines failed with error code: %d", res);
        auto pfnDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(resolveDeviceFunc("vkDestroyPipelineLayout"));
        if (pfnDestroyPipelineLayout) pfnDestroyPipelineLayout(m_device, pipelineLayout, nullptr);
        if (descriptorSetLayout && pfnDestroyDescriptorSetLayout) {
            pfnDestroyDescriptorSetLayout(m_device, descriptorSetLayout, nullptr);
        }
        return false;
    }

    m_pipeline = pipeline;
    m_pipelineLayout = pipelineLayout;
    m_descriptorSetLayout = descriptorSetLayout;

    // Create descriptor pool and allocate descriptor set for bindings 0..6
    auto pfnCreateDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(resolveDeviceFunc("vkCreateDescriptorPool"));
    auto pfnAllocateDescriptorSets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(resolveDeviceFunc("vkAllocateDescriptorSets"));
    void* descriptorPool = nullptr;
    void* descriptorSets[2] = { nullptr, nullptr };

    if (descriptorSetLayout && pfnCreateDescriptorPool && pfnAllocateDescriptorSets) {
        MinimalVkDescriptorPoolSize poolSizes[3]{};
        poolSizes[0].type = kVkDescriptorTypeCombinedImageSampler;
        poolSizes[0].descriptorCount = 10; // 5 samplers * 2 sets
        poolSizes[1].type = kVkDescriptorTypeStorageImage;
        poolSizes[1].descriptorCount = 4;  // 2 storage images * 2 sets
        poolSizes[2].type = kVkDescriptorTypeUniformBuffer;
        poolSizes[2].descriptorCount = 2;  // 1 camera UBO * 2 sets

        MinimalVkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = kVkStructureTypeDescriptorPoolCreateInfo;
        poolInfo.maxSets = 2; // Separate descriptor sets for ping-pong history buffers
        poolInfo.poolSizeCount = 3;
        poolInfo.pPoolSizes = poolSizes;

        res = pfnCreateDescriptorPool(m_device, &poolInfo, nullptr, &descriptorPool);
        if (res == 0 && descriptorPool) {
            void* layouts[2] = { descriptorSetLayout, descriptorSetLayout };
            MinimalVkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = kVkStructureTypeDescriptorSetAllocateInfo;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts;

            res = pfnAllocateDescriptorSets(m_device, &allocInfo, descriptorSets);
            if (res != 0 || !descriptorSets[0]) {
                CBR_LOG_WARN("vkAllocateDescriptorSets warning (code: %d); descriptor set binding deferred.", res);
            }
        }
    }

    m_descriptorPool = descriptorPool;
    m_descriptorSets[0] = descriptorSets[0];
    m_descriptorSets[1] = descriptorSets[1];

    // Cache Vulkan device function pointers to avoid GetProcAddress overhead in render hot loop
    m_pfnCmdPipelineBarrier = reinterpret_cast<PFN_CmdPipelineBarrier>(resolveDeviceFunc("vkCmdPipelineBarrier"));
    m_pfnCmdBlitImage = reinterpret_cast<PFN_CmdBlitImage>(resolveDeviceFunc("vkCmdBlitImage"));
    m_pfnCmdCopyImage = reinterpret_cast<PFN_CmdCopyImage>(resolveDeviceFunc("vkCmdCopyImage"));
    m_pfnCmdClearColorImage = reinterpret_cast<PFN_CmdClearColorImage>(resolveDeviceFunc("vkCmdClearColorImage"));
    m_pfnCmdBindPipeline = reinterpret_cast<PFN_CmdBindPipeline>(resolveDeviceFunc("vkCmdBindPipeline"));
    m_pfnCmdBindDescriptorSets = reinterpret_cast<PFN_CmdBindDescriptorSets>(resolveDeviceFunc("vkCmdBindDescriptorSets"));
    m_pfnCmdPushConstants = reinterpret_cast<PFN_CmdPushConstants>(resolveDeviceFunc("vkCmdPushConstants"));
    m_pfnCmdDispatch = reinterpret_cast<PFN_CmdDispatch>(resolveDeviceFunc("vkCmdDispatch"));

    // Create host-visible coherent camera UBO (binding = 7)
    {
        auto pfnCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(resolveDeviceFunc("vkCreateBuffer"));
        auto pfnDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(resolveDeviceFunc("vkDestroyBuffer"));
        auto pfnGetBufMemReqs = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(resolveDeviceFunc("vkGetBufferMemoryRequirements"));
        auto pfnAllocMem = reinterpret_cast<PFN_vkAllocateMemory>(resolveDeviceFunc("vkAllocateMemory"));
        auto pfnFreeMem = reinterpret_cast<PFN_vkFreeMemory>(resolveDeviceFunc("vkFreeMemory"));
        auto pfnBindBufMem = reinterpret_cast<PFN_vkBindBufferMemory>(resolveDeviceFunc("vkBindBufferMemory"));
        auto pfnMapMem = reinterpret_cast<PFN_vkMapMemory>(resolveDeviceFunc("vkMapMemory"));
        auto pfnGetPhysMemProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            GetVulkanLoaderSymbol(vulkanModule, "vkGetPhysicalDeviceMemoryProperties"));

        if (pfnCreateBuffer && pfnGetBufMemReqs && pfnAllocMem && pfnBindBufMem && pfnMapMem) {
            MinimalVkBufferCreateInfo bufInfo{};
            bufInfo.sType = kVkStructureTypeBufferCreateInfo;
            bufInfo.size = 256;
            bufInfo.usage = kVkBufferUsageUniformBufferBit;
            bufInfo.sharingMode = 0;

            void* uboBuf = nullptr;
            if (pfnCreateBuffer(m_device, &bufInfo, nullptr, &uboBuf) == 0 && uboBuf) {
                MinimalVkMemoryRequirements reqs{};
                pfnGetBufMemReqs(m_device, uboBuf, &reqs);

                uint32_t memTypeIdx = 0;
                MinimalVkPhysMemoryProps memProps{};
                if (m_vkPhysicalDevice && pfnGetPhysMemProps) {
                    pfnGetPhysMemProps(m_vkPhysicalDevice, &memProps);
                    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                        if ((reqs.memoryTypeBits & (1u << i)) &&
                            (memProps.memoryTypes[i].propertyFlags & kVkMemoryPropertyHostVisibleCoherentBits) ==
                                kVkMemoryPropertyHostVisibleCoherentBits) {
                            memTypeIdx = i;
                            break;
                        }
                    }
                } else {
                    for (uint32_t i = 0; i < 32; ++i) {
                        if (reqs.memoryTypeBits & (1u << i)) { memTypeIdx = i; break; }
                    }
                }

                MinimalVkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = kVkStructureTypeMemoryAllocateInfo;
                allocInfo.allocationSize = reqs.size;
                allocInfo.memoryTypeIndex = memTypeIdx;

                void* uboMem = nullptr;
                if (pfnAllocMem(m_device, &allocInfo, nullptr, &uboMem) == 0 && uboMem) {
                    if (pfnBindBufMem(m_device, uboBuf, uboMem, 0) == 0) {
                        void* mapped = nullptr;
                        if (pfnMapMem(m_device, uboMem, 0, sizeof(CBRCameraUniforms), 0, &mapped) == 0 && mapped) {
                            const CBRCameraUniforms initial = BuildCameraUniforms();
                            std::memcpy(mapped, &initial, sizeof(initial));
                            m_cameraUboBuffer = uboBuf;
                            m_cameraUboMemory = uboMem;
                            m_cameraUboMapped = mapped;
                        } else {
                            if (pfnFreeMem) pfnFreeMem(m_device, uboMem, nullptr);
                            if (pfnDestroyBuffer) pfnDestroyBuffer(m_device, uboBuf, nullptr);
                        }
                    } else {
                        if (pfnFreeMem) pfnFreeMem(m_device, uboMem, nullptr);
                        if (pfnDestroyBuffer) pfnDestroyBuffer(m_device, uboBuf, nullptr);
                    }
                } else if (pfnDestroyBuffer) {
                    pfnDestroyBuffer(m_device, uboBuf, nullptr);
                }
            }
        }
    }

    // Create double-buffered 2-timestamp VkQueryPool instances for GPU dispatch duration telemetry (Item M-05)
    m_vkTimestampPeriodNs = 0.0f;
#if defined(CBR_HAS_VULKAN_HEADER)
    if (m_vkPhysicalDevice) {
        auto pfnGetPhysProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            GetVulkanLoaderSymbol(vulkanModule, "vkGetPhysicalDeviceProperties"));
        if (pfnGetPhysProps) {
            VkPhysicalDeviceProperties props{};
            pfnGetPhysProps(m_vkPhysicalDevice, &props);
            m_vkTimestampPeriodNs = props.limits.timestampPeriod;
        }
    }
#endif
    auto pfnCreateQueryPool = reinterpret_cast<PFN_vkCreateQueryPool>(resolveDeviceFunc("vkCreateQueryPool"));
    if (pfnCreateQueryPool) {
        MinimalVkQueryPoolCreateInfo qpInfo{};
        qpInfo.sType = kVkStructureTypeQueryPoolCreateInfo;
        qpInfo.queryType = kVkQueryTypeTimestamp;
        qpInfo.queryCount = 2;
        for (int q = 0; q < 2; ++q) {
            m_vkQueryRecorded[q] = false;
            void* qp = nullptr;
            if (pfnCreateQueryPool(m_device, &qpInfo, nullptr, &qp) == 0 && qp) {
                m_vkQueryPools[q] = qp;
            }
        }
    }

    CBR_LOG_INFO("Vulkan CBR reconstruction compute pipeline created successfully! (Pipeline: %p, Layout: %p, Descriptors: %p, Pool: %p, Sets: %p / %p)",
        m_pipeline, m_pipelineLayout, m_descriptorSetLayout, m_descriptorPool, m_descriptorSets[0], m_descriptorSets[1]);
    return true;
}

void ReconstructionPass::DestroyVulkanPipeline() {
    if (!m_device) return;
    void* vulkanModule = GetVulkanLoaderModule();
    if (!vulkanModule) return;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanModule, name);
    };

    auto pfnDeviceWaitIdle = reinterpret_cast<int (*)(void*)>(resolveDeviceFunc("vkDeviceWaitIdle"));
    if (m_device && pfnDeviceWaitIdle) {
        pfnDeviceWaitIdle(m_device);
    }

    auto pfnDestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(resolveDeviceFunc("vkDestroyPipeline"));
    auto pfnDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(resolveDeviceFunc("vkDestroyPipelineLayout"));
    auto pfnDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(resolveDeviceFunc("vkDestroyDescriptorSetLayout"));
    auto pfnDestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(resolveDeviceFunc("vkDestroyDescriptorPool"));
    auto pfnDestroyQueryPool = reinterpret_cast<PFN_vkDestroyQueryPool>(resolveDeviceFunc("vkDestroyQueryPool"));
    auto pfnUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(resolveDeviceFunc("vkUnmapMemory"));
    auto pfnDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(resolveDeviceFunc("vkDestroyBuffer"));
    auto pfnFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(resolveDeviceFunc("vkFreeMemory"));

    if (m_cameraUboMapped && m_cameraUboMemory && pfnUnmapMemory) {
        pfnUnmapMemory(m_device, m_cameraUboMemory);
    }
    m_cameraUboMapped = nullptr;
    if (m_cameraUboBuffer && pfnDestroyBuffer) {
        pfnDestroyBuffer(m_device, m_cameraUboBuffer, nullptr);
    }
    m_cameraUboBuffer = nullptr;
    if (m_cameraUboMemory && pfnFreeMemory) {
        pfnFreeMemory(m_device, m_cameraUboMemory, nullptr);
    }
    m_cameraUboMemory = nullptr;

    for (int q = 0; q < 2; ++q) {
        if (m_vkQueryPools[q] && pfnDestroyQueryPool) {
            pfnDestroyQueryPool(m_device, m_vkQueryPools[q], nullptr);
        }
        m_vkQueryPools[q] = nullptr;
        m_vkQueryRecorded[q] = false;
    }
    m_vkTimestampPeriodNs = 0.0f;

    if (m_descriptorPool && pfnDestroyDescriptorPool) {
        pfnDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = nullptr;
        m_descriptorSets[0] = nullptr;
        m_descriptorSets[1] = nullptr;
    }
    if (m_pipeline && pfnDestroyPipeline) {
        pfnDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = nullptr;
    }
    if (m_pipelineLayout && pfnDestroyPipelineLayout) {
        pfnDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = nullptr;
    }
    if (m_descriptorSetLayout && pfnDestroyDescriptorSetLayout) {
        pfnDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = nullptr;
    }
    m_pfnCmdPipelineBarrier = nullptr;
    m_pfnCmdBlitImage = nullptr;
    m_pfnCmdCopyImage = nullptr;
    m_pfnCmdClearColorImage = nullptr;
    m_pfnCmdBindPipeline = nullptr;
    m_pfnCmdBindDescriptorSets = nullptr;
    m_pfnCmdPushConstants = nullptr;
    m_pfnCmdDispatch = nullptr;
    m_descriptorsBound.store(false);
    m_initialLayoutsTransitioned.store(false, std::memory_order_release);
}

bool ReconstructionPass::InitializeVulkan(void* vkDevice, void* vkPhysicalDevice) {
    m_isVulkan.store(true);
    m_vkPhysicalDevice = vkPhysicalDevice;
    if (!vkDevice) {
        m_initialized.store(true);
        CBR_LOG_INFO("ReconstructionPass initialized for Vulkan API pipeline (device handle deferred).");
        return true;
    }

    // Reuse existing pipeline if already initialized on the same device (dimensions are resolution-independent)
    if (m_initialized.load() && m_pipeline != nullptr && m_device == vkDevice) {
        return true;
    }

    // Destroy existing pipeline objects on swapchain recreation to prevent driver resource leakage
    if (m_pipeline || m_descriptorPool || m_pipelineLayout || m_descriptorSetLayout) {
        DestroyVulkanPipeline();
    }

    m_device = vkDevice;
    const bool pipelineCreated = CreateVulkanPipeline();
    m_initialized.store(pipelineCreated);
    return pipelineCreated;
}

bool ReconstructionPass::CreateDX12Pipeline() {
#if defined(_WIN32)
    if (!m_d3d12Device) {
        return false;
    }

    HMODULE d3d12Mod = GetModuleHandleA("d3d12.dll");
    if (!d3d12Mod) {
        d3d12Mod = LoadLibraryA("d3d12.dll");
    }
    if (!d3d12Mod) {
        CBR_LOG_WARN("CreateDX12Pipeline: d3d12.dll unavailable.");
        return false;
    }

    typedef long (__stdcall *PFN_D3D12SerializeRootSignature)(
        const MinimalD3D12RootSignatureDesc*, uint32_t, void**, void**);
    auto pfnSerializeRootSig = reinterpret_cast<PFN_D3D12SerializeRootSignature>(
        GetProcAddress(d3d12Mod, "D3D12SerializeRootSignature"));
    if (!pfnSerializeRootSig) {
        CBR_LOG_ERROR("CreateDX12Pipeline: D3D12SerializeRootSignature not found.");
        return false;
    }

    // 1. Load precompiled cbr_reconstruct.dxil/.cso if present, otherwise compile shaders/cbr_reconstruct.hlsl via d3dcompiler_47.dll
    std::vector<uint8_t> bytecodeBytes;
    const std::filesystem::path baseShaderDir = m_shaderDir.empty() ? std::filesystem::path("shaders") : m_shaderDir;
    const std::filesystem::path binaryCandidates[] = {
        baseShaderDir / "cbr_reconstruct.cso",
        baseShaderDir / "cbr_reconstruct.dxil",
        std::filesystem::path("shaders") / "cbr_reconstruct.cso",
        std::filesystem::path("shaders") / "cbr_reconstruct.dxil"
    };
    for (const auto& candidate : binaryCandidates) {
        std::ifstream f(candidate, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            const auto sz = f.tellg();
            if (sz > 16) {
                bytecodeBytes.resize(static_cast<size_t>(sz));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(bytecodeBytes.data()), sz);
                CBR_LOG_INFO("Loaded precompiled DX12 shader bytecode from %s (%zu bytes).", candidate.string().c_str(), bytecodeBytes.size());
                break;
            }
        }
    }

    void* compiledShaderBlob = nullptr;
    if (bytecodeBytes.empty()) {
        std::filesystem::path hlslPath = baseShaderDir / "cbr_reconstruct.hlsl";
        std::ifstream hlslFile(hlslPath, std::ios::binary | std::ios::ate);
        if (!hlslFile.is_open()) {
            hlslPath = std::filesystem::path("shaders") / "cbr_reconstruct.hlsl";
            hlslFile.open(hlslPath, std::ios::binary | std::ios::ate);
        }
        if (!hlslFile.is_open()) {
            CBR_LOG_WARN("CreateDX12Pipeline: could not find cbr_reconstruct.hlsl (checked %s).", hlslPath.string().c_str());
            return false;
        }
        const auto hlslSize = hlslFile.tellg();
        if (hlslSize <= 0) {
            return false;
        }
        std::string hlslSource(static_cast<size_t>(hlslSize), '\0');
        hlslFile.seekg(0);
        hlslFile.read(hlslSource.data(), hlslSize);

        HMODULE d3dCompilerMod = GetModuleHandleA("d3dcompiler_47.dll");
        if (!d3dCompilerMod) {
            d3dCompilerMod = LoadLibraryA("d3dcompiler_47.dll");
        }
        if (!d3dCompilerMod) {
            CBR_LOG_WARN("CreateDX12Pipeline: d3dcompiler_47.dll unavailable; DX12 compute PSO deferred.");
            return false;
        }

        typedef long (__stdcall *PFN_D3DCompile)(
            const void*, size_t, const char*, const void*, void*,
            const char*, const char*, uint32_t, uint32_t, void**, void**);
        auto pfnD3DCompile = reinterpret_cast<PFN_D3DCompile>(GetProcAddress(d3dCompilerMod, "D3DCompile"));
        if (!pfnD3DCompile) {
            CBR_LOG_WARN("CreateDX12Pipeline: D3DCompile export unavailable in d3dcompiler_47.dll.");
            return false;
        }

        void* errBlob = nullptr;
        constexpr uint32_t kD3DCompileOptLevel3 = (1u << 15);
        const long hrCompile = pfnD3DCompile(
            hlslSource.data(), hlslSource.size(),
            "cbr_reconstruct.hlsl", nullptr, nullptr,
            "CSMain", "cs_5_1", kD3DCompileOptLevel3, 0,
            &compiledShaderBlob, &errBlob);
        if (errBlob) {
            const void* errPtr = GetBlobPtr(errBlob);
            const size_t errLen = GetBlobSize(errBlob);
            if (errPtr && errLen > 0) {
                CBR_LOG_WARN("DX12 HLSL compiler message: %.*s", static_cast<int>(errLen), static_cast<const char*>(errPtr));
            }
            ReleaseBlobOrCom(errBlob);
        }
        if (hrCompile < 0 || !compiledShaderBlob) {
            CBR_LOG_ERROR("CreateDX12Pipeline: runtime HLSL compilation failed (HRESULT=0x%08lX).", static_cast<unsigned long>(hrCompile));
            ReleaseBlobOrCom(compiledShaderBlob);
            return false;
        }
    }

    // 2. Serialize root signature matching shaders/cbr_reconstruct.hlsl (b0 constants, t0..t4 SRVs, u0..u1 UAVs, s0..s1 Samplers, b1 camera CBV)
    MinimalD3D12DescriptorRange srvRange{ kD3D12DescriptorRangeSRV, 5, 0, 0, 0xFFFFFFFFu };
    MinimalD3D12DescriptorRange uavRange{ kD3D12DescriptorRangeUAV, 2, 0, 0, 0xFFFFFFFFu };
    MinimalD3D12DescriptorRange samplerRange{ kD3D12DescriptorRangeSampler, 2, 0, 0, 0xFFFFFFFFu };

    MinimalD3D12RootParameter rootParams[5]{};
    rootParams[0].ParameterType = kD3D12RootParamType32BitConstants;
    rootParams[0].Constants = { 0, 0, static_cast<uint32_t>(sizeof(ReconstructionPushConstants) / 4u) };
    rootParams[0].ShaderVisibility = kD3D12ShaderVisibilityAll;

    rootParams[1].ParameterType = kD3D12RootParamTypeDescriptorTable;
    rootParams[1].DescriptorTable = { 1, &srvRange };
    rootParams[1].ShaderVisibility = kD3D12ShaderVisibilityAll;

    rootParams[2].ParameterType = kD3D12RootParamTypeDescriptorTable;
    rootParams[2].DescriptorTable = { 1, &uavRange };
    rootParams[2].ShaderVisibility = kD3D12ShaderVisibilityAll;

    rootParams[3].ParameterType = kD3D12RootParamTypeDescriptorTable;
    rootParams[3].DescriptorTable = { 1, &samplerRange };
    rootParams[3].ShaderVisibility = kD3D12ShaderVisibilityAll;

    rootParams[4].ParameterType = kD3D12RootParamTypeCBV;
    rootParams[4].Descriptor = { 1, 0 };
    rootParams[4].ShaderVisibility = kD3D12ShaderVisibilityAll;

    MinimalD3D12RootSignatureDesc rsDesc{};
    rsDesc.NumParameters = 5;
    rsDesc.pParameters = rootParams;

    void* rsBlob = nullptr;
    void* rsErrBlob = nullptr;
    const long hrSer = pfnSerializeRootSig(&rsDesc, kD3D12RootSignatureVersion1, &rsBlob, &rsErrBlob);
    ReleaseBlobOrCom(rsErrBlob);
    if (hrSer < 0 || !rsBlob) {
        CBR_LOG_ERROR("CreateDX12Pipeline: D3D12SerializeRootSignature failed (0x%08lX).", static_cast<unsigned long>(hrSer));
        ReleaseBlobOrCom(rsBlob);
        ReleaseBlobOrCom(compiledShaderBlob);
        return false;
    }

    void** devVmt = *reinterpret_cast<void***>(m_d3d12Device);
    if (!devVmt || !devVmt[11] || !devVmt[16]) {
        ReleaseBlobOrCom(rsBlob);
        ReleaseBlobOrCom(compiledShaderBlob);
        return false;
    }

    typedef long (__stdcall *PFN_CreateComputePSO)(void*, const MinimalD3D12ComputePipelineStateDesc*, const GUID*, void**);
    typedef long (__stdcall *PFN_CreateRootSignature)(void*, uint32_t, const void*, size_t, const GUID*, void**);

    static const GUID kIID_ID3D12RootSignature =
        { 0xc54a6b66, 0x72df, 0x4ee8, { 0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x92, 0x14 } };
    static const GUID kIID_ID3D12PipelineState =
        { 0x765a30f3, 0xf624, 0x4c6f, { 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45 } };

    auto pfnCreateRootSig = reinterpret_cast<PFN_CreateRootSignature>(devVmt[16]);
    auto pfnCreatePSO = reinterpret_cast<PFN_CreateComputePSO>(devVmt[11]);

    void* rootSig = nullptr;
    const long hrRS = pfnCreateRootSig(m_d3d12Device, 0, GetBlobPtr(rsBlob), GetBlobSize(rsBlob), &kIID_ID3D12RootSignature, &rootSig);
    ReleaseBlobOrCom(rsBlob);
    if (hrRS < 0 || !rootSig) {
        CBR_LOG_ERROR("CreateDX12Pipeline: CreateRootSignature failed (0x%08lX).", static_cast<unsigned long>(hrRS));
        ReleaseBlobOrCom(compiledShaderBlob);
        return false;
    }

    MinimalD3D12ComputePipelineStateDesc psoDesc{};
    psoDesc.pRootSignature = rootSig;
    psoDesc.CS.pShaderBytecode = compiledShaderBlob ? GetBlobPtr(compiledShaderBlob) : bytecodeBytes.data();
    psoDesc.CS.BytecodeLength = compiledShaderBlob ? GetBlobSize(compiledShaderBlob) : bytecodeBytes.size();

    void* pso = nullptr;
    const long hrPSO = pfnCreatePSO(m_d3d12Device, &psoDesc, &kIID_ID3D12PipelineState, &pso);
    ReleaseBlobOrCom(compiledShaderBlob);
    if (hrPSO < 0 || !pso) {
        CBR_LOG_ERROR("CreateDX12Pipeline: CreateComputePipelineState failed (0x%08lX).", static_cast<unsigned long>(hrPSO));
        ReleaseBlobOrCom(rootSig);
        return false;
    }

    m_d3d12RootSignature = rootSig;
    m_d3d12PipelineState = pso;

    // Create 256-byte upload-heap CBV resource for CBRCameraData (register(b1))
    if (devVmt[27]) {
        typedef long (__stdcall *PFN_CreateCommittedResource)(void*, const MinimalD3D12HeapProperties*, uint32_t,
                                                              const MinimalD3D12BufferDesc*, uint32_t, const void*,
                                                              const GUID*, void**);
        static const GUID kIID_ID3D12Resource =
            { 0x696442be, 0xa72e, 0x4059, { 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad } };
        auto pfnCreateCommitted = reinterpret_cast<PFN_CreateCommittedResource>(devVmt[27]);

        MinimalD3D12HeapProperties uploadHeap{};
        uploadHeap.Type = kD3D12HeapTypeUpload;
        uploadHeap.CreationNodeMask = 1;
        uploadHeap.VisibleNodeMask = 1;

        MinimalD3D12BufferDesc bufDesc{};
        bufDesc.Dimension = kD3D12ResourceDimensionBuffer;
        bufDesc.Width = 256;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleCount = 1;
        bufDesc.Layout = kD3D12TextureLayoutRowMajor;

        void* cbvRes = nullptr;
        const long hrCbv = pfnCreateCommitted(m_d3d12Device, &uploadHeap, 0, &bufDesc,
                                              kD3D12ResourceStateGenericRead, nullptr,
                                              &kIID_ID3D12Resource, &cbvRes);
        if (hrCbv >= 0 && cbvRes) {
            void** resVmt = *reinterpret_cast<void***>(cbvRes);
            typedef long (__stdcall *PFN_Map)(void*, uint32_t, const void*, void**);
            typedef uint64_t (__stdcall *PFN_GetGpuVa)(void*);
            if (resVmt && resVmt[8] && resVmt[11]) {
                void* mapped = nullptr;
                if (reinterpret_cast<PFN_Map>(resVmt[8])(cbvRes, 0, nullptr, &mapped) >= 0 && mapped) {
                    const CBRCameraUniforms initial = BuildCameraUniforms();
                    std::memcpy(mapped, &initial, sizeof(initial));
                    m_d3d12CameraCbvResource = cbvRes;
                    m_d3d12CameraCbvMapped = mapped;
                    m_d3d12CameraCbvGpuVa = reinterpret_cast<PFN_GetGpuVa>(resVmt[11])(cbvRes);
                } else {
                    ReleaseBlobOrCom(cbvRes);
                }
            } else {
                ReleaseBlobOrCom(cbvRes);
            }
        }
    }

    CBR_LOG_INFO("DX12 CBR compute RootSignature (%p) and PSO (%p) created successfully.", rootSig, pso);
    return true;
#else
    return true;
#endif
}

void ReconstructionPass::DestroyDX12Pipeline() {
#if defined(_WIN32)
    if (m_d3d12CameraCbvResource) {
        if (m_d3d12CameraCbvMapped) {
            void** resVmt = *reinterpret_cast<void***>(m_d3d12CameraCbvResource);
            typedef void (__stdcall *PFN_Unmap)(void*, uint32_t, const void*);
            if (resVmt && resVmt[9]) {
                reinterpret_cast<PFN_Unmap>(resVmt[9])(m_d3d12CameraCbvResource, 0, nullptr);
            }
        }
        ReleaseBlobOrCom(m_d3d12CameraCbvResource);
    }
    m_d3d12CameraCbvMapped = nullptr;
    m_d3d12CameraCbvGpuVa = 0;
    ReleaseBlobOrCom(m_d3d12PipelineState);
    ReleaseBlobOrCom(m_d3d12RootSignature);
#else
    m_d3d12CameraCbvResource = nullptr;
    m_d3d12CameraCbvMapped = nullptr;
    m_d3d12CameraCbvGpuVa = 0;
    m_d3d12PipelineState = nullptr;
    m_d3d12RootSignature = nullptr;
#endif
}

bool ReconstructionPass::InitializeDX12(void* d3d12Device) {
    m_isVulkan.store(false);
    if (!d3d12Device) {
        m_d3d12Device = nullptr;
        m_initialized.store(true);
        CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline (device handle deferred).");
        return true;
    }

    if (m_initialized.load() && m_d3d12PipelineState && m_d3d12Device == d3d12Device) {
        return true;
    }

    DestroyDX12Pipeline();
    m_d3d12Device = d3d12Device;
    const bool ok = CreateDX12Pipeline();
    m_initialized.store(ok);
    CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline (device: %p, PSO: %s).",
                 d3d12Device, ok ? "READY" : "FAILED");
    return ok;
}

void ReconstructionPass::Shutdown() {
    if (m_isVulkan.load()) {
        DestroyVulkanPipeline();
    } else {
        DestroyDX12Pipeline();
    }
    m_device = nullptr;
    m_vkPhysicalDevice = nullptr;
    m_pipeline = nullptr;
    m_pipelineLayout = nullptr;
    m_descriptorSetLayout = nullptr;
    m_descriptorPool = nullptr;
    m_descriptorSets[0] = nullptr;
    m_descriptorSets[1] = nullptr;
    m_cameraUboBuffer = nullptr;
    m_cameraUboMemory = nullptr;
    m_cameraUboMapped = nullptr;
    m_d3d12Device = nullptr;
    m_d3d12RootSignature = nullptr;
    m_d3d12PipelineState = nullptr;
    m_d3d12CameraCbvResource = nullptr;
    m_d3d12CameraCbvMapped = nullptr;
    m_d3d12CameraCbvGpuVa = 0;
    ClearCameraMatrices();
    m_pfnCmdCopyImage = nullptr;
    m_pfnCmdClearColorImage = nullptr;
    m_lastDispatchDurationMs.store(0.0f, std::memory_order_release);
    m_descriptorsBound.store(false);
    m_initialLayoutsTransitioned.store(false, std::memory_order_release);
    m_initialized.store(false);
    CBR_LOG_INFO("ReconstructionPass shut down cleanly.");
}

bool ReconstructionPass::UpdateVulkanDescriptors(void* quarterColorView, void* quarterDepthView,
                                                void* historyColorView, void* historyDepthView,
                                                void* velocityView,
                                                void* outputImageView, void* outputDepthView,
                                                void* linearSampler, void* pointSampler) {
    void* targetSet = m_descriptorSets[0] ? m_descriptorSets[0] : nullptr;
    if (!m_device || !targetSet) return false;

    void* vulkanModule = GetVulkanLoaderModule();
    if (!vulkanModule) return false;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanModule, name);
    };

    auto pfnUpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(resolveDeviceFunc("vkUpdateDescriptorSets"));
    if (!pfnUpdateDescriptorSets) return false;

    MinimalVkDescriptorImageInfo imageInfos[7]{};
    imageInfos[0].sampler = pointSampler;
    imageInfos[0].imageView = quarterColorView;
    imageInfos[0].imageLayout = kVkImageLayoutShaderReadOnlyOptimal;

    imageInfos[1].sampler = pointSampler;
    imageInfos[1].imageView = quarterDepthView;
    imageInfos[1].imageLayout = kVkImageLayoutShaderReadOnlyOptimal;

    imageInfos[2].sampler = linearSampler;
    imageInfos[2].imageView = historyColorView;
    imageInfos[2].imageLayout = kVkImageLayoutShaderReadOnlyOptimal;

    imageInfos[3].sampler = pointSampler;
    imageInfos[3].imageView = historyDepthView;
    imageInfos[3].imageLayout = kVkImageLayoutShaderReadOnlyOptimal;

    imageInfos[4].sampler = pointSampler;
    imageInfos[4].imageView = velocityView;
    imageInfos[4].imageLayout = kVkImageLayoutShaderReadOnlyOptimal;

    imageInfos[5].sampler = nullptr;
    imageInfos[5].imageView = outputImageView;
    imageInfos[5].imageLayout = kVkImageLayoutGeneral;

    imageInfos[6].sampler = nullptr;
    imageInfos[6].imageView = outputDepthView;
    imageInfos[6].imageLayout = kVkImageLayoutGeneral;

    MinimalVkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = m_cameraUboBuffer;
    uboInfo.offset = 0;
    uboInfo.range = sizeof(CBRCameraUniforms);

    MinimalVkWriteDescriptorSet writes[8]{};
    for (uint32_t i = 0; i < 7; ++i) {
        writes[i].sType = kVkStructureTypeWriteDescriptorSet;
        writes[i].dstSet = targetSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i < 5) ? kVkDescriptorTypeCombinedImageSampler : kVkDescriptorTypeStorageImage;
        writes[i].pImageInfo = &imageInfos[i];
    }
    uint32_t writeCount = 7;
    if (m_cameraUboBuffer) {
        writes[7].sType = kVkStructureTypeWriteDescriptorSet;
        writes[7].dstSet = targetSet;
        writes[7].dstBinding = 7;
        writes[7].dstArrayElement = 0;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = kVkDescriptorTypeUniformBuffer;
        writes[7].pBufferInfo = &uboInfo;
        writeCount = 8;
    }

    pfnUpdateDescriptorSets(m_device, writeCount, writes, 0, nullptr);
    m_descriptorsBound.store(true);
    CBR_LOG_INFO("ReconstructionPass: Vulkan descriptors successfully updated with active image views.");
    return true;
}

bool ReconstructionPass::UpdateVulkanPingPongDescriptors(void* quarterColorView, void* quarterDepthView,
                                                        void* historyColorViewA, void* historyDepthViewA,
                                                        void* historyColorViewB, void* historyDepthViewB,
                                                        void* velocityView,
                                                        void* outputImageView, void* outputDepthView,
                                                        void* linearSampler, void* pointSampler) {
    (void)outputImageView;
    (void)outputDepthView;
    if (!m_device || !m_descriptorSets[0] || !m_descriptorSets[1]) return false;
    if (!quarterColorView || !quarterDepthView || !velocityView ||
        !historyColorViewA || !historyDepthViewA ||
        !historyColorViewB || !historyDepthViewB) {
        CBR_LOG_ERROR("UpdateVulkanPingPongDescriptors: one or more required ping-pong image views are null.");
        return false;
    }

    void* vulkanModule = GetVulkanLoaderModule();
    if (!vulkanModule) return false;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanModule, name);
    };

    auto pfnUpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(resolveDeviceFunc("vkUpdateDescriptorSets"));
    if (!pfnUpdateDescriptorSets) return false;

    // Set 0: Reads History A (bindings 2,3), writes directly to History B (bindings 5,6) (Item C-02)
    MinimalVkDescriptorImageInfo imageInfos0[7]{};
    imageInfos0[0] = { pointSampler, quarterColorView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[1] = { pointSampler, quarterDepthView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[2] = { linearSampler, historyColorViewA, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[3] = { pointSampler, historyDepthViewA, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[4] = { pointSampler, velocityView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[5] = { nullptr, historyColorViewB, kVkImageLayoutGeneral };
    imageInfos0[6] = { nullptr, historyDepthViewB, kVkImageLayoutGeneral };

    // Set 1: Reads History B (bindings 2,3), writes directly to History A (bindings 5,6) (Item C-02)
    MinimalVkDescriptorImageInfo imageInfos1[7]{};
    imageInfos1[0] = { pointSampler, quarterColorView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[1] = { pointSampler, quarterDepthView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[2] = { linearSampler, historyColorViewB, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[3] = { pointSampler, historyDepthViewB, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[4] = { pointSampler, velocityView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[5] = { nullptr, historyColorViewA, kVkImageLayoutGeneral };
    imageInfos1[6] = { nullptr, historyDepthViewA, kVkImageLayoutGeneral };

    MinimalVkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = m_cameraUboBuffer;
    uboInfo.offset = 0;
    uboInfo.range = sizeof(CBRCameraUniforms);

    MinimalVkWriteDescriptorSet writes[16]{};
    for (uint32_t i = 0; i < 7; ++i) {
        writes[i].sType = kVkStructureTypeWriteDescriptorSet;
        writes[i].dstSet = m_descriptorSets[0];
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i < 5) ? kVkDescriptorTypeCombinedImageSampler : kVkDescriptorTypeStorageImage;
        writes[i].pImageInfo = &imageInfos0[i];

        writes[i + 7].sType = kVkStructureTypeWriteDescriptorSet;
        writes[i + 7].dstSet = m_descriptorSets[1];
        writes[i + 7].dstBinding = i;
        writes[i + 7].dstArrayElement = 0;
        writes[i + 7].descriptorCount = 1;
        writes[i + 7].descriptorType = (i < 5) ? kVkDescriptorTypeCombinedImageSampler : kVkDescriptorTypeStorageImage;
        writes[i + 7].pImageInfo = &imageInfos1[i];
    }
    uint32_t writeCount = 14;
    if (m_cameraUboBuffer) {
        writes[14].sType = kVkStructureTypeWriteDescriptorSet;
        writes[14].dstSet = m_descriptorSets[0];
        writes[14].dstBinding = 7;
        writes[14].dstArrayElement = 0;
        writes[14].descriptorCount = 1;
        writes[14].descriptorType = kVkDescriptorTypeUniformBuffer;
        writes[14].pBufferInfo = &uboInfo;

        writes[15].sType = kVkStructureTypeWriteDescriptorSet;
        writes[15].dstSet = m_descriptorSets[1];
        writes[15].dstBinding = 7;
        writes[15].dstArrayElement = 0;
        writes[15].descriptorCount = 1;
        writes[15].descriptorType = kVkDescriptorTypeUniformBuffer;
        writes[15].pBufferInfo = &uboInfo;
        writeCount = 16;
    }

    pfnUpdateDescriptorSets(m_device, writeCount, writes, 0, nullptr);
    m_descriptorsBound.store(true);
    CBR_LOG_INFO("ReconstructionPass: Dual ping-pong Vulkan descriptor sets (Set 0 & 1) updated.");
    return true;
}

void ReconstructionPass::TransitionImageLayout(void* vkCommandBuffer, void* vkImage,
                                              uint32_t oldLayout, uint32_t newLayout,
                                              uint32_t srcStage, uint32_t dstStage,
                                              uint32_t srcAccess, uint32_t dstAccess,
                                              uint32_t aspectMask) {
    if (!vkCommandBuffer || !vkImage) return;

    auto pfnCmdPipelineBarrier = ReconstructionPass::Get().m_pfnCmdPipelineBarrier;
    if (!pfnCmdPipelineBarrier) {
        void* vulkanModule = GetVulkanLoaderModule();
        if (vulkanModule) {
            pfnCmdPipelineBarrier = reinterpret_cast<PFN_CmdPipelineBarrier>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdPipelineBarrier"));
        }
    }
    if (!pfnCmdPipelineBarrier) return;

    MinimalVkImageMemoryBarrier barrier{};
    barrier.sType = kVkStructureTypeImageMemoryBarrier;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = ~0u; // VK_QUEUE_FAMILY_IGNORED
    barrier.dstQueueFamilyIndex = ~0u;
    barrier.image = vkImage;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    pfnCmdPipelineBarrier(vkCommandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    RenderTargetManager::Get().SetVulkanImageLayout(vkImage, newLayout);
}

void ReconstructionPass::BlitToImage(void* vkCommandBuffer, void* srcImage, void* dstImage,
                                    uint32_t srcWidth, uint32_t srcHeight,
                                    uint32_t dstWidth, uint32_t dstHeight,
                                    uint32_t targetDstLayout,
                                    uint32_t srcOldLayout,
                                    uint32_t dstOldLayout) {
    if (!vkCommandBuffer || !srcImage || !dstImage) return;

    auto pfnCmdBlitImage = ReconstructionPass::Get().m_pfnCmdBlitImage;
    if (!pfnCmdBlitImage) {
        void* vulkanModule = GetVulkanLoaderModule();
        if (vulkanModule) {
            pfnCmdBlitImage = reinterpret_cast<PFN_CmdBlitImage>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdBlitImage"));
        }
    }
    if (!pfnCmdBlitImage) return;

    uint32_t srcStage = kVkPipelineStageAllCommandsBit;
    uint32_t srcAccess = 0;
    StageAccessForLayout(srcOldLayout, kVkImageAspectColorBit, srcStage, srcAccess);

    uint32_t dstStage = kVkPipelineStageAllCommandsBit;
    uint32_t dstAccess = 0;
    StageAccessForLayout(dstOldLayout, kVkImageAspectColorBit, dstStage, dstAccess);

    // Transition srcImage from tracked layout to TRANSFER_SRC_OPTIMAL without discarding pixels
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          srcOldLayout, kVkImageLayoutTransferSrcOptimal,
                          srcStage, kVkPipelineStageTransferBit,
                          srcAccess, kVkAccessTransferReadBit,
                          kVkImageAspectColorBit);

    // Transition dstImage from tracked layout to TRANSFER_DST_OPTIMAL
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          dstOldLayout, kVkImageLayoutTransferDstOptimal,
                          dstStage, kVkPipelineStageTransferBit,
                          dstAccess, kVkAccessTransferWriteBit,
                          kVkImageAspectColorBit);

    MinimalVkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = kVkImageAspectColorBit;
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = { 0, 0, 0 };
    blitRegion.srcOffsets[1] = { static_cast<int32_t>(srcWidth), static_cast<int32_t>(srcHeight), 1 };

    blitRegion.dstSubresource.aspectMask = kVkImageAspectColorBit;
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[0] = { 0, 0, 0 };
    blitRegion.dstOffsets[1] = { static_cast<int32_t>(dstWidth), static_cast<int32_t>(dstHeight), 1 };

    pfnCmdBlitImage(vkCommandBuffer,
                    srcImage, kVkImageLayoutTransferSrcOptimal,
                    dstImage, kVkImageLayoutTransferDstOptimal,
                    1, &blitRegion, kVkFilterLinear);

    uint32_t restoreDstStage = kVkPipelineStageBottomOfPipeBit;
    uint32_t restoreDstAccess = 0;
    StageAccessForLayout(targetDstLayout, kVkImageAspectColorBit, restoreDstStage, restoreDstAccess);
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          kVkImageLayoutTransferDstOptimal, targetDstLayout,
                          kVkPipelineStageTransferBit, restoreDstStage,
                          kVkAccessTransferWriteBit, restoreDstAccess,
                          kVkImageAspectColorBit);

    // Restore srcImage back to its prior layout (e.g. GENERAL)
    const uint32_t restoreSrcLayout = (srcOldLayout != 0u) ? srcOldLayout : kVkImageLayoutGeneral;
    uint32_t restoreSrcStage = kVkPipelineStageComputeShaderBit;
    uint32_t restoreSrcAccess = kVkAccessShaderWriteBit;
    StageAccessForLayout(restoreSrcLayout, kVkImageAspectColorBit, restoreSrcStage, restoreSrcAccess);
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          kVkImageLayoutTransferSrcOptimal, restoreSrcLayout,
                          kVkPipelineStageTransferBit, restoreSrcStage,
                          kVkAccessTransferReadBit, restoreSrcAccess,
                          kVkImageAspectColorBit);
}

void ReconstructionPass::CopyImageRegion(void* vkCommandBuffer, void* srcImage, void* dstImage,
                                         uint32_t srcX, uint32_t srcY,
                                         uint32_t dstX, uint32_t dstY,
                                         uint32_t width, uint32_t height,
                                         uint32_t aspectMask,
                                         uint32_t srcOldLayout,
                                         uint32_t dstOldLayout,
                                         uint32_t srcFinalLayout,
                                         uint32_t dstFinalLayout,
                                         uint32_t dstAspectMask) {
    if (!vkCommandBuffer || !srcImage || !dstImage || width == 0 || height == 0) return;

    auto pfnCmdCopyImage = ReconstructionPass::Get().m_pfnCmdCopyImage;
    if (!pfnCmdCopyImage) {
        void* vulkanModule = GetVulkanLoaderModule();
        if (vulkanModule) {
            pfnCmdCopyImage = reinterpret_cast<PFN_CmdCopyImage>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdCopyImage"));
            ReconstructionPass::Get().m_pfnCmdCopyImage = pfnCmdCopyImage;
        }
    }
    if (!pfnCmdCopyImage) return;

    const uint32_t effectiveDstAspect = (dstAspectMask != 0u) ? dstAspectMask : aspectMask;

    uint32_t srcStage = kVkPipelineStageAllCommandsBit;
    uint32_t srcAccess = 0;
    StageAccessForLayout(srcOldLayout, aspectMask, srcStage, srcAccess);

    uint32_t dstStage = kVkPipelineStageAllCommandsBit;
    uint32_t dstAccess = 0;
    StageAccessForLayout(dstOldLayout, effectiveDstAspect, dstStage, dstAccess);

    // Transition srcImage from tracked layout to TRANSFER_SRC_OPTIMAL without discarding pixels
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          srcOldLayout, kVkImageLayoutTransferSrcOptimal,
                          srcStage, kVkPipelineStageTransferBit,
                          srcAccess, kVkAccessTransferReadBit,
                          aspectMask);

    // Transition dstImage to TRANSFER_DST_OPTIMAL (entire destination is overwritten or preserved cleanly)
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          dstOldLayout, kVkImageLayoutTransferDstOptimal,
                          dstStage, kVkPipelineStageTransferBit,
                          dstAccess, kVkAccessTransferWriteBit,
                          effectiveDstAspect);

    MinimalVkImageCopy region{};
    region.srcSubresource.aspectMask = aspectMask;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { static_cast<int32_t>(srcX), static_cast<int32_t>(srcY), 0 };

    region.dstSubresource.aspectMask = effectiveDstAspect;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { static_cast<int32_t>(dstX), static_cast<int32_t>(dstY), 0 };

    region.extent = { width, height, 1 };

    pfnCmdCopyImage(vkCommandBuffer,
                    srcImage, kVkImageLayoutTransferSrcOptimal,
                    dstImage, kVkImageLayoutTransferDstOptimal,
                    1, &region);

    uint32_t dstFinalStage = kVkPipelineStageComputeShaderBit;
    uint32_t dstFinalAccess = kVkAccessShaderReadBit;
    StageAccessForLayout(dstFinalLayout, effectiveDstAspect, dstFinalStage, dstFinalAccess);
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          kVkImageLayoutTransferDstOptimal, dstFinalLayout,
                          kVkPipelineStageTransferBit, dstFinalStage,
                          kVkAccessTransferWriteBit, dstFinalAccess,
                          effectiveDstAspect);

    uint32_t srcFinalStage = kVkPipelineStageColorAttachmentOutputBit;
    uint32_t srcFinalAccess = kVkAccessColorAttachmentWriteBit | kVkAccessColorAttachmentReadBit;
    StageAccessForLayout(srcFinalLayout, aspectMask, srcFinalStage, srcFinalAccess);
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          kVkImageLayoutTransferSrcOptimal, srcFinalLayout,
                          kVkPipelineStageTransferBit, srcFinalStage,
                          kVkAccessTransferReadBit, srcFinalAccess,
                          aspectMask);
}

void ReconstructionPass::EnsureInitialLayoutsTransitioned(void* vkCommandBuffer) {
    if (!vkCommandBuffer || !RenderTargetManager::Get().AreVulkanResourcesCreated()) {
        return;
    }
    if (m_initialLayoutsTransitioned.load(std::memory_order_acquire)) {
        return;
    }
    if (m_initialLayoutsTransitioned.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const auto& res = RenderTargetManager::Get().GetVulkanResources();

    auto pfnCmdClearColorImage = m_pfnCmdClearColorImage;
    if (!pfnCmdClearColorImage) {
        void* vulkanModule = GetVulkanLoaderModule();
        if (vulkanModule) {
            pfnCmdClearColorImage = reinterpret_cast<PFN_CmdClearColorImage>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdClearColorImage"));
            m_pfnCmdClearColorImage = pfnCmdClearColorImage;
        }
    }

    // 1. Transition input targets out of VK_IMAGE_LAYOUT_UNDEFINED -> SHADER_READ_ONLY_OPTIMAL if not already copied (Item C-03)
    const VulkanTextureResource* inputTargets[] = { &res.quarterColor, &res.quarterDepth, &res.velocity };
    for (const VulkanTextureResource* tex : inputTargets) {
        if (!tex->image) continue;
        const uint32_t currentLayout = RenderTargetManager::Get().GetVulkanImageLayout(tex->image);
        if (currentLayout == kVkImageLayoutShaderReadOnlyOptimal) continue;
        TransitionImageLayout(vkCommandBuffer, tex->image,
                              currentLayout, kVkImageLayoutShaderReadOnlyOptimal,
                              kVkPipelineStageTopOfPipeBit, kVkPipelineStageComputeShaderBit | kVkPipelineStageTransferBit,
                              0, kVkAccessShaderReadBit | kVkAccessTransferWriteBit, tex->aspectMask);
    }

    // 2. Clear all 4 ping-pong history images to zero on initial creation (Item C-03)
    const VulkanTextureResource* historyTargets[] = {
        &res.historyColor[0], &res.historyDepth[0],
        &res.historyColor[1], &res.historyDepth[1]
    };
    const uint32_t historyInitialLayouts[] = {
        kVkImageLayoutShaderReadOnlyOptimal, kVkImageLayoutShaderReadOnlyOptimal,
        kVkImageLayoutGeneral, kVkImageLayoutGeneral
    };
    const MinimalVkClearColorValue zeroColor{ { 0.0f, 0.0f, 0.0f, 0.0f } };
    for (size_t i = 0; i < 4; ++i) {
        const VulkanTextureResource* tex = historyTargets[i];
        if (!tex->image) continue;
        TransitionImageLayout(vkCommandBuffer, tex->image,
                              tex->currentLayout, kVkImageLayoutTransferDstOptimal,
                              kVkPipelineStageTopOfPipeBit, kVkPipelineStageTransferBit,
                              0, kVkAccessTransferWriteBit, tex->aspectMask);
        if (pfnCmdClearColorImage) {
            MinimalVkImageSubresourceRange clearRange{};
            clearRange.aspectMask = tex->aspectMask;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            pfnCmdClearColorImage(vkCommandBuffer, tex->image, kVkImageLayoutTransferDstOptimal, &zeroColor, 1, &clearRange);
        }
        const uint32_t targetLayout = historyInitialLayouts[i];
        uint32_t dstStage = kVkPipelineStageComputeShaderBit;
        uint32_t dstAccess = (targetLayout == kVkImageLayoutGeneral) ? kVkAccessShaderWriteBit : kVkAccessShaderReadBit;
        TransitionImageLayout(vkCommandBuffer, tex->image,
                              kVkImageLayoutTransferDstOptimal, targetLayout,
                              kVkPipelineStageTransferBit, dstStage,
                              kVkAccessTransferWriteBit, dstAccess, tex->aspectMask);
    }

    // 3. Transition auxiliary output staging buffers out of UNDEFINED -> GENERAL
    const VulkanTextureResource* outputTargets[] = { &res.outputColor, &res.outputDepth };
    for (const VulkanTextureResource* tex : outputTargets) {
        if (!tex->image) continue;
        TransitionImageLayout(vkCommandBuffer, tex->image,
                              tex->currentLayout, kVkImageLayoutGeneral,
                              kVkPipelineStageTopOfPipeBit, kVkPipelineStageComputeShaderBit | kVkPipelineStageTransferBit,
                              0, kVkAccessShaderWriteBit | kVkAccessTransferWriteBit, tex->aspectMask);
    }

    CBR_LOG_INFO("ReconstructionPass: All CBR VkImages transitioned out of UNDEFINED and history buffers cleared to zero.");
}

ReconstructionPushConstants BuildReconstructionPushConstants(uint32_t frameIndex) {
    const auto config = ConfigManager::Get().GetConfig();
    const auto dims = RenderTargetManager::Get().GetDimensions();
    const auto jitterDelta = JitterManager::Get().GetJitterDelta();

    ReconstructionPushConstants pc{};
    const float fw = dims.fullWidth > 0 ? static_cast<float>(dims.fullWidth) : 3840.0f;
    const float fh = dims.fullHeight > 0 ? static_cast<float>(dims.fullHeight) : 2160.0f;
    pc.targetResolution[0] = fw;
    pc.targetResolution[1] = fh;
    pc.invTargetResolution[0] = (dims.invFullWidth > 0.0f) ? dims.invFullWidth : (1.0f / fw);
    pc.invTargetResolution[1] = (dims.invFullHeight > 0.0f) ? dims.invFullHeight : (1.0f / fh);
    pc.frameIndex = frameIndex;
    pc.depthTolerance = config.depthTolerance;
    pc.historyWeight = config.historyWeight;
    pc.debugView = config.debugView;
    pc.enableColorClamping = config.enableColorClamping ? 1u : 0u;
    pc.mipLodBias = config.mipLodBias;
    pc.colorSpace = (config.colorSpace == ColorSpace::RGB) ? 1u : ((config.colorSpace == ColorSpace::ICtCp) ? 2u : 0u);
    pc.enableSpatialFallback = config.enableSpatialFallback ? 1u : 0u;
    pc.jitterDelta[0] = jitterDelta.x;
    pc.jitterDelta[1] = jitterDelta.y;
    pc.enableMotionDilation = config.enableMotionDilation ? 1u : 0u;
    pc.jitterCompensation = config.jitterCompensation;
    pc.shiftDirection = (config.jitterDirection < 0) ? -1 : 1;
    const DepthRange range = SanitizeDepthRange(config.depthConvention, config.depthNear, config.depthFar);
    pc.depthMode = (config.depthConvention == DepthConvention::Reversed) ? 1u : 0u;
    pc.depthNear = range.zNear;
    pc.depthFar = range.zFar;
    return pc;
}

void ReconstructionPass::DispatchVulkan(void* vkCommandBuffer, uint32_t frameIndex) {
    if (!m_initialized.load() || !vkCommandBuffer) {
        return;
    }

    if (!m_pipeline || !m_pipelineLayout) {
        static std::atomic<bool> s_warned{ false };
        if (!s_warned.exchange(true)) {
            CBR_LOG_WARN("DispatchVulkan: compute pipeline not loaded; skipping dispatch.");
        }
        return;
    }

    if (!m_descriptorsBound.load()) {
        static std::atomic<bool> s_warnedDescriptors{ false };
        if (!s_warnedDescriptors.exchange(true)) {
            CBR_LOG_WARN("DispatchVulkan deferred: descriptor set not yet bound with active image views.");
        }
        return;
    }

    // Item C-03: Transition all 7 CBR images out of VK_IMAGE_LAYOUT_UNDEFINED before first dispatch
    EnsureInitialLayoutsTransitioned(vkCommandBuffer);

    auto pfnCmdBindPipeline = m_pfnCmdBindPipeline;
    auto pfnCmdBindDescriptorSets = m_pfnCmdBindDescriptorSets;
    auto pfnCmdPushConstants = m_pfnCmdPushConstants;
    auto pfnCmdDispatch = m_pfnCmdDispatch;
    auto pfnCmdPipelineBarrier = m_pfnCmdPipelineBarrier;

    if (!pfnCmdBindPipeline || !pfnCmdPushConstants || !pfnCmdDispatch) {
        void* vulkanModule = GetVulkanLoaderModule();
        if (vulkanModule) {
            if (!pfnCmdBindPipeline) pfnCmdBindPipeline = reinterpret_cast<PFN_CmdBindPipeline>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdBindPipeline"));
            if (!pfnCmdBindDescriptorSets) pfnCmdBindDescriptorSets = reinterpret_cast<PFN_CmdBindDescriptorSets>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdBindDescriptorSets"));
            if (!pfnCmdPushConstants) pfnCmdPushConstants = reinterpret_cast<PFN_CmdPushConstants>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdPushConstants"));
            if (!pfnCmdDispatch) pfnCmdDispatch = reinterpret_cast<PFN_CmdDispatch>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdDispatch"));
            if (!pfnCmdPipelineBarrier) pfnCmdPipelineBarrier = reinterpret_cast<PFN_CmdPipelineBarrier>(GetVulkanLoaderSymbol(vulkanModule, "vkCmdPipelineBarrier"));
        }
    }

    if (!pfnCmdBindPipeline || !pfnCmdPushConstants || !pfnCmdDispatch) {
        return;
    }

    if (m_cameraUboMapped) {
        const CBRCameraUniforms camUniforms = BuildCameraUniforms();
        std::memcpy(m_cameraUboMapped, &camUniforms, sizeof(camUniforms));
    }

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t targetW = dims.fullWidth > 0 ? dims.fullWidth : 3840u;
    const uint32_t targetH = dims.fullHeight > 0 ? dims.fullHeight : 2160u;
    constexpr uint32_t kWorkgroupSizeX = 16u;
    constexpr uint32_t kWorkgroupSizeY = 16u;
    const uint32_t groupCountX = (targetW + kWorkgroupSizeX - 1u) / kWorkgroupSizeX;
    const uint32_t groupCountY = (targetH + kWorkgroupSizeY - 1u) / kWorkgroupSizeY;

    const uint32_t activeSetIdx = RenderTargetManager::Get().GetCurrentHistoryIndex() & 1u;
    const uint32_t writeIdx = 1u - activeSetIdx;
    const uint32_t readIdx  = activeSetIdx;

    if (RenderTargetManager::Get().AreVulkanResourcesCreated()) {
        const auto& res = RenderTargetManager::Get().GetVulkanResources();
        auto transitionPreDispatch = [&](const VulkanTextureResource& tex, uint32_t newLayout, uint32_t dstAccess) {
            if (!tex.image) return;
            // Use tracked layout rather than hardcoded UNDEFINED (0) so Step-1 copied inputs and initial history aren't discarded (Item C-03).
            const uint32_t oldLayout = RenderTargetManager::Get().GetVulkanImageLayout(tex.image);
            uint32_t srcStage = kVkPipelineStageTopOfPipeBit;
            uint32_t srcAccess = 0;
            StageAccessForLayout(oldLayout, tex.aspectMask, srcStage, srcAccess);
            if (oldLayout == kVkImageLayoutShaderReadOnlyOptimal) {
                srcAccess = 0; // Read-only source requires no write cache flush
            } else if (oldLayout == kVkImageLayoutGeneral) {
                srcAccess = kVkAccessShaderWriteBit;
            }
            TransitionImageLayout(vkCommandBuffer, tex.image,
                                  oldLayout, newLayout,
                                  srcStage ? srcStage : kVkPipelineStageTopOfPipeBit,
                                  kVkPipelineStageComputeShaderBit,
                                  srcAccess, dstAccess,
                                  tex.aspectMask);
        };

        transitionPreDispatch(res.quarterColor,           kVkImageLayoutShaderReadOnlyOptimal, kVkAccessShaderReadBit);
        transitionPreDispatch(res.quarterDepth,           kVkImageLayoutShaderReadOnlyOptimal, kVkAccessShaderReadBit);
        transitionPreDispatch(res.velocity,               kVkImageLayoutShaderReadOnlyOptimal, kVkAccessShaderReadBit);
        transitionPreDispatch(res.historyColor[readIdx],  kVkImageLayoutShaderReadOnlyOptimal, kVkAccessShaderReadBit);
        transitionPreDispatch(res.historyDepth[readIdx],  kVkImageLayoutShaderReadOnlyOptimal, kVkAccessShaderReadBit);
        transitionPreDispatch(res.historyColor[writeIdx], kVkImageLayoutGeneral,              kVkAccessShaderWriteBit);
        transitionPreDispatch(res.historyDepth[writeIdx], kVkImageLayoutGeneral,              kVkAccessShaderWriteBit);
    }

    pfnCmdBindPipeline(vkCommandBuffer, kVkPipelineBindPointCompute, m_pipeline);

    void* activeSet = m_descriptorSets[activeSetIdx] ? m_descriptorSets[activeSetIdx] : m_descriptorSets[0];

    if (activeSet && pfnCmdBindDescriptorSets) {
        const void* const descriptorSets[] = { activeSet };
        pfnCmdBindDescriptorSets(vkCommandBuffer, kVkPipelineBindPointCompute, m_pipelineLayout, 0, 1, descriptorSets, 0, nullptr);
    }

    pfnCmdPushConstants(vkCommandBuffer, m_pipelineLayout, kVkShaderStageComputeBit, 0, sizeof(pushConstants), &pushConstants);

    // 2-Timestamp VkQueryPool profiling around vkCmdDispatch + high-res CPU fallback (Item M-05)
    void* vulkanTimerMod = GetVulkanLoaderModule();
    auto resolveTimerFunc = [&](const char* name) -> void* {
        if (!vulkanTimerMod) return nullptr;
        auto pfnGetDev = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetVulkanLoaderSymbol(vulkanTimerMod, "vkGetDeviceProcAddr"));
        if (pfnGetDev && m_device) {
            void* p = pfnGetDev(m_device, name);
            if (p) return p;
        }
        return GetVulkanLoaderSymbol(vulkanTimerMod, name);
    };
    auto pfnCmdResetQueryPool = reinterpret_cast<PFN_vkCmdResetQueryPool>(resolveTimerFunc("vkCmdResetQueryPool"));
    auto pfnCmdWriteTimestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(resolveTimerFunc("vkCmdWriteTimestamp"));
    auto pfnGetQueryPoolResults = reinterpret_cast<PFN_vkGetQueryPoolResults>(resolveTimerFunc("vkGetQueryPoolResults"));

    const uint32_t qpIdx = frameIndex & 1u;
    void* activeQueryPool = m_vkQueryPools[qpIdx];
    bool gpuTimestampReady = false;
    if (activeQueryPool && m_vkQueryRecorded[qpIdx] && pfnGetQueryPoolResults && m_vkTimestampPeriodNs > 0.0f) {
        uint64_t timestamps[2]{ 0, 0 };
        const int qRes = pfnGetQueryPoolResults(m_device, activeQueryPool, 0, 2,
                                                sizeof(timestamps), timestamps, sizeof(uint64_t),
                                                kVkQueryResult64Bit);
        if (qRes == 0 && timestamps[1] > timestamps[0]) {
            const double gpuNs = static_cast<double>(timestamps[1] - timestamps[0]) * static_cast<double>(m_vkTimestampPeriodNs);
            m_lastDispatchDurationMs.store(static_cast<float>(gpuNs * 1e-6), std::memory_order_release);
            gpuTimestampReady = true;
        }
    }

    const bool canRecordGpuTs = activeQueryPool && pfnCmdResetQueryPool && pfnCmdWriteTimestamp;
    if (canRecordGpuTs) {
        pfnCmdResetQueryPool(vkCommandBuffer, activeQueryPool, 0, 2);
        pfnCmdWriteTimestamp(vkCommandBuffer, kVkPipelineStageComputeShaderBit, activeQueryPool, 0);
    }

    const auto cpuStart = std::chrono::steady_clock::now();
    pfnCmdDispatch(vkCommandBuffer, groupCountX, groupCountY, 1);
    const auto cpuEnd = std::chrono::steady_clock::now();

    if (canRecordGpuTs) {
        pfnCmdWriteTimestamp(vkCommandBuffer, kVkPipelineStageComputeShaderBit, activeQueryPool, 1);
        m_vkQueryRecorded[qpIdx] = true;
    }

    if (!gpuTimestampReady) {
        const float cpuMs = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
        m_lastDispatchDurationMs.store(cpuMs > 0.001f ? cpuMs : 0.001f, std::memory_order_release);
    }

    if (pfnCmdPipelineBarrier) {
        if (RenderTargetManager::Get().AreVulkanResourcesCreated()) {
            const auto& res = RenderTargetManager::Get().GetVulkanResources();

            // Transition written history targets from GENERAL -> SHADER_READ_ONLY_OPTIMAL so Step 3 blit and next frame's temporal read consume them zero-copy (Item C-02/C-03).
            TransitionImageLayout(vkCommandBuffer, res.historyColor[writeIdx].image,
                                  kVkImageLayoutGeneral, kVkImageLayoutShaderReadOnlyOptimal,
                                  kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit | kVkPipelineStageTransferBit,
                                  kVkAccessShaderWriteBit, kVkAccessShaderReadBit | kVkAccessTransferReadBit,
                                  res.historyColor[writeIdx].aspectMask);
            TransitionImageLayout(vkCommandBuffer, res.historyDepth[writeIdx].image,
                                  kVkImageLayoutGeneral, kVkImageLayoutShaderReadOnlyOptimal,
                                  kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                                  kVkAccessShaderWriteBit, kVkAccessShaderReadBit,
                                  res.historyDepth[writeIdx].aspectMask);

            // Transition previous frame's read history targets (both color AND depth) back to GENERAL for next frame's write (Item C-02/C-03).
            // Write-After-Read needs an execution dependency (srcAccessMask = 0) before the next frame's shader writes.
            TransitionImageLayout(vkCommandBuffer, res.historyColor[readIdx].image,
                                  kVkImageLayoutShaderReadOnlyOptimal, kVkImageLayoutGeneral,
                                  kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                                  0, kVkAccessShaderWriteBit,
                                  res.historyColor[readIdx].aspectMask);
            TransitionImageLayout(vkCommandBuffer, res.historyDepth[readIdx].image,
                                  kVkImageLayoutShaderReadOnlyOptimal, kVkImageLayoutGeneral,
                                  kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                                  0, kVkAccessShaderWriteBit,
                                  res.historyDepth[readIdx].aspectMask);
        } else {
            MinimalVkMemoryBarrier barrier{};
            barrier.sType = kVkStructureTypeMemoryBarrier;
            barrier.srcAccessMask = kVkAccessShaderWriteBit;
            barrier.dstAccessMask = kVkAccessShaderReadBit | kVkAccessMemoryReadBit;
            pfnCmdPipelineBarrier(vkCommandBuffer,
                                  kVkPipelineStageComputeShaderBit,
                                  kVkPipelineStageAllCommandsBit,
                                  0, 1, &barrier, 0, nullptr, 0, nullptr);
        }
    }
}

void ReconstructionPass::DispatchDX12(void* d3d12GraphicsCommandList, uint32_t frameIndex) {
    if (!m_initialized.load() || !d3d12GraphicsCommandList) {
        static std::atomic<bool> s_warned{ false };
        if (!s_warned.exchange(true)) CBR_LOG_WARN("DispatchDX12 dropped: ReconstructionPass not initialized or null command list.");
        return;
    }

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t targetW = dims.fullWidth > 0 ? dims.fullWidth : 3840u;
    const uint32_t targetH = dims.fullHeight > 0 ? dims.fullHeight : 2160u;
    constexpr uint32_t kWorkgroupSizeX = 16u;
    constexpr uint32_t kWorkgroupSizeY = 16u;
    const uint32_t groupCountX = (targetW + kWorkgroupSizeX - 1u) / kWorkgroupSizeX;
    const uint32_t groupCountY = (targetH + kWorkgroupSizeY - 1u) / kWorkgroupSizeY;

#if defined(_WIN32)
    // ID3D12GraphicsCommandList VMT call dispatch with resource barriers & descriptor tables (Item H-02 / M-05)
    if (m_d3d12RootSignature && m_d3d12PipelineState && RenderTargetManager::Get().AreDX12ResourcesCreated()) {
        typedef void (__stdcall *PFN_Dispatch)(void*, uint32_t, uint32_t, uint32_t);
        typedef void (__stdcall *PFN_SetPipelineState)(void*, void*);
        typedef void (__stdcall *PFN_ResourceBarrier)(void*, uint32_t, const MinimalD3D12ResourceBarrier*);
        typedef void (__stdcall *PFN_SetDescriptorHeaps)(void*, uint32_t, void* const*);
        typedef void (__stdcall *PFN_SetComputeRootSignature)(void*, void*);
        typedef void (__stdcall *PFN_SetComputeRootDescriptorTable)(void*, uint32_t, uint64_t);
        typedef void (__stdcall *PFN_SetComputeRoot32BitConstants)(void*, uint32_t, uint32_t, const void*, uint32_t);
        typedef void (__stdcall *PFN_SetComputeRootConstantBufferView)(void*, uint32_t, uint64_t);

        void** vmt = *reinterpret_cast<void***>(d3d12GraphicsCommandList);
        if (vmt && vmt[14] && vmt[25] && vmt[26] && vmt[28] && vmt[29] && vmt[31] && vmt[34]) {
            auto pfnDispatch       = reinterpret_cast<PFN_Dispatch>(vmt[14]);
            auto pfnSetPSO         = reinterpret_cast<PFN_SetPipelineState>(vmt[25]);
            auto pfnBarrier        = reinterpret_cast<PFN_ResourceBarrier>(vmt[26]);
            auto pfnSetHeaps       = reinterpret_cast<PFN_SetDescriptorHeaps>(vmt[28]);
            auto pfnSetRootSig     = reinterpret_cast<PFN_SetComputeRootSignature>(vmt[29]);
            auto pfnSetRootTable   = reinterpret_cast<PFN_SetComputeRootDescriptorTable>(vmt[31]);
            auto pfnSetConstants   = reinterpret_cast<PFN_SetComputeRoot32BitConstants>(vmt[34]);

            auto& rtm = RenderTargetManager::Get();
            const auto& dx12 = rtm.GetDX12Resources();
            const uint32_t activeSetIdx = rtm.GetCurrentHistoryIndex() & 1u;
            const uint32_t readIdx = activeSetIdx;
            const uint32_t writeIdx = 1u - activeSetIdx;

            MinimalD3D12ResourceBarrier preBarriers[7]{};
            uint32_t preCount = 0;
            auto appendBarrier = [&](void* res, uint32_t targetState, MinimalD3D12ResourceBarrier* outArr, uint32_t& outCount) {
                if (!res) return;
                const uint32_t oldState = rtm.GetDX12ResourceState(res);
                if (oldState == targetState) return;
                MinimalD3D12ResourceBarrier& b = outArr[outCount++];
                b.Type = 0; // D3D12_RESOURCE_BARRIER_TYPE_TRANSITION
                b.Flags = 0;
                b.Transition.pResource = res;
                b.Transition.Subresource = kD3D12ResourceBarrierAllSubresources;
                b.Transition.StateBefore = oldState;
                b.Transition.StateAfter = targetState;
                rtm.SetDX12ResourceState(res, targetState);
            };

            appendBarrier(dx12.quarterColor.resource,          kD3D12ResourceStateNonPixelSRV, preBarriers, preCount);
            appendBarrier(dx12.quarterDepth.resource,          kD3D12ResourceStateNonPixelSRV, preBarriers, preCount);
            appendBarrier(dx12.velocity.resource,              kD3D12ResourceStateNonPixelSRV, preBarriers, preCount);
            appendBarrier(dx12.historyColor[readIdx].resource, kD3D12ResourceStateNonPixelSRV, preBarriers, preCount);
            appendBarrier(dx12.historyDepth[readIdx].resource, kD3D12ResourceStateNonPixelSRV, preBarriers, preCount);
            appendBarrier(dx12.historyColor[writeIdx].resource,kD3D12ResourceStateUAV,         preBarriers, preCount);
            appendBarrier(dx12.historyDepth[writeIdx].resource,kD3D12ResourceStateUAV,         preBarriers, preCount);

            if (preCount > 0) {
                pfnBarrier(d3d12GraphicsCommandList, preCount, preBarriers);
            }

            void* heaps[2] = { dx12.srvUavHeap, dx12.samplerHeap };
            if (heaps[0] && heaps[1]) {
                pfnSetHeaps(d3d12GraphicsCommandList, 2, heaps);
            }
            pfnSetRootSig(d3d12GraphicsCommandList, m_d3d12RootSignature);
            pfnSetPSO(d3d12GraphicsCommandList, m_d3d12PipelineState);
            if (m_d3d12CameraCbvMapped) {
                const CBRCameraUniforms camUniforms = BuildCameraUniforms();
                std::memcpy(m_d3d12CameraCbvMapped, &camUniforms, sizeof(camUniforms));
            }
            pfnSetConstants(d3d12GraphicsCommandList, 0, static_cast<uint32_t>(sizeof(pushConstants) / 4u), &pushConstants, 0);
            pfnSetRootTable(d3d12GraphicsCommandList, 1, dx12.srvGpuHandle[activeSetIdx]);
            pfnSetRootTable(d3d12GraphicsCommandList, 2, dx12.uavGpuHandle[activeSetIdx]);
            pfnSetRootTable(d3d12GraphicsCommandList, 3, dx12.samplerGpuHandle);
            if (m_d3d12CameraCbvGpuVa && vmt[37]) {
                auto pfnSetRootCBV = reinterpret_cast<PFN_SetComputeRootConstantBufferView>(vmt[37]);
                pfnSetRootCBV(d3d12GraphicsCommandList, 4, m_d3d12CameraCbvGpuVa);
            }

            const auto cpuStart = std::chrono::steady_clock::now();
            pfnDispatch(d3d12GraphicsCommandList, groupCountX, groupCountY, 1);
            const auto cpuEnd = std::chrono::steady_clock::now();
            const float cpuMs = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
            m_lastDispatchDurationMs.store(cpuMs > 0.001f ? cpuMs : 0.001f, std::memory_order_release);

            MinimalD3D12ResourceBarrier postBarriers[4]{};
            uint32_t postCount = 0;
            appendBarrier(dx12.historyColor[writeIdx].resource, kD3D12ResourceStateNonPixelSRV, postBarriers, postCount);
            appendBarrier(dx12.historyDepth[writeIdx].resource, kD3D12ResourceStateNonPixelSRV, postBarriers, postCount);
            appendBarrier(dx12.historyColor[readIdx].resource,  kD3D12ResourceStateUAV,         postBarriers, postCount);
            appendBarrier(dx12.historyDepth[readIdx].resource,  kD3D12ResourceStateUAV,         postBarriers, postCount);
            if (postCount > 0) {
                pfnBarrier(d3d12GraphicsCommandList, postCount, postBarriers);
            }
        }
    }
#else
    (void)pushConstants;
    (void)groupCountX;
    (void)groupCountY;
#endif
}

} // namespace cbr
