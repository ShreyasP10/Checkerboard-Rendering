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
};

// Must match the push-constant block / cbuffer in shaders/cbr_reconstruct.{comp,hlsl}
static_assert(sizeof(ReconstructionPushConstants) == 48, "push constant layout drifted from the shaders");
static_assert(sizeof(ReconstructionPushConstants) % 16 == 0, "cbuffer size must be a multiple of 16 bytes");

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
