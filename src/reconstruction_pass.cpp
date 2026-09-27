#include "cbr/reconstruction_pass.h"
#include "cbr/config.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"
#include <chrono>

namespace cbr {

ReconstructionPass& ReconstructionPass::Get() {
    static ReconstructionPass instance;
    return instance;
}

bool ReconstructionPass::InitializeVulkan(void* /*vkDevice*/, void* /*vkPhysicalDevice*/) {
    m_isVulkan = true;
    m_initialized = true;
    CBR_LOG_INFO("ReconstructionPass initialized for Vulkan API pipeline.");
    return true;
}

bool ReconstructionPass::InitializeDX12(void* /*d3d12Device*/) {
    m_isVulkan = false;
    m_initialized = true;
    CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline.");
    return true;
}

void ReconstructionPass::Shutdown() {
    m_initialized = false;
    CBR_LOG_INFO("ReconstructionPass shut down.");
}

void ReconstructionPass::DispatchVulkan(void* /*vkCommandBuffer*/, uint32_t frameIndex) {
    if (!m_initialized) return;

    const auto& config = ConfigManager::Get().GetConfig();
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    ReconstructionPushConstants pushConstants{};
    pushConstants.targetResolution[0] = static_cast<float>(dims.fullWidth);
    pushConstants.targetResolution[1] = static_cast<float>(dims.fullHeight);
    pushConstants.invTargetResolution[0] = 1.0f / pushConstants.targetResolution[0];
    pushConstants.invTargetResolution[1] = 1.0f / pushConstants.targetResolution[1];
    pushConstants.frameIndex = frameIndex;
    pushConstants.depthTolerance = config.depthTolerance;
    pushConstants.historyWeight = config.historyWeight;
    pushConstants.debugView = config.debugView;
    pushConstants.enableColorClamping = config.enableColorClamping ? 1u : 0u;
    pushConstants.mipLodBias = config.mipLodBias;

    uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    // In a live Vulkan context, this binds the compute pipeline, pushes constants,
    // and calls vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);
    // Followed by a memory barrier transitioning the reconstructed image for sampling.
    (void)groupCountX;
    (void)groupCountY;
}

void ReconstructionPass::DispatchDX12(void* /*d3d12GraphicsCommandList*/, uint32_t frameIndex) {
    if (!m_initialized) return;

    const auto& config = ConfigManager::Get().GetConfig();
    const auto& dims = RenderTargetManager::Get().GetDimensions();

    uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    (void)config;
    (void)frameIndex;
    (void)groupCountX;
    (void)groupCountY;
    // In DX12, sets root signature, pipeline state, descriptor tables, and calls Dispatch(groupCountX, groupCountY, 1)
}

} // namespace cbr
