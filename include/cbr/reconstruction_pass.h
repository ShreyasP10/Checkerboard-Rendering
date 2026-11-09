#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cbr {

struct ReconstructionPushConstants {
    float    targetResolution[2];
    float    invTargetResolution[2];
    uint32_t frameIndex;
    float    depthTolerance;
    float    historyWeight;
    uint32_t debugView;
    uint32_t enableColorClamping;
    float    mipLodBias;
    uint32_t colorSpace;            // 0 = YCoCg, 1 = RGB, 2 = ICtCp (HDR10 / BT.2100)
    uint32_t enableSpatialFallback; // 1 = on
    float    jitterDelta[2];        // subpixel projection jitter delta (jc - jp)
    uint32_t enableMotionDilation;  // 1 = 3x3 closest-depth motion dilation
    float    jitterCompensation;    // multiplier on jitterDelta (default 0: not needed, see checkerboard_mapping.h)
    int32_t  shiftDirection;        // odd-frame sampling-grid shift: +1 / -1 (see checkerboard_mapping.h)
    uint32_t depthMode;             // 0 = Standard, 1 = Reversed
    float    depthNear;
    float    depthFar;              // 0 = infinite (Reversed only)
};

// Must match the push-constant block / cbuffer in shaders/cbr_reconstruct.{comp,hlsl}
static_assert(sizeof(ReconstructionPushConstants) == 80, "push constant layout drifted from the shaders");
static_assert(sizeof(ReconstructionPushConstants) % 16 == 0, "cbuffer size must be a multiple of 16 bytes");

// Builds the push-constant / cbuffer payload from the current config, jitter state and target size.
// Shared by the Vulkan and DX12 dispatch paths so they cannot diverge.
ReconstructionPushConstants BuildReconstructionPushConstants(uint32_t frameIndex);

class ReconstructionPass {
public:
    static ReconstructionPass& Get();

    bool InitializeVulkan(void* vkDevice, void* vkPhysicalDevice);
    bool InitializeDX12(void* d3d12Device);
    void Shutdown();

    // Dispatch compute shader
    void DispatchVulkan(void* vkCommandBuffer, uint32_t frameIndex);
    void DispatchDX12(void* d3d12GraphicsCommandList, uint32_t frameIndex);

    // Updates descriptor set with active Vulkan image views and samplers
    bool UpdateVulkanDescriptors(void* quarterColorView, void* quarterDepthView,
                                void* historyColorView, void* historyDepthView,
                                void* velocityView,
                                void* outputImageView, void* outputDepthView,
                                void* linearSampler, void* pointSampler);

    // Updates both ping-pong descriptor sets (set 0: read A/write B, set 1: read B/write A)
    bool UpdateVulkanPingPongDescriptors(void* quarterColorView, void* quarterDepthView,
                                        void* historyColorViewA, void* historyDepthViewA,
                                        void* historyColorViewB, void* historyDepthViewB,
                                        void* velocityView,
                                        void* outputImageView, void* outputDepthView,
                                        void* linearSampler, void* pointSampler);

    // Helper function to transition image layouts and insert memory barriers with correct stage/access masks
    static void TransitionImageLayout(void* vkCommandBuffer, void* vkImage,
                                      uint32_t oldLayout, uint32_t newLayout,
                                      uint32_t srcStage, uint32_t dstStage,
                                      uint32_t srcAccess, uint32_t dstAccess,
                                      uint32_t aspectMask = 1);

    // Helper function to blit reconstructed color image to a destination image (e.g. swapchain backbuffer)
    static void BlitToImage(void* vkCommandBuffer, void* srcImage, void* dstImage,
                            uint32_t srcWidth, uint32_t srcHeight,
                            uint32_t dstWidth, uint32_t dstHeight,
                            uint32_t targetDstLayout = 1000001002 /* VK_IMAGE_LAYOUT_PRESENT_SRC_KHR */);

    // Helper function to copy an image region (e.g. game attachments to mod intermediate buffers, Item C-01)
    static void CopyImageRegion(void* vkCommandBuffer, void* srcImage, void* dstImage,
                                uint32_t srcX, uint32_t srcY,
                                uint32_t dstX, uint32_t dstY,
                                uint32_t width, uint32_t height,
                                uint32_t aspectMask = 1);

    void EnsureInitialLayoutsTransitioned(void* vkCommandBuffer);
    void ResetLayoutTransitions() { m_initialLayoutsTransitioned.store(false, std::memory_order_release); }

    bool AreDescriptorsBound() const { return m_descriptorsBound.load(); }

    void SetShaderDirectory(const std::filesystem::path& dir) { m_shaderDir = dir; }
    const std::filesystem::path& GetShaderDirectory() const { return m_shaderDir; }

    bool IsInitialized() const { return m_initialized.load(); }
    void* GetPipeline() const { return m_pipeline; }
    void* GetPipelineLayout() const { return m_pipelineLayout; }
    void* GetDescriptorSetLayout() const { return m_descriptorSetLayout; }
    void* GetDescriptorPool() const { return m_descriptorPool; }
    void* GetDescriptorSet(uint32_t index = 0) const { return m_descriptorSets[index < 2 ? index : 0]; }

    // Helper to load SPIR-V bytecode from disk
    static std::vector<uint32_t> LoadSpvFile(const std::filesystem::path& path);

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    bool CreateVulkanPipeline();
    void DestroyVulkanPipeline();

    std::atomic<bool>     m_initialized{ false };
    std::atomic<bool>     m_descriptorsBound{ false };
    std::atomic<bool>     m_isVulkan{ true };
    std::atomic<bool>     m_initialLayoutsTransitioned{ false };
    std::filesystem::path m_shaderDir;

    void* m_device{ nullptr };
    void* m_pipeline{ nullptr };
    void* m_pipelineLayout{ nullptr };
    void* m_descriptorSetLayout{ nullptr };
    void* m_descriptorPool{ nullptr };
    void* m_descriptorSets[2]{ nullptr, nullptr };

    // Cached Vulkan device function pointers to avoid GetProcAddress overhead in render hot loop
    using PFN_CmdPipelineBarrier = void (*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, const void*, uint32_t, const void*, uint32_t, const void*);
    using PFN_CmdBlitImage = void (*)(void*, void*, uint32_t, void*, uint32_t, uint32_t, const void*, uint32_t);
    using PFN_CmdBindPipeline = void (*)(void*, uint32_t, void*);
    using PFN_CmdBindDescriptorSets = void (*)(void*, uint32_t, void*, uint32_t, uint32_t, const void* const*, uint32_t, const uint32_t*);
    using PFN_CmdPushConstants = void (*)(void*, void*, uint32_t, uint32_t, uint32_t, const void*);
    using PFN_CmdDispatch = void (*)(void*, uint32_t, uint32_t, uint32_t);

    PFN_CmdPipelineBarrier    m_pfnCmdPipelineBarrier{ nullptr };
    PFN_CmdBlitImage          m_pfnCmdBlitImage{ nullptr };
    using PFN_CmdCopyImage = void (*)(void*, void*, uint32_t, void*, uint32_t, uint32_t, const void*);
    PFN_CmdCopyImage          m_pfnCmdCopyImage{ nullptr };
    PFN_CmdBindPipeline       m_pfnCmdBindPipeline{ nullptr };
    PFN_CmdBindDescriptorSets m_pfnCmdBindDescriptorSets{ nullptr };
    PFN_CmdPushConstants      m_pfnCmdPushConstants{ nullptr };
    PFN_CmdDispatch           m_pfnCmdDispatch{ nullptr };

    // DirectX 12 Pipeline State (Item H-02)
    void*                     m_d3d12Device{ nullptr };
    void*                     m_d3d12RootSignature{ nullptr };
    void*                     m_d3d12PipelineState{ nullptr };
};

} // namespace cbr
