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
    uint32_t colorSpace;            // 0 = YCoCg, 1 = RGB
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

    void SetShaderDirectory(const std::filesystem::path& dir) { m_shaderDir = dir; }
    const std::filesystem::path& GetShaderDirectory() const { return m_shaderDir; }

    bool IsInitialized() const { return m_initialized.load(); }
    void* GetPipeline() const { return m_pipeline; }
    void* GetPipelineLayout() const { return m_pipelineLayout; }
    void* GetDescriptorSetLayout() const { return m_descriptorSetLayout; }
    void* GetDescriptorPool() const { return m_descriptorPool; }
    void* GetDescriptorSet() const { return m_descriptorSet; }

    // Helper to load SPIR-V bytecode from disk
    static std::vector<uint32_t> LoadSpvFile(const std::filesystem::path& path);

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    bool CreateVulkanPipeline();
    void DestroyVulkanPipeline();

    std::atomic<bool>     m_initialized{ false };
    std::atomic<bool>     m_isVulkan{ true };
    std::filesystem::path m_shaderDir;

    void* m_device{ nullptr };
    void* m_pipeline{ nullptr };
    void* m_pipelineLayout{ nullptr };
    void* m_descriptorSetLayout{ nullptr };
    void* m_descriptorPool{ nullptr };
    void* m_descriptorSet{ nullptr };
};

} // namespace cbr
