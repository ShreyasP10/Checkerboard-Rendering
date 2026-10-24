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
constexpr uint32_t kVkStructureTypeMemoryBarrier = 46;

constexpr uint32_t kVkShaderStageComputeBit = 0x00000020;
constexpr uint32_t kVkPipelineBindPointCompute = 1;
constexpr uint32_t kVkPipelineStageComputeShaderBit = 0x00000800;
constexpr uint32_t kVkPipelineStageAllCommandsBit = 0x00010000;
constexpr uint32_t kVkAccessShaderWriteBit = 0x00000040;
constexpr uint32_t kVkAccessShaderReadBit = 0x00000020;
constexpr uint32_t kVkAccessMemoryReadBit = 0x00008000;

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
typedef int   (*PFN_vkCreatePipelineLayout)(void* device, const MinimalVkPipelineLayoutCreateInfo* pCreateInfo, const void* pAllocator, void** pPipelineLayout);
typedef void  (*PFN_vkDestroyPipelineLayout)(void* device, void* pipelineLayout, const void* pAllocator);
typedef int   (*PFN_vkCreateComputePipelines)(void* device, void* pipelineCache, uint32_t createInfoCount, const MinimalVkComputePipelineCreateInfo* pCreateInfos, const void* pAllocator, void** pPipelines);
typedef void  (*PFN_vkDestroyPipeline)(void* device, void* pipeline, const void* pAllocator);
typedef void  (*PFN_vkCmdBindPipeline)(void* commandBuffer, uint32_t pipelineBindPoint, void* pipeline);
typedef void  (*PFN_vkCmdBindDescriptorSets)(void* commandBuffer, uint32_t pipelineBindPoint, void* layout, uint32_t firstSet, uint32_t descriptorSetCount, const void** pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
typedef void  (*PFN_vkCmdPushConstants)(void* commandBuffer, void* layout, uint32_t stageFlags, uint32_t offset, uint32_t size, const void* pValues);
typedef void  (*PFN_vkCmdDispatch)(void* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
typedef void  (*PFN_vkCmdPipelineBarrier)(void* commandBuffer, uint32_t srcStageMask, uint32_t dstStageMask, uint32_t dependencyFlags, uint32_t memoryBarrierCount, const MinimalVkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void* pImageMemoryBarriers);

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
    void* descriptorSet = nullptr;

    if (descriptorSetLayout && pfnCreateDescriptorPool && pfnAllocateDescriptorSets) {
        MinimalVkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = kVkDescriptorTypeCombinedImageSampler;
        poolSizes[0].descriptorCount = 5;
        poolSizes[1].type = kVkDescriptorTypeStorageImage;
        poolSizes[1].descriptorCount = 2;

        MinimalVkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = kVkStructureTypeDescriptorPoolCreateInfo;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;

        res = pfnCreateDescriptorPool(m_device, &poolInfo, nullptr, &descriptorPool);
        if (res == 0 && descriptorPool) {
            MinimalVkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = kVkStructureTypeDescriptorSetAllocateInfo;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;

            res = pfnAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet);
            if (res != 0 || !descriptorSet) {
                CBR_LOG_WARN("vkAllocateDescriptorSets warning (code: %d); descriptor set binding deferred.", res);
            }
        }
    }

    m_descriptorPool = descriptorPool;
    m_descriptorSet = descriptorSet;
    CBR_LOG_INFO("Vulkan CBR reconstruction compute pipeline created successfully! (Pipeline: %p, Layout: %p, Descriptors: %p, Pool: %p, Set: %p)",
        m_pipeline, m_pipelineLayout, m_descriptorSetLayout, m_descriptorPool, m_descriptorSet);
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

    auto pfnDestroyPipeline = reinterpret_cast<PFN_vkDestroyPipeline>(resolveDeviceFunc("vkDestroyPipeline"));
    auto pfnDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(resolveDeviceFunc("vkDestroyPipelineLayout"));
    auto pfnDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(resolveDeviceFunc("vkDestroyDescriptorSetLayout"));
    auto pfnDestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(resolveDeviceFunc("vkDestroyDescriptorPool"));

    if (m_descriptorPool && pfnDestroyDescriptorPool) {
        pfnDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = nullptr;
        m_descriptorSet = nullptr;
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
#endif
}

bool ReconstructionPass::InitializeVulkan(void* vkDevice, void* /*vkPhysicalDevice*/) {
    m_isVulkan.store(true);
    if (!vkDevice) {
        m_initialized.store(true);
        CBR_LOG_INFO("ReconstructionPass initialized for Vulkan API pipeline (device handle deferred).");
        return true;
    }

    m_device = vkDevice;
    const bool pipelineCreated = CreateVulkanPipeline();
    m_initialized.store(pipelineCreated);
    return pipelineCreated;
}

bool ReconstructionPass::InitializeDX12(void* /*d3d12Device*/) {
    m_isVulkan.store(false);
    m_initialized.store(true);
    CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline.");
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
    m_descriptorSet = nullptr;
    m_initialized.store(false);
    CBR_LOG_INFO("ReconstructionPass shut down cleanly.");
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
    pc.invTargetResolution[0] = 1.0f / fw;
    pc.invTargetResolution[1] = 1.0f / fh;
    pc.frameIndex = frameIndex;
    pc.depthTolerance = config.depthTolerance;
    pc.historyWeight = config.historyWeight;
    pc.debugView = config.debugView;
    pc.enableColorClamping = config.enableColorClamping ? 1u : 0u;
    pc.mipLodBias = config.mipLodBias;
    pc.colorSpace = (config.colorSpace == ColorSpace::RGB) ? 1u : 0u;
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

#if defined(_WIN32)
    HMODULE vulkanModule = GetModuleHandleA("vulkan-1.dll");
    if (!vulkanModule) return;

    auto pfnCmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(GetProcAddress(vulkanModule, "vkCmdBindPipeline"));
    auto pfnCmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(GetProcAddress(vulkanModule, "vkCmdBindDescriptorSets"));
    auto pfnCmdPushConstants = reinterpret_cast<PFN_vkCmdPushConstants>(GetProcAddress(vulkanModule, "vkCmdPushConstants"));
    auto pfnCmdDispatch = reinterpret_cast<PFN_vkCmdDispatch>(GetProcAddress(vulkanModule, "vkCmdDispatch"));
    auto pfnCmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(GetProcAddress(vulkanModule, "vkCmdPipelineBarrier"));

    if (!pfnCmdBindPipeline || !pfnCmdPushConstants || !pfnCmdDispatch) {
        return;
    }

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t targetW = dims.fullWidth > 0 ? dims.fullWidth : 3840u;
    const uint32_t targetH = dims.fullHeight > 0 ? dims.fullHeight : 2160u;
    const uint32_t groupCountX = (targetW + 15u) / 16u;
    const uint32_t groupCountY = (targetH + 15u) / 16u;

    pfnCmdBindPipeline(vkCommandBuffer, kVkPipelineBindPointCompute, m_pipeline);

    if (m_descriptorSet && pfnCmdBindDescriptorSets) {
        pfnCmdBindDescriptorSets(vkCommandBuffer, kVkPipelineBindPointCompute, m_pipelineLayout, 0, 1, const_cast<const void**>(&m_descriptorSet), 0, nullptr);
    }

    pfnCmdPushConstants(vkCommandBuffer, m_pipelineLayout, kVkShaderStageComputeBit, 0, sizeof(pushConstants), &pushConstants);
    pfnCmdDispatch(vkCommandBuffer, groupCountX, groupCountY, 1);

    if (pfnCmdPipelineBarrier) {
        MinimalVkMemoryBarrier barrier{};
        barrier.sType = kVkStructureTypeMemoryBarrier;
        barrier.srcAccessMask = kVkAccessShaderWriteBit;
        barrier.dstAccessMask = kVkAccessShaderReadBit | kVkAccessMemoryReadBit;
        pfnCmdPipelineBarrier(vkCommandBuffer,
                              kVkPipelineStageComputeShaderBit,
                              kVkPipelineStageAllCommandsBit,
                              0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
#else
    (void)frameIndex;
#endif
}

void ReconstructionPass::DispatchDX12(void* /*d3d12GraphicsCommandList*/, uint32_t frameIndex) {
    if (!m_initialized.load()) {
        static std::atomic<bool> s_warned{ false };
        if (!s_warned.exchange(true)) CBR_LOG_WARN("DispatchDX12 dropped: ReconstructionPass not initialized.");
        return;
    }

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    const uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    (void)pushConstants;
    (void)groupCountX;
    (void)groupCountY;
}

} // namespace cbr
