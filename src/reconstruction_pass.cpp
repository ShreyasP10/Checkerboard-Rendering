#include "cbr/reconstruction_pass.h"
#include <atomic>
#include <cstring>
#include <fstream>
#include <vector>
#include "cbr/cbr_engine.h"
#include "cbr/config.h"
#include "cbr/depth_convention.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

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

constexpr uint32_t kVkPipelineStageComputeShaderBit = 0x00000800;
constexpr uint32_t kVkPipelineStageTransferBit = 0x00001000;
constexpr uint32_t kVkPipelineStageBottomOfPipeBit = 0x00002000;
constexpr uint32_t kVkPipelineStageAllCommandsBit = 0x00010000;
constexpr uint32_t kVkAccessShaderWriteBit = 0x00000040;
constexpr uint32_t kVkAccessShaderReadBit = 0x00000020;
constexpr uint32_t kVkAccessTransferReadBit = 0x00000800;
constexpr uint32_t kVkAccessTransferWriteBit = 0x00001000;
constexpr uint32_t kVkAccessMemoryReadBit = 0x00008000;

constexpr uint32_t kVkShaderStageComputeBit = 0x00000020;
constexpr uint32_t kVkPipelineBindPointCompute = 1;

constexpr uint32_t kVkDescriptorTypeCombinedImageSampler = 1;
constexpr uint32_t kVkDescriptorTypeStorageImage = 3;

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

} // namespace
#endif

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

bool ReconstructionPass::CreateVulkanPipeline() {
#if defined(_WIN32)
    if (!m_device) {
        return false;
    }

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) {
        CBR_LOG_WARN("CreateVulkanPipeline: vulkan-1.dll not loaded.");
        return false;
    }

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
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
    const auto modDir = CBREngine::Get().GetModuleDirectory();
    if (!modDir.empty()) {
        searchPaths.push_back(modDir / "shaders" / "cbr_reconstruct.spv");
        searchPaths.push_back(modDir / "cbr_reconstruct.spv");
    }
    searchPaths.push_back("shaders/cbr_reconstruct.spv");
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

    // 7 bindings matching cbr_reconstruct.comp (set = 0):
    // Bindings 0..4: combined image samplers
    // Bindings 5..6: storage images (OutputImage, OutputDepth)
    MinimalVkDescriptorSetLayoutBinding dsBindings[7]{};
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

    MinimalVkDescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.sType = kVkStructureTypeDescriptorSetLayoutCreateInfo;
    dsLayoutInfo.bindingCount = 7;
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
        MinimalVkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = kVkDescriptorTypeCombinedImageSampler;
        poolSizes[0].descriptorCount = 10; // 5 samplers * 2 sets
        poolSizes[1].type = kVkDescriptorTypeStorageImage;
        poolSizes[1].descriptorCount = 4;  // 2 storage images * 2 sets

        MinimalVkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = kVkStructureTypeDescriptorPoolCreateInfo;
        poolInfo.maxSets = 2; // Separate descriptor sets for ping-pong history buffers
        poolInfo.poolSizeCount = 2;
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
    m_pfnCmdBindPipeline = reinterpret_cast<PFN_CmdBindPipeline>(resolveDeviceFunc("vkCmdBindPipeline"));
    m_pfnCmdBindDescriptorSets = reinterpret_cast<PFN_CmdBindDescriptorSets>(resolveDeviceFunc("vkCmdBindDescriptorSets"));
    m_pfnCmdPushConstants = reinterpret_cast<PFN_CmdPushConstants>(resolveDeviceFunc("vkCmdPushConstants"));
    m_pfnCmdDispatch = reinterpret_cast<PFN_CmdDispatch>(resolveDeviceFunc("vkCmdDispatch"));

    CBR_LOG_INFO("Vulkan CBR reconstruction compute pipeline created successfully! (Pipeline: %p, Layout: %p, Descriptors: %p, Pool: %p, Sets: %p / %p)",
        m_pipeline, m_pipelineLayout, m_descriptorSetLayout, m_descriptorPool, m_descriptorSets[0], m_descriptorSets[1]);
    return true;
#else
    return true;
#endif
}

void ReconstructionPass::DestroyVulkanPipeline() {
#if defined(_WIN32)
    if (!m_device) return;
    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) return;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
    };

    auto pfnDeviceWaitIdle = reinterpret_cast<int (*)(void*)>(resolveDeviceFunc("vkDeviceWaitIdle"));
    if (m_device && pfnDeviceWaitIdle) {
        pfnDeviceWaitIdle(m_device);
    }

    auto pfnDestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(resolveDeviceFunc("vkDestroyPipeline"));
    auto pfnDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(resolveDeviceFunc("vkDestroyPipelineLayout"));
    auto pfnDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(resolveDeviceFunc("vkDestroyDescriptorSetLayout"));
    auto pfnDestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(resolveDeviceFunc("vkDestroyDescriptorPool"));

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
    m_pfnCmdBindPipeline = nullptr;
    m_pfnCmdBindDescriptorSets = nullptr;
    m_pfnCmdPushConstants = nullptr;
    m_pfnCmdDispatch = nullptr;
    m_descriptorsBound.store(false);
#endif
}

bool ReconstructionPass::InitializeVulkan(void* vkDevice, void* /*vkPhysicalDevice*/) {
    m_isVulkan.store(true);
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

bool ReconstructionPass::InitializeDX12(void* d3d12Device) {
    m_isVulkan.store(false);
    m_d3d12Device = d3d12Device;
    m_initialized.store(true);
    CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline (device: %p).", d3d12Device);
    return true;
}

void ReconstructionPass::Shutdown() {
    if (m_isVulkan.load()) {
        DestroyVulkanPipeline();
    }
    m_device = nullptr;
    m_pipeline = nullptr;
    m_pipelineLayout = nullptr;
    m_descriptorSetLayout = nullptr;
    m_descriptorPool = nullptr;
    m_descriptorSets[0] = nullptr;
    m_descriptorSets[1] = nullptr;
    m_d3d12Device = nullptr;
    m_d3d12RootSignature = nullptr;
    m_d3d12PipelineState = nullptr;
    m_pfnCmdCopyImage = nullptr;
    m_descriptorsBound.store(false);
    m_initialized.store(false);
    CBR_LOG_INFO("ReconstructionPass shut down cleanly.");
}

bool ReconstructionPass::UpdateVulkanDescriptors(void* quarterColorView, void* quarterDepthView,
                                                void* historyColorView, void* historyDepthView,
                                                void* velocityView,
                                                void* outputImageView, void* outputDepthView,
                                                void* linearSampler, void* pointSampler) {
#if defined(_WIN32)
    void* targetSet = m_descriptorSets[0] ? m_descriptorSets[0] : nullptr;
    if (!m_device || !targetSet) return false;

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) return false;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
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

    MinimalVkWriteDescriptorSet writes[7]{};
    for (uint32_t i = 0; i < 7; ++i) {
        writes[i].sType = kVkStructureTypeWriteDescriptorSet;
        writes[i].dstSet = targetSet;
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i < 5) ? kVkDescriptorTypeCombinedImageSampler : kVkDescriptorTypeStorageImage;
        writes[i].pImageInfo = &imageInfos[i];
    }

    pfnUpdateDescriptorSets(m_device, 7, writes, 0, nullptr);
    m_descriptorsBound.store(true);
    CBR_LOG_INFO("ReconstructionPass: Vulkan descriptors successfully updated with active image views.");
    return true;
#else
    (void)quarterColorView; (void)quarterDepthView; (void)historyColorView;
    (void)historyDepthView; (void)velocityView; (void)outputImageView;
    (void)outputDepthView; (void)linearSampler; (void)pointSampler;
    return true;
#endif
}

bool ReconstructionPass::UpdateVulkanPingPongDescriptors(void* quarterColorView, void* quarterDepthView,
                                                        void* historyColorViewA, void* historyDepthViewA,
                                                        void* historyColorViewB, void* historyDepthViewB,
                                                        void* velocityView,
                                                        void* outputImageView, void* outputDepthView,
                                                        void* linearSampler, void* pointSampler) {
#if defined(_WIN32)
    if (!m_device || !m_descriptorSets[0] || !m_descriptorSets[1]) return false;

    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) return false;

    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule, "vkGetDeviceProcAddr"));
    auto resolveDeviceFunc = [&](const char* name) -> void* {
        if (pfnGetDeviceProcAddr && m_device) {
            void* p = pfnGetDeviceProcAddr(m_device, name);
            if (p) return p;
        }
        return reinterpret_cast<void*>(GetProcAddress(vulkanModule, name));
    };

    auto pfnUpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(resolveDeviceFunc("vkUpdateDescriptorSets"));
    if (!pfnUpdateDescriptorSets) return false;

    // Set 0: Reads History A, writes to History B (Item C-02: ping-pong feedback loop)
    MinimalVkDescriptorImageInfo imageInfos0[7]{};
    imageInfos0[0] = { pointSampler, quarterColorView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[1] = { pointSampler, quarterDepthView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[2] = { linearSampler, historyColorViewA, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[3] = { pointSampler, historyDepthViewA, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[4] = { pointSampler, velocityView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos0[5] = { nullptr, historyColorViewB, kVkImageLayoutGeneral };
    imageInfos0[6] = { nullptr, historyDepthViewB, kVkImageLayoutGeneral };

    // Set 1: Reads History B, writes to History A (Item C-02: ping-pong feedback loop)
    MinimalVkDescriptorImageInfo imageInfos1[7]{};
    imageInfos1[0] = { pointSampler, quarterColorView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[1] = { pointSampler, quarterDepthView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[2] = { linearSampler, historyColorViewB, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[3] = { pointSampler, historyDepthViewB, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[4] = { pointSampler, velocityView, kVkImageLayoutShaderReadOnlyOptimal };
    imageInfos1[5] = { nullptr, historyColorViewA, kVkImageLayoutGeneral };
    imageInfos1[6] = { nullptr, historyDepthViewA, kVkImageLayoutGeneral };

    MinimalVkWriteDescriptorSet writes[14]{};
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

    pfnUpdateDescriptorSets(m_device, 14, writes, 0, nullptr);
    m_descriptorsBound.store(true);
    CBR_LOG_INFO("ReconstructionPass: Dual ping-pong Vulkan descriptor sets (Set 0 & 1) updated.");
    return true;
#else
    (void)quarterColorView; (void)quarterDepthView; (void)historyColorViewA;
    (void)historyDepthViewA; (void)historyColorViewB; (void)historyDepthViewB;
    (void)velocityView; (void)outputImageView; (void)outputDepthView;
    (void)linearSampler; (void)pointSampler;
    return true;
#endif
}

void ReconstructionPass::TransitionImageLayout(void* vkCommandBuffer, void* vkImage,
                                              uint32_t oldLayout, uint32_t newLayout,
                                              uint32_t srcStage, uint32_t dstStage,
                                              uint32_t srcAccess, uint32_t dstAccess,
                                              uint32_t aspectMask) {
#if defined(_WIN32)
    if (!vkCommandBuffer || !vkImage) return;

    auto pfnCmdPipelineBarrier = ReconstructionPass::Get().m_pfnCmdPipelineBarrier;
    if (!pfnCmdPipelineBarrier) {
        HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
        if (vulkanModule) {
            pfnCmdPipelineBarrier = reinterpret_cast<PFN_CmdPipelineBarrier>(GetProcAddress(vulkanModule, "vkCmdPipelineBarrier"));
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
#else
    (void)vkCommandBuffer; (void)vkImage; (void)oldLayout; (void)newLayout;
    (void)srcStage; (void)dstStage; (void)srcAccess; (void)dstAccess; (void)aspectMask;
#endif
}

void ReconstructionPass::BlitToImage(void* vkCommandBuffer, void* srcImage, void* dstImage,
                                    uint32_t srcWidth, uint32_t srcHeight,
                                    uint32_t dstWidth, uint32_t dstHeight,
                                    uint32_t targetDstLayout) {
#if defined(_WIN32)
    if (!vkCommandBuffer || !srcImage || !dstImage) return;

    auto pfnCmdBlitImage = ReconstructionPass::Get().m_pfnCmdBlitImage;
    if (!pfnCmdBlitImage) {
        HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
        if (vulkanModule) {
            pfnCmdBlitImage = reinterpret_cast<PFN_CmdBlitImage>(GetProcAddress(vulkanModule, "vkCmdBlitImage"));
        }
    }
    if (!pfnCmdBlitImage) return;

    // Transition srcImage to TRANSFER_SRC_OPTIMAL
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          kVkImageLayoutGeneral, kVkImageLayoutTransferSrcOptimal,
                          kVkPipelineStageComputeShaderBit, kVkPipelineStageTransferBit,
                          kVkAccessShaderWriteBit, kVkAccessTransferReadBit,
                          kVkImageAspectColorBit);

    // Transition dstImage to TRANSFER_DST_OPTIMAL
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          0 /* UNDEFINED */, kVkImageLayoutTransferDstOptimal,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageTransferBit,
                          0, kVkAccessTransferWriteBit,
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

    // Transition dstImage to targetDstLayout (e.g. PRESENT_SRC_KHR or COLOR_ATTACHMENT_OPTIMAL)
    if (targetDstLayout == kVkImageLayoutColorAttachmentOptimal) {
        TransitionImageLayout(vkCommandBuffer, dstImage,
                              kVkImageLayoutTransferDstOptimal, kVkImageLayoutColorAttachmentOptimal,
                              kVkPipelineStageTransferBit, kVkPipelineStageColorAttachmentOutputBit,
                              kVkAccessTransferWriteBit, kVkAccessColorAttachmentWriteBit | kVkAccessColorAttachmentReadBit,
                              kVkImageAspectColorBit);
    } else {
        TransitionImageLayout(vkCommandBuffer, dstImage,
                              kVkImageLayoutTransferDstOptimal, targetDstLayout,
                              kVkPipelineStageTransferBit, kVkPipelineStageBottomOfPipeBit,
                              kVkAccessTransferWriteBit, 0,
                              kVkImageAspectColorBit);
    }

    // Transition srcImage back to GENERAL
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          kVkImageLayoutTransferSrcOptimal, kVkImageLayoutGeneral,
                          kVkPipelineStageTransferBit, kVkPipelineStageComputeShaderBit,
                          kVkAccessTransferReadBit, kVkAccessShaderWriteBit,
                          kVkImageAspectColorBit);
#else
    (void)vkCommandBuffer; (void)srcImage; (void)dstImage;
    (void)srcWidth; (void)srcHeight; (void)dstWidth; (void)dstHeight;
    (void)targetDstLayout;
#endif
}

void ReconstructionPass::CopyImageRegion(void* vkCommandBuffer, void* srcImage, void* dstImage,
                                         uint32_t srcX, uint32_t srcY,
                                         uint32_t dstX, uint32_t dstY,
                                         uint32_t width, uint32_t height,
                                         uint32_t aspectMask) {
#if defined(_WIN32)
    if (!vkCommandBuffer || !srcImage || !dstImage || width == 0 || height == 0) return;

    auto pfnCmdCopyImage = ReconstructionPass::Get().m_pfnCmdCopyImage;
    if (!pfnCmdCopyImage) {
        HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
        if (vulkanModule) {
            pfnCmdCopyImage = reinterpret_cast<PFN_CmdCopyImage>(GetProcAddress(vulkanModule, "vkCmdCopyImage"));
            ReconstructionPass::Get().m_pfnCmdCopyImage = pfnCmdCopyImage;
        }
    }
    if (!pfnCmdCopyImage) return;

    // Transition srcImage to TRANSFER_SRC_OPTIMAL
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          0 /* UNDEFINED or current */, kVkImageLayoutTransferSrcOptimal,
                          kVkPipelineStageAllCommandsBit, kVkPipelineStageTransferBit,
                          0, kVkAccessTransferReadBit,
                          aspectMask);

    // Transition dstImage to TRANSFER_DST_OPTIMAL
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          0 /* UNDEFINED or current */, kVkImageLayoutTransferDstOptimal,
                          kVkPipelineStageAllCommandsBit, kVkPipelineStageTransferBit,
                          0, kVkAccessTransferWriteBit,
                          aspectMask);

    MinimalVkImageCopy region{};
    region.srcSubresource.aspectMask = aspectMask;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = { static_cast<int32_t>(srcX), static_cast<int32_t>(srcY), 0 };

    region.dstSubresource.aspectMask = aspectMask;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = { static_cast<int32_t>(dstX), static_cast<int32_t>(dstY), 0 };

    region.extent = { width, height, 1 };

    pfnCmdCopyImage(vkCommandBuffer,
                    srcImage, kVkImageLayoutTransferSrcOptimal,
                    dstImage, kVkImageLayoutTransferDstOptimal,
                    1, &region);

    // Transition dstImage to SHADER_READ_ONLY_OPTIMAL for compute sampling
    TransitionImageLayout(vkCommandBuffer, dstImage,
                          kVkImageLayoutTransferDstOptimal, kVkImageLayoutShaderReadOnlyOptimal,
                          kVkPipelineStageTransferBit, kVkPipelineStageComputeShaderBit,
                          kVkAccessTransferWriteBit, kVkAccessShaderReadBit,
                          aspectMask);

    // Transition srcImage back to COLOR_ATTACHMENT_OPTIMAL (or DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    const uint32_t restoreLayout = (aspectMask & 2 /* DEPTH */) ? kVkImageLayoutDepthStencilAttachmentOptimal : kVkImageLayoutColorAttachmentOptimal;
    TransitionImageLayout(vkCommandBuffer, srcImage,
                          kVkImageLayoutTransferSrcOptimal, restoreLayout,
                          kVkPipelineStageTransferBit, kVkPipelineStageColorAttachmentOutputBit,
                          kVkAccessTransferReadBit, kVkAccessColorAttachmentWriteBit | kVkAccessColorAttachmentReadBit,
                          aspectMask);
#else
    (void)vkCommandBuffer; (void)srcImage; (void)dstImage;
    (void)srcX; (void)srcY; (void)dstX; (void)dstY;
    (void)width; (void)height; (void)aspectMask;
#endif
}

void ReconstructionPass::EnsureInitialLayoutsTransitioned(void* vkCommandBuffer) {
#if defined(_WIN32)
    if (!vkCommandBuffer || m_initialLayoutsTransitioned.load(std::memory_order_acquire)) {
        return;
    }
    if (m_initialLayoutsTransitioned.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    if (!RenderTargetManager::Get().AreVulkanResourcesCreated()) return;
    const auto& res = RenderTargetManager::Get().GetVulkanResources();

    // Transition all 7 CBR images from UNDEFINED to initial operational layouts (Item C-03)
    TransitionImageLayout(vkCommandBuffer, res.quarterColor.image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutShaderReadOnlyOptimal,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderReadBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.quarterDepth.image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutShaderReadOnlyOptimal,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderReadBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.historyColor[0].image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutShaderReadOnlyOptimal,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderReadBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.historyColor[1].image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutGeneral,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderWriteBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.historyDepth[0].image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutShaderReadOnlyOptimal,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderReadBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.historyDepth[1].image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutGeneral,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderWriteBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.velocity.image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutShaderReadOnlyOptimal,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderReadBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.outputColor.image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutGeneral,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderWriteBit, kVkImageAspectColorBit);

    TransitionImageLayout(vkCommandBuffer, res.outputDepth.image,
                          0 /* VK_IMAGE_LAYOUT_UNDEFINED */, kVkImageLayoutGeneral,
                          kVkPipelineStageBottomOfPipeBit, kVkPipelineStageComputeShaderBit,
                          0, kVkAccessShaderWriteBit, kVkImageAspectColorBit);

    CBR_LOG_INFO("ReconstructionPass: Initial layout transitions from VK_IMAGE_LAYOUT_UNDEFINED executed.");
#else
    (void)vkCommandBuffer;
#endif
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

#if defined(_WIN32)
    // Item C-03: Transition all 7 CBR images out of VK_IMAGE_LAYOUT_UNDEFINED before first dispatch
    EnsureInitialLayoutsTransitioned(vkCommandBuffer);

    auto pfnCmdBindPipeline = m_pfnCmdBindPipeline;
    auto pfnCmdBindDescriptorSets = m_pfnCmdBindDescriptorSets;
    auto pfnCmdPushConstants = m_pfnCmdPushConstants;
    auto pfnCmdDispatch = m_pfnCmdDispatch;
    auto pfnCmdPipelineBarrier = m_pfnCmdPipelineBarrier;

    if (!pfnCmdBindPipeline || !pfnCmdPushConstants || !pfnCmdDispatch) {
        HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
        if (vulkanModule) {
            if (!pfnCmdBindPipeline) pfnCmdBindPipeline = reinterpret_cast<PFN_CmdBindPipeline>(GetProcAddress(vulkanModule, "vkCmdBindPipeline"));
            if (!pfnCmdBindDescriptorSets) pfnCmdBindDescriptorSets = reinterpret_cast<PFN_CmdBindDescriptorSets>(GetProcAddress(vulkanModule, "vkCmdBindDescriptorSets"));
            if (!pfnCmdPushConstants) pfnCmdPushConstants = reinterpret_cast<PFN_CmdPushConstants>(GetProcAddress(vulkanModule, "vkCmdPushConstants"));
            if (!pfnCmdDispatch) pfnCmdDispatch = reinterpret_cast<PFN_CmdDispatch>(GetProcAddress(vulkanModule, "vkCmdDispatch"));
            if (!pfnCmdPipelineBarrier) pfnCmdPipelineBarrier = reinterpret_cast<PFN_CmdPipelineBarrier>(GetProcAddress(vulkanModule, "vkCmdPipelineBarrier"));
        }
    }

    if (!pfnCmdBindPipeline || !pfnCmdPushConstants || !pfnCmdDispatch) {
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

    const uint32_t activeSetIdx = RenderTargetManager::Get().GetCurrentHistoryIndex() & 1u;
    const uint32_t writeIdx = 1u - activeSetIdx;
    const uint32_t readIdx  = activeSetIdx;

    if (RenderTargetManager::Get().AreVulkanResourcesCreated()) {
        const auto& res = RenderTargetManager::Get().GetVulkanResources();

        // Transition current frame inputs to SHADER_READ_ONLY_OPTIMAL (Item C-03)
        TransitionImageLayout(vkCommandBuffer, res.quarterColor.image, 0, kVkImageLayoutShaderReadOnlyOptimal,
                              kVkPipelineStageAllCommandsBit, kVkPipelineStageComputeShaderBit,
                              0, kVkAccessShaderReadBit, kVkImageAspectColorBit);
        TransitionImageLayout(vkCommandBuffer, res.quarterDepth.image, 0, kVkImageLayoutShaderReadOnlyOptimal,
                              kVkPipelineStageAllCommandsBit, kVkPipelineStageComputeShaderBit,
                              0, kVkAccessShaderReadBit, kVkImageAspectColorBit);
        TransitionImageLayout(vkCommandBuffer, res.velocity.image, 0, kVkImageLayoutShaderReadOnlyOptimal,
                              kVkPipelineStageAllCommandsBit, kVkPipelineStageComputeShaderBit,
                              0, kVkAccessShaderReadBit, kVkImageAspectColorBit);

        // Transition read history to SHADER_READ_ONLY_OPTIMAL (or clear on frameIndex == 0)
        TransitionImageLayout(vkCommandBuffer, res.historyColor[readIdx].image,
                              (frameIndex == 0) ? 0 : kVkImageLayoutGeneral, kVkImageLayoutShaderReadOnlyOptimal,
                              kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                              kVkAccessShaderWriteBit, kVkAccessShaderReadBit, kVkImageAspectColorBit);
        TransitionImageLayout(vkCommandBuffer, res.historyDepth[readIdx].image,
                              (frameIndex == 0) ? 0 : kVkImageLayoutGeneral, kVkImageLayoutShaderReadOnlyOptimal,
                              kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                              kVkAccessShaderWriteBit, kVkAccessShaderReadBit, kVkImageAspectColorBit);

        // Transition write history / output to GENERAL
        TransitionImageLayout(vkCommandBuffer, res.historyColor[writeIdx].image,
                              0, kVkImageLayoutGeneral,
                              kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                              kVkAccessShaderReadBit, kVkAccessShaderWriteBit, kVkImageAspectColorBit);
        TransitionImageLayout(vkCommandBuffer, res.historyDepth[writeIdx].image,
                              0, kVkImageLayoutGeneral,
                              kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                              kVkAccessShaderReadBit, kVkAccessShaderWriteBit, kVkImageAspectColorBit);
    }

    pfnCmdBindPipeline(vkCommandBuffer, kVkPipelineBindPointCompute, m_pipeline);

    void* activeSet = m_descriptorSets[activeSetIdx] ? m_descriptorSets[activeSetIdx] : m_descriptorSets[0];

    if (activeSet && pfnCmdBindDescriptorSets) {
        const void* const descriptorSets[] = { activeSet };
        pfnCmdBindDescriptorSets(vkCommandBuffer, kVkPipelineBindPointCompute, m_pipelineLayout, 0, 1, descriptorSets, 0, nullptr);
    }

    pfnCmdPushConstants(vkCommandBuffer, m_pipelineLayout, kVkShaderStageComputeBit, 0, sizeof(pushConstants), &pushConstants);
    pfnCmdDispatch(vkCommandBuffer, groupCountX, groupCountY, 1);

    if (pfnCmdPipelineBarrier) {
        if (RenderTargetManager::Get().AreVulkanResourcesCreated()) {
            const auto& res = RenderTargetManager::Get().GetVulkanResources();
            MinimalVkImageMemoryBarrier barriers[2]{};

            barriers[0].sType = kVkStructureTypeImageMemoryBarrier;
            barriers[0].srcAccessMask = kVkAccessShaderWriteBit;
            barriers[0].dstAccessMask = kVkAccessTransferReadBit | kVkAccessShaderReadBit;
            barriers[0].oldLayout = kVkImageLayoutGeneral;
            barriers[0].newLayout = kVkImageLayoutGeneral;
            barriers[0].srcQueueFamilyIndex = ~0u;
            barriers[0].dstQueueFamilyIndex = ~0u;
            barriers[0].image = res.historyColor[writeIdx].image;
            barriers[0].subresourceRange.aspectMask = kVkImageAspectColorBit;
            barriers[0].subresourceRange.baseMipLevel = 0;
            barriers[0].subresourceRange.levelCount = 1;
            barriers[0].subresourceRange.baseArrayLayer = 0;
            barriers[0].subresourceRange.layerCount = 1;

            barriers[1].sType = kVkStructureTypeImageMemoryBarrier;
            barriers[1].srcAccessMask = kVkAccessShaderWriteBit;
            barriers[1].dstAccessMask = kVkAccessShaderReadBit;
            barriers[1].oldLayout = kVkImageLayoutGeneral;
            barriers[1].newLayout = kVkImageLayoutGeneral;
            barriers[1].srcQueueFamilyIndex = ~0u;
            barriers[1].dstQueueFamilyIndex = ~0u;
            barriers[1].image = res.historyDepth[writeIdx].image;
            barriers[1].subresourceRange.aspectMask = kVkImageAspectColorBit;
            barriers[1].subresourceRange.baseMipLevel = 0;
            barriers[1].subresourceRange.levelCount = 1;
            barriers[1].subresourceRange.baseArrayLayer = 0;
            barriers[1].subresourceRange.layerCount = 1;

            pfnCmdPipelineBarrier(vkCommandBuffer,
                                  kVkPipelineStageComputeShaderBit,
                                  kVkPipelineStageTransferBit | kVkPipelineStageComputeShaderBit,
                                  0, 0, nullptr, 0, nullptr, 2, barriers);

            // Copy written history buffer to outputColor image (Item C-02)
            CopyImageRegion(vkCommandBuffer,
                            res.historyColor[writeIdx].image, res.outputColor.image,
                            0, 0, 0, 0,
                            targetW, targetH,
                            kVkImageAspectColorBit);

            // Ping-pong layout transitions for next frame:
            // Written historyColor[writeIdx] transitions to SHADER_READ_ONLY_OPTIMAL for next frame's read
            TransitionImageLayout(vkCommandBuffer, res.historyColor[writeIdx].image,
                                  kVkImageLayoutGeneral, kVkImageLayoutShaderReadOnlyOptimal,
                                  kVkPipelineStageTransferBit | kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                                  kVkAccessTransferReadBit | kVkAccessShaderWriteBit, kVkAccessShaderReadBit,
                                  kVkImageAspectColorBit);

            // Previously read historyColor[activeSetIdx] transitions to GENERAL for next frame's write
            TransitionImageLayout(vkCommandBuffer, res.historyColor[activeSetIdx].image,
                                  kVkImageLayoutShaderReadOnlyOptimal, kVkImageLayoutGeneral,
                                  kVkPipelineStageComputeShaderBit, kVkPipelineStageComputeShaderBit,
                                  kVkAccessShaderReadBit, kVkAccessShaderWriteBit,
                                  kVkImageAspectColorBit);
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
#else
    (void)frameIndex;
#endif
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
    // ID3D12GraphicsCommandList VMT call dispatch (Item H-02)
    if (m_d3d12RootSignature && m_d3d12PipelineState) {
        typedef void (__stdcall *PFN_SetComputeRootSignature)(void*, void*);
        typedef void (__stdcall *PFN_SetPipelineState)(void*, void*);
        typedef void (__stdcall *PFN_SetComputeRoot32BitConstants)(void*, unsigned int, unsigned int, const void*, unsigned int);
        typedef void (__stdcall *PFN_Dispatch)(void*, unsigned int, unsigned int, unsigned int);

        void** vmt = *reinterpret_cast<void***>(d3d12GraphicsCommandList);
        if (vmt) {
            auto pfnSetPipelineState = reinterpret_cast<PFN_SetPipelineState>(vmt[25]);
            auto pfnSetComputeRootSignature = reinterpret_cast<PFN_SetComputeRootSignature>(vmt[30]);
            auto pfnSetConstants = reinterpret_cast<PFN_SetComputeRoot32BitConstants>(vmt[33]);
            auto pfnDispatch = reinterpret_cast<PFN_Dispatch>(vmt[29]);

            if (pfnSetPipelineState && pfnSetComputeRootSignature && pfnSetConstants && pfnDispatch) {
                pfnSetPipelineState(d3d12GraphicsCommandList, m_d3d12PipelineState);
                pfnSetComputeRootSignature(d3d12GraphicsCommandList, m_d3d12RootSignature);
                pfnSetConstants(d3d12GraphicsCommandList, 0, sizeof(pushConstants) / 4, &pushConstants, 0);
                pfnDispatch(d3d12GraphicsCommandList, groupCountX, groupCountY, 1);
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
