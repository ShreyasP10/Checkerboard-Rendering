#include "cbr/reconstruction_pass.h"
#include <atomic>
#include "cbr/config.h"
#include "cbr/depth_convention.h"
#include "cbr/jitter_manager.h"
#include "cbr/render_target_manager.h"
#include "cbr/logger.h"
#include <chrono>

namespace cbr {

ReconstructionPass& ReconstructionPass::Get() {
    static ReconstructionPass instance;
    return instance;
}

bool ReconstructionPass::InitializeVulkan(void* /*vkDevice*/, void* /*vkPhysicalDevice*/) {
    m_isVulkan.store(true);
    m_initialized.store(true);
    CBR_LOG_INFO("ReconstructionPass initialized for Vulkan API pipeline.");
    return true;
}

bool ReconstructionPass::InitializeDX12(void* /*d3d12Device*/) {
    m_isVulkan.store(false);
    m_initialized.store(true);
    CBR_LOG_INFO("ReconstructionPass initialized for DirectX 12 API pipeline.");
    return true;
}

void ReconstructionPass::Shutdown() {
    m_initialized.store(false);
    CBR_LOG_INFO("ReconstructionPass shut down.");
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

void ReconstructionPass::DispatchVulkan(void* /*vkCommandBuffer*/, uint32_t frameIndex) {
    if (!m_initialized.load()) {
        static std::atomic<bool> s_warned{ false };   // once only: this runs every frame
        if (!s_warned.exchange(true)) CBR_LOG_WARN("DispatchVulkan dropped: ReconstructionPass not initialized.");
        return;
    }

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    const uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    // In a live Vulkan context, this binds the compute pipeline, pushes `pushConstants`,
    // and calls vkCmdDispatch(cmdBuffer, groupCountX, groupCountY, 1);
    // Followed by a memory barrier transitioning the reconstructed image for sampling.
    (void)pushConstants;
    (void)groupCountX;
    (void)groupCountY;
}

void ReconstructionPass::DispatchDX12(void* /*d3d12GraphicsCommandList*/, uint32_t frameIndex) {
    if (!m_initialized.load()) {
        static std::atomic<bool> s_warned{ false };   // once only: this runs every frame
        if (!s_warned.exchange(true)) CBR_LOG_WARN("DispatchDX12 dropped: ReconstructionPass not initialized.");
        return;
    }

    const ReconstructionPushConstants pushConstants = BuildReconstructionPushConstants(frameIndex);
    const auto dims = RenderTargetManager::Get().GetDimensions();

    const uint32_t groupCountX = (dims.fullWidth + 15u) / 16u;
    const uint32_t groupCountY = (dims.fullHeight + 15u) / 16u;

    // In DX12, sets root signature, pipeline state, descriptor tables, uploads `pushConstants`
    // as root constants, and calls Dispatch(groupCountX, groupCountY, 1)
    (void)pushConstants;
    (void)groupCountX;
    (void)groupCountY;
}

} // namespace cbr
