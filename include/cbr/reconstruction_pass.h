#pragma once

#include <atomic>
#include <cstdint>
#include <string>

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
    float    jitterCompensation;    // multiplier on jitterDelta (1, -1 or 0)
};

// Must match the push-constant block / cbuffer in shaders/cbr_reconstruct.{comp,hlsl}
static_assert(sizeof(ReconstructionPushConstants) == 64, "push constant layout drifted from the shaders");
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

    bool IsInitialized() const { return m_initialized.load(); }

private:
    ReconstructionPass() = default;
    ~ReconstructionPass() = default;

    std::atomic<bool> m_initialized{ false };
    bool m_isVulkan{ true };
};

} // namespace cbr
