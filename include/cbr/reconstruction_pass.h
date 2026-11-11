#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
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
    float    mipLodBias;            // Retained for 80-byte push-constant ABI parity; enforced CPU-side in Hooked_vkCreateSampler (Item M-01)
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

// Camera matrices & post-resolve controls bound via binding = 7 (Vulkan std140 UBO) / register(b1) (DX12 CBV)
struct alignas(16) CBRCameraUniforms {
    float    currViewProjInv[16];
    float    prevViewProj[16];
    float    sharpeningStrength;
    uint32_t enableCameraFallback;
    uint32_t hasCameraMatrices;
    uint32_t reservedPad;
};
static_assert(sizeof(CBRCameraUniforms) == 144, "CBRCameraUniforms layout drifted from shader UBO/cbuffer");

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
                            uint32_t targetDstLayout = 1000001002 /* VK_IMAGE_LAYOUT_PRESENT_SRC_KHR */,
                            uint32_t srcOldLayout = 1 /* VK_IMAGE_LAYOUT_GENERAL */,
                            uint32_t dstOldLayout = 2 /* VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL */);

    // Helper function to copy an image region (e.g. game attachments to mod intermediate buffers, Item C-01)
    static void CopyImageRegion(void* vkCommandBuffer, void* srcImage, void* dstImage,
                                uint32_t srcX, uint32_t srcY,
                                uint32_t dstX, uint32_t dstY,
                                uint32_t width, uint32_t height,
                                uint32_t aspectMask = 1,
                                uint32_t srcOldLayout = 2 /* VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL */,
                                uint32_t dstOldLayout = 5 /* VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL */,
                                uint32_t srcFinalLayout = 2 /* VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL */,
                                uint32_t dstFinalLayout = 5 /* VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL */,
                                uint32_t dstAspectMask = 0 /* 0 => match aspectMask */);

    void EnsureInitialLayoutsTransitioned(void* vkCommandBuffer);
    void ResetLayoutTransitions() { m_initialLayoutsTransitioned.store(false, std::memory_order_release); }

    bool AreDescriptorsBound() const { return m_descriptorsBound.load(); }

    void SetShaderDirectory(const std::filesystem::path& dir) { m_shaderDir = dir; }
    const std::filesystem::path& GetShaderDirectory() const { return m_shaderDir; }

    // Camera-only depth unprojection matrices (Section 5.4)
    bool SetCameraMatrices(const float* currViewProj4x4, const float* prevViewProj4x4 = nullptr);
    void ClearCameraMatrices();
    static bool InvertMatrix4x4(const float* m, float* outInv);
    CBRCameraUniforms BuildCameraUniforms() const;

    bool IsInitialized() const { return m_initialized.load(); }
    [[nodiscard]] float GetLastDispatchDurationMs() const { return m_lastDispatchDurationMs.load(std::memory_order_acquire); }
    void* GetPipeline() const { return m_pipeline; }
    void* GetPipelineLayout() const { return m_pipelineLayout; }
    void* GetDescriptorSetLayout() const { return m_descriptorSetLayout; }
    void* GetDescriptorPool() const { return m_descriptorPool; }
    void* GetDescriptorSet(uint32_t index = 0) const { return m_descriptorSets[index < 2 ? index : 0]; }
    void* GetDX12RootSignature() const { return m_d3d12RootSignature; }
    void* GetDX12PipelineState() const { return m_d3d12PipelineState; }

    // Helper to load SPIR-V bytecode from disk
    static std::vector<uint32_t> LoadSpvFile(const std::filesystem::path& path);

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    bool CreateVulkanPipeline();
    void DestroyVulkanPipeline();
    bool CreateDX12Pipeline();
    void DestroyDX12Pipeline();

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
    using PFN_CmdClearColorImage = void (*)(void*, void*, uint32_t, const void*, uint32_t, const void*);
    PFN_CmdCopyImage          m_pfnCmdCopyImage{ nullptr };
    PFN_CmdClearColorImage    m_pfnCmdClearColorImage{ nullptr };
    PFN_CmdBindPipeline       m_pfnCmdBindPipeline{ nullptr };
    PFN_CmdBindDescriptorSets m_pfnCmdBindDescriptorSets{ nullptr };
    PFN_CmdPushConstants      m_pfnCmdPushConstants{ nullptr };
    PFN_CmdDispatch           m_pfnCmdDispatch{ nullptr };

    // Vulkan camera uniform buffer (binding = 7, persistent coherent mapping)
    void*                     m_cameraUboBuffer{ nullptr };
    void*                     m_cameraUboMemory{ nullptr };
    void*                     m_cameraUboMapped{ nullptr };

    // DirectX 12 Pipeline State (Item H-02)
    void*                     m_d3d12Device{ nullptr };
    void*                     m_d3d12RootSignature{ nullptr };
    void*                     m_d3d12PipelineState{ nullptr };
    void*                     m_d3d12CameraCbvResource{ nullptr };
    void*                     m_d3d12CameraCbvMapped{ nullptr };
    uint64_t                  m_d3d12CameraCbvGpuVa{ 0 };

    // Camera unprojection matrix tracking state (Section 5.4)
    mutable std::mutex        m_cameraMutex;
    float                     m_currViewProj[16]{};
    float                     m_currViewProjInv[16]{};
    float                     m_prevViewProj[16]{};
    bool                      m_hasCameraMatrices{ false };
    bool                      m_hasPrevMatrix{ false };

    // Vulkan & CPU reconstruction duration telemetry (Item M-05)
    void*                     m_vkPhysicalDevice{ nullptr };
    void*                     m_vkQueryPools[2]{ nullptr, nullptr };
    bool                      m_vkQueryRecorded[2]{ false, false };
    float                     m_vkTimestampPeriodNs{ 0.0f };
    std::atomic<float>        m_lastDispatchDurationMs{ 0.0f };
};

} // namespace cbr
